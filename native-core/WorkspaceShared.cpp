// SPDX-License-Identifier: GPL-3.0-only
// Launcher instances the user chose to manage here. The files stay in the Launcher's own folder; the
// helper only coordinates them, and stopping removes every trace of that coordination.
#include "WorkspaceServices.h"
#include "Contract.h"
#include "awake/integration/DurableIO.h"
#include "awake/integration/InstanceAuthority.h"
#include "awake/integration/InstanceDiscovery.h"
#include "awake/integration/ProfileAccess.h"
#include "awake/integration/TransactionEngine.h"
#include "modplatform/packwiz/Packwiz.h"
#include "settings/INIFile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
namespace Awake::Core {
using namespace Awake::Integration;
namespace {
constexpr qint64 metadataLimit = 8 * 1024 * 1024;
QJsonObject jsonFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > metadataLimit) throw ServiceError{"INVALID_REQUEST", "This instance's metadata cannot be read."};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QString gameDirectory(const QString& root)
{
    // The Launcher's own rule: "minecraft" is used only when no ".minecraft" exists.
    return QFileInfo(root + "/minecraft").isDir() && !QFileInfo(root + "/.minecraft").exists() ? QStringLiteral("minecraft") : QStringLiteral(".minecraft");
}
void describe(const QString& root, QJsonObject& record)
{
    QString game; QJsonObject loader{{"id", "vanilla"}, {"version", ""}};
    static const QMap<QString, QString> loaders{{"net.minecraftforge", "forge"}, {"net.neoforged", "neoforge"}, {"net.fabricmc.fabric-loader", "fabric"}, {"org.quiltmc.quilt-loader", "quilt"}};
    for (const auto& value : jsonFile(root + "/mmc-pack.json")["components"].toArray()) {
        const auto component = value.toObject(); const auto uid = component["uid"].toString();
        const auto version = component["version"].toString(component["cachedVersion"].toString());
        if (uid == "net.minecraft") game = version;
        else if (loaders.contains(uid)) loader = {{"id", loaders[uid]}, {"version", version}};
    }
    if (game.isEmpty() || (loader["id"] != "vanilla" && loader["version"].toString().isEmpty()))
        throw ServiceError{"INVALID_REQUEST", "This instance does not name its Minecraft and loader versions."};
    INIFile config; QString name;
    if (config.loadFile(root + "/instance.cfg")) name = config.get("name", QString{}).toString().trimmed();
    record["name"] = (name.isEmpty() ? QFileInfo(root).fileName() : name).left(80);
    record["minecraftVersion"] = game; record["loader"] = loader;
}
/** Already-installed provider mods become tracked entries, but only when the file still matches its metadata. */
QJsonObject importTracked(const QString& mods, const QJsonObject& record)
{
    static const QRegularExpression identity("^[A-Za-z0-9_-]{1,128}$");
    QJsonObject projects;
    for (const auto& index : QDir(mods + "/.index").entryInfoList({"*.pw.toml"}, QDir::Files | QDir::Hidden, QDir::Name)) {
        if (index.isSymLink() || index.size() > 4 * 1024 * 1024) continue;
        QFile file(index.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto native = Packwiz::V1::parseModIndex(file.readAll(), index.fileName()); file.close();
        const bool modrinth = native.provider == ModPlatform::ResourceProvider::MODRINTH, curseforge = native.provider == ModPlatform::ResourceProvider::FLAME;
        const auto project = native.projectId.toString(), version = native.fileId.toString(), name = native.filename;
        if (!native.isValid() || (!modrinth && !curseforge) || native.hashFormat != (curseforge ? "sha1" : "sha512") || QFileInfo(name).fileName() != name ||
            name.contains('\\') || !name.endsWith(".jar", Qt::CaseInsensitive) || !identity.match(project).hasMatch() || !identity.match(version).hasMatch()) continue;
        const auto key = curseforge ? "curseforge:" + project : project;
        if (projects.contains(key)) continue;
        auto jar = mods + '/' + name; if (!QFileInfo::exists(jar)) jar += ".disabled";
        const QFileInfo info(jar);
        if (!info.isFile() || info.isSymLink()) continue;
        const auto expected = DurableIO::hash(jar, nullptr, curseforge ? QCryptographicHash::Sha1 : QCryptographicHash::Sha512);
        if (expected.isEmpty() || expected != native.hash) continue;
        const auto url = native.url.toString().isEmpty() ? QStringLiteral("https://www.curseforge.com/") : native.url.toString(); // never fetched for an installed file
        QJsonObject artifact{{"fileName", name}, {"size", QString::number(info.size())}, {"sha1", DurableIO::hash(jar, nullptr, QCryptographicHash::Sha1)}, {"url", url.left(2048)}};
        if (modrinth) artifact["sha512"] = expected;
        const auto number = (native.versionNumber.isEmpty() ? version : native.versionNumber).left(256);
        QJsonObject wire{{"projectId", project}, {"versionId", version}, {"versionNumber", number}, {"name", (native.name.isEmpty() ? number : native.name).left(256)},
            {"releaseType", "release"}, {"gameVersions", QJsonArray{record["minecraftVersion"]}}, {"loaders", QJsonArray{record["loader"].toObject().value("id")}},
            {"files", QJsonArray{artifact}}, {"dependencies", QJsonArray{}}};
        if (curseforge) wire["provider"] = "curseforge";
        if (!Contract().valid("version", wire)) continue;
        QJsonObject entry{{"projectId", project}, {"versionId", version}, {"versionNumber", number}, {"fileName", name}, {"sha256", DurableIO::hash(jar)},
            {"indexFile", index.fileName()}, {"indexSha256", DurableIO::hash(index.absoluteFilePath())}, {"updateLocked", native.lockUpdate}, {"version", wire}, {"enabled", !jar.endsWith(".disabled")}};
        if (curseforge) entry["provider"] = "curseforge";
        projects[key] = entry;
    }
    return projects;
}
}
QString modsPath(const QJsonObject& record) { return record["modsPath"].toString(QStringLiteral(".minecraft/mods")); }
void WorkspaceServices::adopt(const QString& id, const QJsonObject& request)
{
    requireResult(m_store.transition(id, "resolving"));
    const auto root = QFileInfo(request["path"].toString()).canonicalFilePath();
    if (root.isEmpty() || !QDir::isAbsolutePath(request["path"].toString()) || !QFileInfo(root + "/instance.cfg").isFile() || !QFileInfo(root + "/mmc-pack.json").isFile())
        throw ServiceError{"INVALID_REQUEST", "This folder is not an Awake Launcher instance."};
    if (root == m_profile || root.startsWith(m_profile + '/')) throw ServiceError{"INVALID_REQUEST", "Manager projects are already managed here."};
    const auto physical = physicalIdentityV2(root);
    if (physical.isEmpty()) throw ServiceError{"IDENTITY_UNVERIFIABLE", "This instance's folder cannot be identified."};
    for (const auto& file : QDir(m_profile + "/catalog").entryInfoList({"*.json"}, QDir::Files | QDir::Hidden | QDir::System))
        if (readRecord(file.absoluteFilePath())["physicalId"] == physical) throw ServiceError{"POLICY_CONFLICT", "This instance is already managed here."};
    if (QFileInfo::exists(root + "/.awake-integration") && !QDir(root + "/.awake-integration").isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot) && !InstanceAuthority::clearAbandoned(root))
        throw ServiceError{"POLICY_CONFLICT", "Another program coordinates this instance. Its files were left alone."};
    QJsonObject record{{"format", 1}, {"workspaceId", id}, {"physicalId", physical}, {"sharedRoot", root}, {"modsPath", gameDirectory(root) + "/mods"}};
    describe(root, record);
    if (!QDir().mkpath(root + '/' + modsPath(record))) throw ServiceError{"IO_ERROR", "The instance's mods folder cannot be created."};
    QString error;
    if (!profileAccessIsPrivate(root)) throw ServiceError{"IDENTITY_UNVERIFIABLE", "Other accounts on this PC can change this folder. Make it private in the Manager first."};
    if (!InstanceAuthority::enroll(root, physical, m_profile + "/ownership", physicalIdentityV2(m_profile), id, &error))
        throw ServiceError{"IDENTITY_UNVERIFIABLE", "This instance could not be connected: " + error};
    try {
        auto lease = acquire(record);
        const auto policies = QJsonDocument(QJsonObject{{"format", 1}, {"projects", importTracked(root + '/' + modsPath(record), record)}}).toJson(QJsonDocument::Compact);
        if (!DurableIO::write(root + "/.awake-integration/policies.json", policies, &error)) throw ServiceError{"IO_ERROR", error};
        requireResult(m_store.transition(id, "verifying"));
        const auto view = workspaceView(record); scan(record);
        writeRecord(m_profile + "/catalog/" + id + ".json", record);
        requireResult(m_store.transition(id, "completed", {{"workspace", view}, {"verified", true}}));
    } catch (...) {
        InstanceAuthority::unenroll(root, physical, id);
        throw;
    }
}
void WorkspaceServices::undoAdopt(const QString& id, const QJsonObject& request)
{
    // A helper that died mid-connect must not leave the instance hidden from the Launcher.
    const auto root = QFileInfo(request["path"].toString()).canonicalFilePath();
    if (root.isEmpty() || QFileInfo::exists(m_profile + "/catalog/" + id + ".json")) return;
    InstanceAuthority::unenroll(root, physicalIdentityV2(root), id);
}
QJsonObject WorkspaceServices::release(const QJsonObject& request)
{
    const auto record = workspaceRecord(request["physicalId"].toString());
    if (!record.contains("sharedRoot")) throw ServiceError{"UNSUPPORTED_CAPABILITY", "Only Launcher instances can stop being managed."};
    const auto workspaceId = record["workspaceId"].toString(), folder = root(record);
    for (const auto& value : m_store.all()) {
        const auto status = value.toObject();
        if (status["terminal"].toBool()) continue;
        try { if (context(status["operationId"].toString())["request"].toObject()["physicalId"] == record["physicalId"]) throw ServiceError{"BUSY", "Finish or cancel this instance's pending changes first.", true}; }
        catch (const ServiceError& error) { if (error.code == "BUSY") throw; }
    }
    { // Proves no game or change is using the folder; the lock must be free again before it can be removed.
        auto lease = acquire(record);
        if (QFileInfo::exists(folder + "/.awake-integration/recovery.json")) throw ServiceError{"RECOVERY_REQUIRED", "Finish recovering this instance first."};
    }
    QString error;
    if (!InstanceAuthority::unenroll(folder, record["physicalId"].toString(), workspaceId, &error)) throw ServiceError{"IO_ERROR", "This instance could not be disconnected: " + error, true};
    QFile::remove(m_profile + "/catalog/" + workspaceId + ".json");
    return {{"released", true}};
}
void WorkspaceServices::sweepShared()
{
    // The folder was removed outside the Manager, or a stop was interrupted after the instance was freed.
    for (const auto& file : QDir(m_profile + "/catalog").entryInfoList({"*.json"}, QDir::Files | QDir::Hidden | QDir::System)) {
        try {
            const auto record = readRecord(file.absoluteFilePath());
            if (record.contains("sharedRoot") && !QFileInfo::exists(root(record) + "/.awake-integration/ownership.json")) QFile::remove(file.absoluteFilePath());
        } catch (const ServiceError&) { /* reported by the normal listing */ }
    }
}
}
