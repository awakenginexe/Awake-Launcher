// SPDX-License-Identifier: GPL-3.0-only
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
namespace Awake::Core {
using namespace Awake::Integration;
namespace {
constexpr auto mods = ".minecraft/mods";
QByteArray encode(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
QString sha(const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
bool uuid(const QString& value) { return !QUuid(value).isNull() && QUuid(value).toString(QUuid::WithoutBraces) == value; }
bool leaf(const QString& name)
{
    static const QRegularExpression invalid("[<>:\"/\\\\|?*\\x00-\\x1f]"), device("^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption);
    return !name.isEmpty() && name.size() <= 255 && !name.endsWith('.') && !name.endsWith(' ') && name != "." && name != ".." &&
        !invalid.match(name).hasMatch() && !device.match(name).hasMatch();
}
QByteArray bytes(const QString& path, qint64 limit = 8 * 1024 * 1024)
{
    QString error;
    if (DurableIO::hash(path, &error).isEmpty() || !storageAccessIsPrivate(path)) throw ServiceError{"RECOVERY_REQUIRED", "Storage identity is unverifiable: " + error};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) throw ServiceError{"IO_ERROR", "Cannot read bounded native storage."};
    const auto data = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || data.size() > limit) throw ServiceError{"IO_ERROR", "Native storage read failed."};
    return data;
}
QJsonObject paged(const QString& key, const QJsonArray& all, const QJsonObject& params)
{
    const int offset = params["offset"].toInt(), limit = params["limit"].toInt(100); QJsonArray values;
    for (int i = offset; i < all.size() && i < offset + limit; ++i) values.append(all[i]);
    return {{key, values}, {"total", all.size()}, {"nextOffset", offset + values.size() < all.size() ? QJsonValue(offset + values.size()) : QJsonValue(QJsonValue::Null)}};
}
QJsonObject errorBody(const QString& code, const QString& message) { return {{"code", code}, {"message", message.left(2048)}, {"retryable", false}}; }
void directory(const QString& path)
{ if (!createPrivateStorageDirectory(path)) throw ServiceError{"IO_ERROR", "Private storage directory is unavailable."}; }
void put(const QString& path, const QByteArray& data)
{ QString error; if (!DurableIO::write(path, data, &error)) throw ServiceError{"IO_ERROR", error}; }
QString hash(const QString& path)
{ QString error; const auto value = DurableIO::hash(path, &error); if (value.isEmpty()) throw ServiceError{"INTEGRITY_FAILED", error}; return value; }
}
QJsonObject readRecord(const QString& path)
{
    const auto envelope = QJsonDocument::fromJson(bytes(path)).object(), body = envelope["body"].toObject();
    if (envelope.size() != 2 || body.isEmpty() || envelope["sha256"] != sha(encode(body))) throw ServiceError{"RECOVERY_REQUIRED", "A durable record is corrupt."};
    return body;
}
void writeRecord(const QString& path, const QJsonObject& value)
{ const auto data = encode({{"body", value}, {"sha256", sha(encode(value))}}); if (data.size() > 8 * 1024 * 1024) throw ServiceError{"LIMIT_EXCEEDED", "Durable record exceeds 8 MiB."}; put(path, data); }
void requireResult(const QJsonObject& response)
{ if (response.contains("error")) { const auto error = response["error"].toObject(); throw ServiceError{error["code"].toString(), error["message"].toString(), error["retryable"].toBool()}; } }
WorkspaceServices::WorkspaceServices(QString profile, OperationStore& store, std::shared_ptr<std::atomic_bool> cancel, ModrinthService::Fetch fixture)
    : m_profile(std::move(profile)), m_store(store), m_provider(cancel, fixture), m_curseforge(std::move(cancel), std::move(fixture)) {}
QString WorkspaceServices::operationDirectory(const QString& id) const
{ if (!uuid(id)) throw ServiceError{"INVALID_REQUEST", "Invalid operation identity."}; return m_store.directory() + '/' + id; }
void WorkspaceServices::saveContext(const QString& id, const QString& method, const QJsonObject& request)
{
    directory(operationDirectory(id));
    writeRecord(operationDirectory(id) + "/context.json", {{"format", 1}, {"method", method}, {"request", request}});
}
QJsonObject WorkspaceServices::context(const QString& id) const
{
    const auto value = readRecord(operationDirectory(id) + "/context.json");
    if (value["format"] != 1 || !Contract().valid(value["method"].toString() + ".params", value["request"]))
        throw ServiceError{"RECOVERY_REQUIRED", "Operation context is incompatible."};
    return value;
}
QString WorkspaceServices::root(const QJsonObject& record) const
{
    const auto id = record["workspaceId"].toString();
    if (!uuid(id)) throw ServiceError{"RECOVERY_REQUIRED", "Workspace registration is invalid."};
    return m_profile + "/workspaces/" + id;
}
std::shared_ptr<PhysicalInstanceLease> WorkspaceServices::acquire(const QJsonObject& record) const
{
    QString error; auto lease = PhysicalInstanceLease::acquire(root(record), record["physicalId"].toString(), m_profile + "/ownership", &error);
    if (!lease) throw ServiceError{"BUSY", error, true};
    return lease;
}
QJsonObject WorkspaceServices::workspaceRecord(const QString& id) const
{
    for (const auto& file : QDir(m_profile + "/catalog").entryInfoList({"*.json"}, QDir::Files | QDir::Hidden | QDir::System)) {
        const auto record = readRecord(file.absoluteFilePath());
        if (record["format"] != 1 || record["workspaceId"] != file.completeBaseName()) throw ServiceError{"RECOVERY_REQUIRED", "Workspace catalog is corrupt."};
        if (record["physicalId"] != id) continue;
        if (!InstanceAuthority::validate(root(record), id, m_profile + "/ownership")) throw ServiceError{"INCOMPATIBLE_OWNERSHIP", "Workspace ownership is unknown or incompatible."};
        return record;
    }
    throw ServiceError{"NOT_FOUND", "No enrolled Manager-owned workspace has this physical identity."};
}
QJsonObject WorkspaceServices::workspaceView(const QJsonObject& record) const
{
    TransactionEngine engine(m_store, root(record), mods); const auto revision = engine.revision();
    if (revision.isEmpty()) throw ServiceError{"RECOVERY_REQUIRED", "Workspace inventory cannot be verified."};
    QJsonObject value;
    for (const auto* key : {"workspaceId", "physicalId", "name", "minecraftVersion", "loader"}) value[key] = record[key];
    value["instanceRevision"] = revision; value["ownership"] = "manager-owned";
    value["recoveryRequired"] = QFileInfo::exists(root(record) + "/.awake-integration/recovery.json");
    if (!Contract().valid("workspace", value)) throw ServiceError{"RECOVERY_REQUIRED", "Workspace metadata is invalid."};
    return value;
}
QJsonObject WorkspaceServices::policies(const QJsonObject& record) const
{
    const auto value = QJsonDocument::fromJson(bytes(root(record) + "/.awake-integration/policies.json")).object();
    if (value["format"] != 1 || !value["projects"].isObject() || value["projects"].toObject().size() > 10000)
        throw ServiceError{"POLICY_CONFLICT", "Workspace policies are invalid; existing data was preserved."};
    return value;
}
QJsonArray WorkspaceServices::scan(const QJsonObject& record) const
{
    const auto projects = policies(record)["projects"].toObject(); QMap<QString, QPair<QString, QJsonObject>> metadata;
    for (auto it = projects.begin(); it != projects.end(); ++it) {
        const auto entry = it.value().toObject(); const auto filename = entry["fileName"].toString(), index = entry["indexFile"].toString();
        const auto version = entry["version"].toObject();
        if (!leaf(filename) || !leaf(index) || !filename.endsWith(".jar", Qt::CaseInsensitive) || !index.endsWith(".pw.toml") ||
            (entry["provider"].toString("modrinth") == "curseforge" ? "curseforge:" + entry["projectId"].toString() : entry["projectId"].toString()) != it.key() || !Contract().valid("version", version) || version["projectId"] != entry["projectId"] ||
            version["versionId"] != entry["versionId"] || version["versionNumber"] != entry["versionNumber"] || version["files"].toArray().size() != 1 ||
            version["files"].toArray().first().toObject()["fileName"] != filename)
            throw ServiceError{"POLICY_CONFLICT", "An installed policy entry is invalid."};
        const auto key = filename.toCaseFolded();
        if (metadata.contains(key)) throw ServiceError{"POLICY_CONFLICT", "Installed policy filenames collide."};
        metadata[key] = {it.key(), entry};
    }
    QJsonArray result; QSet<QString> found;
    for (const auto& file : QDir(root(record) + '/' + mods).entryInfoList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name)) {
        auto base = file.fileName(); const bool enabled = !base.endsWith(".disabled", Qt::CaseInsensitive);
        if (!enabled) base.chop(9);
        if (!base.endsWith(".jar", Qt::CaseInsensitive)) continue;
        const auto digest = hash(file.absoluteFilePath()); const auto key = base.toCaseFolded();
        if (found.contains(key)) throw ServiceError{"POLICY_CONFLICT", "Both enabled and disabled copies of a mod exist."}; found.insert(key);
        const auto entry = metadata.value(key).second; QString state = "unknown"; bool nativeLock = false, locked = false;
        if (!entry.isEmpty()) {
            const auto indexPath = root(record) + '/' + mods + "/.index/" + entry["indexFile"].toString();
            const auto indexHash = hash(indexPath);
            const auto native = indexHash == "missing" ? Packwiz::V1::Mod{} : Packwiz::V1::parseModIndex(bytes(indexPath, 4 * 1024 * 1024), entry["indexFile"].toString());
            const auto artifact = entry["version"].toObject()["files"].toArray().first().toObject();
            state = digest == entry["sha256"] && indexHash == entry["indexSha256"] && native.isValid() &&
                native.projectId.toString() == entry["projectId"].toString() && native.fileId.toString() == entry["versionId"].toString() &&
                native.filename == entry["fileName"] && native.provider == (entry["provider"] == "curseforge" ? ModPlatform::ResourceProvider::FLAME : ModPlatform::ResourceProvider::MODRINTH) &&
                native.hashFormat == (entry["provider"] == "curseforge" ? "sha1" : "sha512") && native.hash == artifact[native.hashFormat] &&
                DurableIO::hash(file.absoluteFilePath(), nullptr, entry["provider"] == "curseforge" ? QCryptographicHash::Sha1 : QCryptographicHash::Sha512) == artifact[native.hashFormat] ? "verified" : "modified";
            nativeLock = native.lockUpdate || entry["updateLocked"].toBool();
            locked = nativeLock || entry["excluded"].toBool() || !entry["pinnedVersion"].toString().isEmpty();
        }
        const auto nullable = [&](const char* field) { return entry.isEmpty() ? QJsonValue(QJsonValue::Null) : entry[field]; };
        result.append(QJsonObject{{"inventoryId", sha(file.fileName().toUtf8())}, {"fileName", file.fileName()}, {"enabled", enabled},
            {"size", QString::number(file.size())}, {"sha256", digest}, {"projectId", nullable("projectId")}, {"versionId", nullable("versionId")},
            {"versionNumber", nullable("versionNumber")}, {"metadataState", state}, {"updateLocked", locked}, {"nativeLock", nativeLock},
            {"policy", QJsonObject{{"pinnedVersion", entry["pinnedVersion"].toString().isEmpty() ? QJsonValue(QJsonValue::Null) : entry["pinnedVersion"]}, {"excluded", entry["excluded"].toBool()}}}});
        if (entry["provider"] == "curseforge") { auto row = result.last().toObject(); row["provider"] = "curseforge"; result.replace(result.size() - 1, row); }
    }
    if (result.size() > 100000) throw ServiceError{"LIMIT_EXCEEDED", "Inventory exceeds 100,000 mod files."};
    return result;
}
QJsonObject WorkspaceServices::list(const QJsonObject& params)
{
    QJsonArray result;
    for (const auto& file : QDir(m_profile + "/catalog").entryInfoList({"*.json"}, QDir::Files | QDir::Hidden | QDir::System, QDir::Name)) {
        const auto record = readRecord(file.absoluteFilePath());
        workspaceRecord(record["physicalId"].toString()); auto lease = acquire(record); result.append(workspaceView(record));
    }
    return paged("workspaces", result, params);
}
QJsonObject WorkspaceServices::inventory(const QJsonObject& params)
{
    const auto record = workspaceRecord(params["physicalId"].toString()); auto lease = acquire(record);
    TransactionEngine engine(m_store, root(record), mods); const auto before = engine.revision();
    if (before.isEmpty()) throw ServiceError{"RECOVERY_REQUIRED", "Inventory cannot be verified."};
    if (params.contains("instanceRevision") && params["instanceRevision"] != before) throw ServiceError{"STALE_INSTANCE", "Inventory changed between pages."};
    auto result = paged("items", scan(record), params);
    if (engine.revision() != before) throw ServiceError{"STALE_INSTANCE", "Inventory changed while reading."};
    result["instanceRevision"] = before; return result;
}
void WorkspaceServices::create(const QString& id, const QJsonObject& request)
{
    requireResult(m_store.transition(id, "resolving")); m_provider.checkCancelled();
    const auto loader = request["loader"].toObject();
    const auto catalog = m_provider.catalog({{"minecraftVersion", request["minecraftVersion"]}, {"loader", loader["id"]}});
    const auto loaderId = loader["id"].toString();
    if ((loaderId == "vanilla" && !loader["version"].toString().isEmpty()) ||
        (loaderId != "vanilla" && (!QStringList{"fabric", "forge", "neoforge", "quilt"}.contains(loaderId) ||
         !catalog["loaderVersions"].toArray().contains(loader["version"]))))
        throw ServiceError{"INVALID_REQUEST", "Unsupported Minecraft/loader combination."};
    m_provider.checkCancelled();
    const auto workspaceRoot = m_profile + "/workspaces/" + id;
    if (QFileInfo::exists(workspaceRoot)) throw ServiceError{"RECOVERY_REQUIRED", "Creation destination already exists."};
    directory(workspaceRoot); QString error; const auto physical = physicalIdentityV2(workspaceRoot);
    if (!InstanceAuthority::enroll(workspaceRoot, physical, m_profile + "/ownership", physicalIdentityV2(m_profile), id, &error))
        throw ServiceError{"RECOVERY_REQUIRED", "Workspace enrollment failed: " + error};
    const QJsonObject record{{"format", 1}, {"workspaceId", id}, {"physicalId", physical}, {"name", request["name"]},
        {"minecraftVersion", request["minecraftVersion"]}, {"loader", loader}};
    auto lease = acquire(record); directory(workspaceRoot + "/.minecraft"); directory(workspaceRoot + '/' + mods); directory(workspaceRoot + '/' + mods + "/.index");
    directory(operationDirectory(id) + "/staging");
    INIFile config; config["InstanceType"] = "OneSix"; config["name"] = request["name"].toString(); config["iconKey"] = "default"; config["uuid"] = id;
    const auto staged = operationDirectory(id) + "/staging/instance.cfg";
    if (!config.saveFile(staged) || !DurableIO::copy(staged, workspaceRoot + "/instance.cfg", &error)) throw ServiceError{"RECOVERY_REQUIRED", "Native configuration initialization failed: " + error};
    QJsonArray components{QJsonObject{{"uid", "net.minecraft"}, {"version", request["minecraftVersion"]}, {"important", true}}};
    if (loaderId == "fabric" || loaderId == "quilt")
        components.append(QJsonObject{{"uid", "net.fabricmc.intermediary"}, {"version", request["minecraftVersion"]}});
    const QMap<QString, QString> componentIds{{"fabric", "net.fabricmc.fabric-loader"}, {"forge", "net.minecraftforge"},
        {"neoforge", "net.neoforged"}, {"quilt", "org.quiltmc.quilt-loader"}};
    if (componentIds.contains(loaderId))
        components.append(QJsonObject{{"uid", componentIds.value(loaderId)}, {"version", loader["version"]}});
    put(workspaceRoot + "/mmc-pack.json", encode({{"formatVersion", 1}, {"components", components}}));
    put(workspaceRoot + "/.awake-integration/policies.json", encode({{"format", 1}, {"projects", QJsonObject{}}}));
    writeRecord(operationDirectory(id) + "/created.json", {{"workspace", record}, {"instance.cfg", hash(workspaceRoot + "/instance.cfg")},
        {"mmc-pack.json", hash(workspaceRoot + "/mmc-pack.json")}, {"policies.json", hash(workspaceRoot + "/.awake-integration/policies.json")}});
    requireResult(m_store.transition(id, "verifying"));
    const auto view = workspaceView(record); scan(record);
    writeRecord(m_profile + "/catalog/" + id + ".json", record);
    requireResult(m_store.transition(id, "completed", {{"workspace", view}, {"verified", true}}));
}
namespace {
bool exportableConfig(const QString& path)
{
    if (!path.startsWith(".minecraft/config/") || !QStringList{"json", "toml", "cfg", "conf", "ini", "properties", "txt", "yaml", "yml"}.contains(QFileInfo(path).suffix().toLower())) return false;
    for (const auto& part : path.split('/')) if (!leaf(part)) return false;
    return true;
}
}
QJsonObject WorkspaceServices::outboxList(const QJsonObject& params)
{
    if (!Contract().valid("outbox.list.params", params)) throw ServiceError{"INVALID_REQUEST", "Invalid outbox selection."};
    const auto record = workspaceRecord(params["physicalId"].toString()); auto lease = acquire(record);
    TransactionEngine engine(m_store, root(record), mods); const auto before = engine.revision();
    if (before.isEmpty()) throw ServiceError{"RECOVERY_REQUIRED", "Configuration paths cannot be verified."};
    if (params["instanceRevision"] != before) throw ServiceError{"STALE_INSTANCE", "Workspace changed before export."};
    if (QFileInfo::exists(root(record) + "/.awake-integration/recovery.json")) throw ServiceError{"RECOVERY_REQUIRED", "Resolve workspace recovery before export."};
    const auto tracked = policies(record)["overrides"].toObject(); QJsonArray files;
    for (auto it = tracked.begin(); it != tracked.end(); ++it) {
        if (!exportableConfig(it.key())) continue;
        const auto path = root(record) + '/' + it.key(); const QFileInfo info(path);
        if (hash(path) != it.value()) throw ServiceError{"POLICY_CONFLICT", "Export requires unchanged tracked configuration: " + it.key()};
        if (info.size() > 8 * 1024 * 1024) throw ServiceError{"LIMIT_EXCEEDED", "Configuration export is limited to 8 MiB per file."};
        files.append(QJsonObject{{"path", it.key().mid(11)}, {"size", QString::number(info.size())}, {"sha256", it.value()}});
    }
    if (engine.revision() != before) throw ServiceError{"STALE_INSTANCE", "Workspace changed during export enumeration."};
    auto result = paged("files", files, params); result["instanceRevision"] = before; return result;
}
QJsonObject WorkspaceServices::outboxRead(const QJsonObject& params)
{
    if (params.contains("token")) throw ServiceError{"UNSUPPORTED_CAPABILITY", "Token-based archive outbox is not implemented."};
    if (!Contract().valid("outbox.read.params", params)) throw ServiceError{"INVALID_REQUEST", "Invalid outbox read."};
    const auto record = workspaceRecord(params["physicalId"].toString()); auto lease = acquire(record);
    TransactionEngine engine(m_store, root(record), mods); const auto before = engine.revision();
    if (before.isEmpty()) throw ServiceError{"RECOVERY_REQUIRED", "Configuration paths cannot be verified."};
    if (params["instanceRevision"] != before) throw ServiceError{"STALE_INSTANCE", "Workspace changed before export."};
    if (QFileInfo::exists(root(record) + "/.awake-integration/recovery.json")) throw ServiceError{"RECOVERY_REQUIRED", "Resolve workspace recovery before export."};
    const auto relative = ".minecraft/" + params["path"].toString(); const auto tracked = policies(record)["overrides"].toObject();
    if (!exportableConfig(relative) || !tracked.contains(relative)) throw ServiceError{"NOT_FOUND", "Only tracked supported config overrides can be exported."};
    const auto data = bytes(root(record) + '/' + relative);
    if (sha(data) != tracked[relative]) throw ServiceError{"POLICY_CONFLICT", "Export requires unchanged tracked configuration."};
    bool valid = false; const auto offset = params["offset"].toString().toULongLong(&valid); const auto length = params["length"].toInt();
    if (!valid || offset > quint64(data.size())) throw ServiceError{"INVALID_REQUEST", "Outbox offset exceeds the file."};
    if (engine.revision() != before) throw ServiceError{"STALE_INSTANCE", "Workspace changed during export read."};
    const auto chunk = data.mid(qsizetype(offset), length);
    return {{"path", params["path"]}, {"instanceRevision", before}, {"offset", params["offset"]}, {"size", QString::number(data.size())},
        {"bytesBase64", QString::fromLatin1(chunk.toBase64())}, {"eof", offset + quint64(chunk.size()) == quint64(data.size())}, {"sha256", tracked[relative]}};
}
QJsonObject WorkspaceServices::plan(const QJsonObject& params)
{
    const auto id = params["operationId"].toString(); requireResult(m_store.get(id));
    const auto value = readRecord(operationDirectory(id) + "/plan.json");
    auto check = value; const auto digest = check.take("planDigest");
    if (digest != sha(encode(check)) || value["operationId"] != id) throw ServiceError{"RECOVERY_REQUIRED", "Reviewed plan is corrupt."};
    QJsonArray files;
    for (const auto& entry : value["files"].toArray()) { const auto file = entry.toObject(); files.append(QJsonObject{{"path", file["path"]},
        {"action", file["afterHash"] == "missing" ? "remove" : file["beforeHash"] == "missing" ? "install" : "replace"},
        {"kind", file["path"].toString().startsWith(".awake-integration/") || file["path"].toString().startsWith(".minecraft/mods/.index/") ? "bookkeeping" :
            file["path"].toString().startsWith(".minecraft/mods/") && (file["path"].toString().endsWith(".jar", Qt::CaseInsensitive) || file["path"].toString().endsWith(".jar.disabled", Qt::CaseInsensitive)) ? "mod" : "override"},
        {"beforeSha256", file["beforeHash"]}, {"afterSha256", file["afterHash"]}, {"size", file["sizeAfter"]}}); }
    auto result = paged("files", files, params);
    for (const auto* key : {"operationId", "planDigest", "instanceRevision", "expectedInstanceRevision"}) result[key] = value[key];
    result["warnings"] = value["review"].toObject()["warnings"].toArray(); result["dependencies"] = value["review"].toObject()["dependencies"].toArray();
    return result;
}
QJsonObject WorkspaceServices::operation(const QString& id) const
{
    const auto response = m_store.get(id); requireResult(response); const auto stored = response["result"].toObject(); QJsonObject result;
    for (const auto* key : {"operationId", "state", "terminal", "createdAt", "updatedAt", "sequence", "progress", "error"}) if (stored.contains(key)) result[key] = stored[key];
    result["kind"] = stored["method"] == "workspaces.create" ? "workspace-create" : "modpack-change";
    result["cancellationRequested"] = stored["cancellationRequested"].toBool();
    const auto internal = stored["result"].toObject(); QJsonObject completion;
    for (const auto* key : {"workspace", "planDigest", "instanceRevision", "expectedInstanceRevision", "fileCount", "verified"})
        if (internal.contains(key)) completion[key] = internal[key];
    if (stored["state"] == "completed") completion["verified"] = true;
    if (!completion.isEmpty()) result["result"] = completion;
    try { const auto request = context(id)["request"].toObject();
        if (request.contains("physicalId")) result["physicalId"] = request["physicalId"]; else if (internal.contains("workspace")) result["physicalId"] = internal["workspace"].toObject()["physicalId"];
    } catch (const ServiceError&) { /* A failed request can precede context publication. */ }
    return result;
}
bool WorkspaceServices::legacyCompatible(const QString& physicalId) const
{
    const auto record = workspaceRecord(physicalId);
    if (!QStringList{"vanilla", "fabric"}.contains(record["loader"].toObject()["id"].toString())) return false;
    const auto projects = policies(record)["projects"].toObject();
    for (auto it = projects.begin(); it != projects.end(); ++it) if (it.value().toObject()["provider"] == "curseforge") return false;
    return true;
}
bool WorkspaceServices::unsafeToExit() const
{
    for (const auto& value : m_store.all()) if (!value.toObject()["terminal"].toBool()) return true;
    for (const auto& entry : QDir(m_profile + "/workspaces").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        if (QFileInfo::exists(entry.absoluteFilePath() + "/.awake-integration/recovery.json")) return true;
    try {
        for (const auto& file : QDir(m_profile + "/catalog").entryInfoList({"*.json"}, QDir::Files | QDir::Hidden | QDir::System)) {
            const auto record = readRecord(file.absoluteFilePath());
            if (!InstanceAuthority::validate(root(record), record["physicalId"].toString(), m_profile + "/ownership")) return true;
        }
    } catch (const ServiceError&) { return true; }
    return false;
}
}
