// SPDX-License-Identifier: GPL-3.0-only
#include "WorkspaceServices.h"
#include "PackArchive.h"
#include "InboxService.h"
#include "awake/integration/ProfileAccess.h"
#include "awake/integration/DurableIO.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QMutex>
#include <QRunnable>
#include <QThreadPool>
#include <atomic>
#include <optional>
#include <QUrl>
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
    const bool authoritative = request["packAuthoritative"].toBool();
    m_provider.setPackAuthoritative(authoritative); m_curseforge.setPackAuthoritative(authoritative);
    const auto directory = operationDirectory(id) + "/import";
    const auto parsed = unpack(m_profile, request, directory), record = workspaceRecord(request["physicalId"].toString());
    if (record.contains("sharedRoot")) throw ServiceError{"UNSUPPORTED_CAPABILITY", "Update this modpack in Awake Launcher."};
    const bool loaderBump = authoritative && request["allowLoaderUpdate"].toBool() && parsed["minecraftVersion"] == record["minecraftVersion"] &&
        parsed["loader"].toObject()["id"] == record["loader"].toObject()["id"] && parsed["loader"] != record["loader"];
    if (!loaderBump && (parsed["minecraftVersion"] != record["minecraftVersion"] || parsed["loader"] != record["loader"]))
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
    QSet<QString> installed, locked, chosen, sameVersion;
    for (const auto& value : inventory) {
        const auto row = value.toObject();
        if (row["projectId"].isString()) {
            const auto key = row["provider"].toString("modrinth") + ':' + row["projectId"].toString();
            installed.insert(key);
            if (row["updateLocked"].toBool()) locked.insert(key);
            sameVersion.insert(key + ':' + row["versionId"].toString());
        }
    }
    const auto select = [&](const QString& provider, const QString& project, const QString& version, const QString& artifactHash = QString{}) {
        const auto key = provider + ':' + project;
        if (sameVersion.contains(key + ':' + version)) { chosen.insert(key); return; } // a pack update keeps files that did not change
        if (authoritative && locked.contains(key)) { if (warnings.size() < 128) warnings.append("Kept your pinned version of " + project); chosen.insert(key); return; }
        chosen.insert(key);
        QJsonObject change{{"action", installed.contains(key) ? "replace" : "install"}, {"projectId", project}, {"versionId", version}};
        if (provider != "modrinth") change["provider"] = provider;
        if (!artifactHash.isEmpty()) change["artifactSha512"] = artifactHash;
        changes.append(change);
        if (changes.size() > 1024) throw ServiceError{"LIMIT_EXCEEDED", "Package exceeds 1,024 provider projects."};
    };
    QJsonArray packProjects; int ordinal = parsed["files"].toArray().size();
    for (const auto& value : parsed["projects"].toArray()) {
        m_provider.checkCancelled(); const auto project = value.toObject();
        if (!project["required"].toBool() && !includeOptional) { if (warnings.size() < 128) warnings.append("Skipped optional CurseForge project " + project["projectId"].toString()); continue; }
        if (!m_curseforge.available()) throw ServiceError{"PROVIDER_RESTRICTED", "CurseForge import requires separately authorized API access; no project key is distributed."};
        packProjects.append(project);
    }
    for (const auto& entry : m_curseforge.resolvePack(packProjects)) {
        if (entry.blocked) { if (warnings.size() < 128) warnings.append("Manual download required: " + entry.name + (entry.fileName.isEmpty() ? QString{} : " (" + entry.fileName.left(200) + ")")); continue; }
        if (entry.mod) { select("curseforge", entry.projectId, entry.fileId); continue; }
        // Resource packs, shaders and data packs in a CurseForge pack are verified files in their own folders.
        const auto path = entry.folder + '/' + entry.fileName;
        if (!gamePath(path) || destinations.contains(path.toCaseFolded())) continue;
        destinations.insert(path.toCaseFolded());
        const auto name = "download-" + QString::number(++ordinal);
        m_curseforge.download(entry.artifact, directory + '/' + name);
        files.append(QJsonObject{{"path", ".minecraft/" + path}, {"stagedName", name}, {"sha256", Awake::Integration::DurableIO::hash(directory + '/' + name)}});
    }
    QStringList jarHashes;
    for (const auto& value : parsed["downloads"].toArray()) {
        const auto file = value.toObject(); const auto path = file["path"].toString();
        if (path.startsWith("mods/") && path.count('/') == 1 && path.endsWith(".jar", Qt::CaseInsensitive)) jarHashes.append(file["hashes"].toObject()["sha512"].toString());
    }
    QHash<QString, QJsonObject> primed;
    if (authoritative) {
        try {
            primed = m_provider.primeByHashes(jarHashes); QStringList projects;
            for (const auto& raw : primed) projects.append(raw["project_id"].toString());
            projects.removeDuplicates(); m_provider.primeProjects(projects);
        } catch (const ServiceError& error) { if (error.code == "CANCELLED") throw; }
    }
    const auto downloadable = [&](const QJsonArray& urls) {
        for (const auto& candidate : urls) {
            const auto host = QUrl(candidate.toString()).host();
            if (host == "cdn.modrinth.com" || (authoritative && QStringList{"github.com", "raw.githubusercontent.com", "gitlab.com", "edge.forgecdn.net", "mediafilez.forgecdn.net", "files.feed-the-beast.com", "cdn.feed-the-beast.com"}.contains(host))) return candidate.toString();
        }
        return QString{};
    };
    struct LooseFile { QString path, name; QJsonObject artifact; }; QList<LooseFile> loose;
    const auto stageDownload = [&](const QString& path, const QString& url, const QJsonObject& file) {
        const auto hashes = file["hashes"].toObject();
        loose.append({path, "download-" + QString::number(++ordinal), {{"url", url}, {"size", QString::number(qint64(file["fileSize"].toDouble()))}, {"sha512", hashes["sha512"]}, {"sha1", hashes["sha1"]}}});
    };
    for (const auto& value : parsed["downloads"].toArray()) {
        m_provider.checkCancelled(); const auto file = value.toObject(); const auto path = file["path"].toString();
        if (file["env"].toObject()["client"] == "unsupported") continue;
        if (file["env"].toObject()["client"] == "optional" && !includeOptional) { if (warnings.size() < 128) warnings.append("Skipped optional file " + path.left(900)); continue; }
        if (!gamePath(path) || destinations.contains(path.toCaseFolded())) throw ServiceError{"POLICY_CONFLICT", "Package contains conflicting or reserved file destinations."};
        destinations.insert(path.toCaseFolded()); const auto url = downloadable(file["downloads"].toArray());
        if (url.isEmpty()) throw ServiceError{"PROVIDER_RESTRICTED", "Manual distribution is required for " + path.left(1500) + "; no bytes were applied."};
        const auto hashes = file["hashes"].toObject();
        if (path.startsWith("mods/") && path.count('/') == 1 && path.endsWith(".jar", Qt::CaseInsensitive)) {
            // A published pack may list jars Modrinth does not index, or two files of one project: those stay verified files.
            if (authoritative && (!primed.contains(hashes["sha512"].toString()) || chosen.contains("modrinth:" + primed[hashes["sha512"].toString()]["project_id"].toString()))) { stageDownload(path, url, file); continue; }
            // A Fabric-only file in a Forge/NeoForge pack is admitted only for this exact hash and only if the
            // resulting workspace enables Sinytra Connector (enforced by prepare's graph check).
            const auto resolved = m_provider.versionByHash(hashes["sha512"].toString(), record["minecraftVersion"].toString(), record["loader"].toObject()["id"].toString(), true);
            const auto artifact = resolved.wire["files"].toArray().first().toObject();
            if (artifact["sha512"] != hashes["sha512"] || artifact["sha1"] != hashes["sha1"] || artifact["size"].toString() != QString::number(qint64(file["fileSize"].toDouble())) || artifact["fileName"] != path.mid(5)) {
                // Providers record the uploaded name; a pack may install the same verified bytes under another name.
                if (authoritative && artifact["sha512"] == hashes["sha512"]) { stageDownload(path, url, file); continue; }
                throw ServiceError{"INTEGRITY_FAILED", "Package file differs from its compatible provider version."};
            }
            const auto before = changes.size();
            select("modrinth", resolved.wire["projectId"].toString(), resolved.wire["versionId"].toString(), hashes["sha512"].toString());
            if (!resolved.bridge.isEmpty() && changes.size() > before) { auto change = changes.last().toObject(); change["loaderBridge"] = resolved.bridge; changes.replace(changes.size() - 1, change); }
        } else stageDownload(path, url, file);
    }
    if (m_provider.hasFixture()) {
        for (const auto& item : loose) { m_provider.download(item.artifact, directory + '/' + item.name); files.append(QJsonObject{{"path", ".minecraft/" + item.path}, {"stagedName", item.name}, {"sha256", Awake::Integration::DurableIO::hash(directory + '/' + item.name)}}); }
    } else if (!loose.isEmpty()) {
        QThreadPool pool; pool.setMaxThreadCount(8); QMutex lock; std::optional<ServiceError> failure; std::atomic_int finished{0};
        for (const auto& item : loose) pool.start(QRunnable::create([&, item] {
            { QMutexLocker guard(&lock); if (failure) return; }
            try { ModrinthService worker(m_provider.cancelToken()); worker.setPackAuthoritative(authoritative); worker.download(item.artifact, directory + '/' + item.name); ++finished; }
            catch (const ServiceError& error) { QMutexLocker guard(&lock); if (!failure) failure = error; }
        }));
        while (!pool.waitForDone(250)) requireResult(m_store.note(id, {{"phase", "downloading"}, {"completed", QString::number(finished.load())}, {"total", QString::number(loose.size())}, {"unit", "files"}}));
        if (failure) throw *failure;
        for (const auto& item : loose) files.append(QJsonObject{{"path", ".minecraft/" + item.path}, {"stagedName", item.name}, {"sha256", Awake::Integration::DurableIO::hash(directory + '/' + item.name)}});
    }
    // A pack update removes what the previous version installed and this one no longer lists.
    for (const auto& value : request["removeInventoryIds"].toArray()) {
        for (const auto& existing : inventory) {
            const auto row = existing.toObject();
            if (row["inventoryId"] != value) continue;
            const auto key = row["provider"].toString("modrinth") + ':' + row["projectId"].toString();
            if (!chosen.contains(key) && !destinations.contains(("mods/" + row["fileName"].toString()).toCaseFolded())) changes.append(QJsonObject{{"action", "remove"}, {"inventoryId", value}});
        }
    }
    auto parameters = request; parameters["changes"] = changes;
    if (loaderBump) parameters["newLoaderVersion"] = parsed["loader"].toObject()["version"];
    prepare(id, parameters, files, warnings);
}
}
