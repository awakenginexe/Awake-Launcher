// SPDX-License-Identifier: GPL-3.0-only
#include "WorkspaceServices.h"
#include "PackArchive.h"
#include "InboxService.h"
#include "awake/integration/ProfileAccess.h"
#include "awake/integration/DurableIO.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSet>
#include <QUuid>
namespace Awake::Core {
namespace {
QJsonObject unpack(const QString& profile, const QJsonObject& request, const QString& directory)
{
    if (!Awake::Integration::createPrivateStorageDirectory(directory)) throw ServiceError{"IO_ERROR", "Private import staging is unavailable."};
    auto result = PackArchive::inspect(InboxService(profile).committedPath(request["token"].toString()), request["format"].toString(), directory);
    if (!result["ok"].toBool()) throw ServiceError{"INVALID_ARCHIVE", result["error"].toString().left(2048)};
    auto loader = result["loader"].toObject();
    if (loader["id"] == "fabric-loader") loader["id"] = "fabric";
    if (loader["id"] == "quilt-loader") loader["id"] = "quilt";
    result["loader"] = loader;
    return result;
}
bool gamePath(const QString& path)
{
    const auto top = path.section('/', 0, 0);
    return !top.startsWith('.') && top != "instance.cfg" && top != "mmc-pack.json" &&
        !path.startsWith("mods/.index/", Qt::CaseInsensitive) && !path.startsWith("mods/.awake-", Qt::CaseInsensitive);
}
}
QJsonObject WorkspaceServices::inspectImport(const QJsonObject& request)
{
    const auto directory = m_profile + "/inbox/inspect-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto cleanup = qScopeGuard([&] { QDir(directory).removeRecursively(); });
    const auto parsed = unpack(m_profile, request, directory); QJsonArray files, warnings;
    for (const auto& value : parsed["files"].toArray()) {
        const auto file = value.toObject();
        if (!gamePath(file["path"].toString())) throw ServiceError{"INVALID_ARCHIVE", "Package attempts to replace reserved metadata."};
        files.append(QJsonObject{{"path", file["path"]}, {"size", QString::number(qint64(file["size"].toDouble()))}, {"sha256", file["sha256"]}});
    }
    if (request["format"] == "curseforge" && !m_curseforge.available()) warnings.append("CurseForge files require separately authorized local API access; the distributed helper has no credential.");
    for (const auto& value : parsed["downloads"].toArray()) {
        const auto file = value.toObject(); bool permitted = false;
        for (const auto& url : file["downloads"].toArray()) if (QUrl(url.toString()).host() == "cdn.modrinth.com") permitted = true;
        if (!permitted && warnings.size() < 128) warnings.append("Manual artifact required: " + file["path"].toString().left(900));
    }
    return {{"minecraftVersion", parsed["minecraftVersion"]}, {"loader", parsed["loader"]}, {"files", files},
        {"projects", parsed["projects"]}, {"downloadCount", parsed["downloads"].toArray().size()}, {"warnings", warnings}};
}
void WorkspaceServices::prepareImport(const QString& id, const QJsonObject& request)
{
    m_provider.checkCancelled();
    const auto directory = operationDirectory(id) + "/import";
    const auto parsed = unpack(m_profile, request, directory), record = workspaceRecord(request["physicalId"].toString());
    if (parsed["minecraftVersion"] != record["minecraftVersion"] || parsed["loader"] != record["loader"])
        throw ServiceError{"DEPENDENCY_CONFLICT", "Create or select a workspace with the package's exact Minecraft and loader versions."};
    QJsonArray changes, files, warnings; QSet<QString> destinations;
    const bool includeOptional = request["includeOptional"].toBool();
    for (const auto& value : parsed["files"].toArray()) {
        auto file = value.toObject(); const auto path = file["path"].toString();
        if (!gamePath(path)) throw ServiceError{"INVALID_ARCHIVE", "Package attempts to replace reserved metadata."};
        file["path"] = ".minecraft/" + path; files.append(file); destinations.insert(path.toCaseFolded());
    }
    QJsonArray inventory;
    { auto lease = acquire(record); inventory = scan(record); }
    QSet<QString> installed;
    for (const auto& value : inventory) {
        const auto row = value.toObject();
        if (row["projectId"].isString()) installed.insert(row["provider"].toString("modrinth") + ':' + row["projectId"].toString());
    }
    const auto select = [&](const QString& provider, const QString& project, const QString& version, const QString& artifactHash = QString{}) {
        QJsonObject change{{"action", installed.contains(provider + ':' + project) ? "replace" : "install"}, {"projectId", project}, {"versionId", version}};
        if (provider != "modrinth") change["provider"] = provider;
        if (!artifactHash.isEmpty()) change["artifactSha512"] = artifactHash;
        changes.append(change);
        if (changes.size() > 1024) throw ServiceError{"LIMIT_EXCEEDED", "Package exceeds 1,024 provider projects."};
    };
    for (const auto& value : parsed["projects"].toArray()) {
        m_provider.checkCancelled(); const auto project = value.toObject();
        if (!project["required"].toBool() && !includeOptional) { if (warnings.size() < 128) warnings.append("Skipped optional CurseForge project " + project["projectId"].toString()); continue; }
        if (!m_curseforge.available()) throw ServiceError{"PROVIDER_RESTRICTED", "CurseForge import requires separately authorized API access; no project key is distributed."};
        select("curseforge", project["projectId"].toString(), project["versionId"].toString());
    }
    int ordinal = parsed["files"].toArray().size();
    for (const auto& value : parsed["downloads"].toArray()) {
        m_provider.checkCancelled(); const auto file = value.toObject(); const auto path = file["path"].toString();
        if (file["env"].toObject()["client"] == "optional" && !includeOptional) { if (warnings.size() < 128) warnings.append("Skipped optional file " + path.left(900)); continue; }
        if (!gamePath(path) || destinations.contains(path.toCaseFolded())) throw ServiceError{"POLICY_CONFLICT", "Package contains conflicting or reserved file destinations."};
        destinations.insert(path.toCaseFolded()); QString url;
        for (const auto& candidate : file["downloads"].toArray()) if (QUrl(candidate.toString()).host() == "cdn.modrinth.com") { url = candidate.toString(); break; }
        if (url.isEmpty()) throw ServiceError{"PROVIDER_RESTRICTED", "Manual distribution is required for " + path.left(1500) + "; no bytes were applied."};
        const auto hashes = file["hashes"].toObject();
        if (path.startsWith("mods/") && path.count('/') == 1 && path.endsWith(".jar", Qt::CaseInsensitive)) {
            // A Fabric-only file in a Forge/NeoForge pack is admitted only for this exact hash and only if the
            // resulting workspace enables Sinytra Connector (enforced by prepare's graph check).
            const auto resolved = m_provider.versionByHash(hashes["sha512"].toString(), record["minecraftVersion"].toString(), record["loader"].toObject()["id"].toString(), true);
            const auto artifact = resolved.wire["files"].toArray().first().toObject();
            if (artifact["sha512"] != hashes["sha512"] || artifact["sha1"] != hashes["sha1"] || artifact["size"].toString() != QString::number(qint64(file["fileSize"].toDouble())) || artifact["fileName"] != path.mid(5))
                throw ServiceError{"INTEGRITY_FAILED", "Package file differs from its compatible provider version."};
            select("modrinth", resolved.wire["projectId"].toString(), resolved.wire["versionId"].toString(), hashes["sha512"].toString());
            if (!resolved.bridge.isEmpty()) { auto change = changes.last().toObject(); change["loaderBridge"] = resolved.bridge; changes.replace(changes.size() - 1, change); }
        } else {
            const auto name = "download-" + QString::number(++ordinal);
            m_provider.download({{"url", url}, {"size", QString::number(qint64(file["fileSize"].toDouble()))}, {"sha512", hashes["sha512"]}, {"sha1", hashes["sha1"]}}, directory + '/' + name);
            files.append(QJsonObject{{"path", ".minecraft/" + path}, {"stagedName", name}, {"sha256", Awake::Integration::DurableIO::hash(directory + '/' + name)}});
        }
    }
    auto parameters = request; parameters["changes"] = changes;
    prepare(id, parameters, files, warnings);
}
}
