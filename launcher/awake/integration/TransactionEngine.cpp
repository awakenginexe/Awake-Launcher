// SPDX-License-Identifier: GPL-3.0-only
#include "TransactionEngine.h"
#include "DurableIO.h"
#include "InstanceDiscovery.h"
#include "OperationStore.h"
#include "ProfileAccess.h"
#include "GameProcessGroup.h"
#include "Protocol.h"
#include "InstanceAuthority.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <qt_windows.h>
namespace Awake::Integration {
namespace {
QByteArray encode(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
QString digest(const QJsonObject& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(encode(value), QCryptographicHash::Sha256).toHex());
}
bool leaf(const QString& name)
{
    static const QRegularExpression invalid("[<>:\"/\\\\|?*\\x00-\\x1f]");
    static const QRegularExpression device(QRegularExpression::anchoredPattern("(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])"), QRegularExpression::CaseInsensitiveOption);
    return !name.isEmpty() && name.size() <= 255 && name != "." && name != ".." && !name.endsWith('.') && !name.endsWith(' ') &&
        !invalid.match(name).hasMatch() && !device.match(name.section('.', 0, 0)).hasMatch();
}
bool relative(const QString& path)
{
    if (QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path) return false;
    for (const auto& part : path.split('/')) if (!leaf(part)) return false;
    return true;
}
bool safePath(const QString& root, const QString& relativePath, QString* error)
{
    if (!relative(relativePath)) { if (error) *error = "Invalid relative target."; return false; }
    QString path = root;
    const auto parts = relativePath.split('/');
    for (qsizetype index = 0; index < parts.size(); ++index) {
        path = QDir(path).filePath(parts[index]);
        const auto native = "\\\\?\\" + QDir::toNativeSeparators(path);
        const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) {
                static const QRegularExpression shortAlias("^[^ .~]{1,6}~[0-9]+(?:\\.[^ .]{1,3})?$", QRegularExpression::CaseInsensitiveOption);
                if (parts[index].section('.', 0, 0).size() <= 8 && shortAlias.match(parts[index]).hasMatch()) {
                    if (error) *error = "New targets resembling Windows short aliases are unsupported.";
                    return false;
                }
                continue;
            }
            if (error) *error = "Target attributes cannot be read.";
            return false;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) || (index + 1 < parts.size() && !(attributes & FILE_ATTRIBUTE_DIRECTORY))) {
            if (error) *error = "Linked paths and non-directory ancestors are not writable targets.";
            return false;
        }
        const auto needed = GetLongPathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr, 0);
        if (!needed || needed > 32768) { if (error) *error = "Cannot resolve the target's Windows filename."; return false; }
        QString resolved; resolved.resize(int(needed));
        const auto count = GetLongPathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), reinterpret_cast<LPWSTR>(resolved.data()), needed);
        if (!count || count >= needed) return false;
        resolved.truncate(int(count));
        if (QFileInfo(QDir::fromNativeSeparators(resolved)).fileName() != parts[index]) {
            if (error) *error = "An aliased or differently cased Windows target must use its actual filename.";
            return false;
        }
    }
    return true;
}
bool allowedPath(const QString& path, const QString& mods)
{
    if (!relative(path)) return false;
    if (path == "instance.cfg" || path == "mmc-pack.json" || path == ".awake-integration/policies.json") return true;
    if (path.startsWith(".minecraft/") && !path.startsWith(mods + '/', Qt::CaseInsensitive)) return true;
    if (!path.startsWith(mods + '/')) return false;
    const auto rest = path.mid(mods.size() + 1);
    if (rest.startsWith(".index/")) return leaf(rest.mid(7)) && rest.endsWith(".pw.toml");
    // Forge also loads mods from versioned subfolders (mods/1.12.2/...); each part was checked above.
    return rest.contains('/') ? !rest.startsWith(".index/") : leaf(rest) && rest != ".index";
}
bool overridePaths(const QString& root, const QString& policy, const QString& mods, QSet<QString>& paths, QString* error)
{
    QFile file(policy);
    if (!QFileInfo::exists(policy)) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) { if (error) *error = "Cannot read bounded policies."; return false; }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) { if (error) *error = "Cannot read policies."; return false; }
    QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) return true;
    const auto value = doc.object().value("overrides");
    if (value.isUndefined()) return true;
    if (!value.isObject() || value.toObject().size() > 10000) { if (error) *error = "Invalid managed override list."; return false; }
    QSet<QString> folded;
    static const QRegularExpression hash("^[0-9a-f]{64}$");
    const auto overrides = value.toObject();
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        const auto path = it.key(), key = path.toCaseFolded();
        if (!path.startsWith(".minecraft/") || !allowedPath(path, mods) || path.startsWith(mods + "/.index/", Qt::CaseInsensitive) ||
            !it.value().isString() || !hash.match(it.value().toString()).hasMatch() || folded.contains(key) || !safePath(root, path, error)) {
            if (error && error->isEmpty()) *error = "Invalid managed override."; return false;
        }
        folded.insert(key); paths.insert(path);
    }
    return true;
}
bool pinParents(const QString& root, const QSet<QString>& paths, const std::shared_ptr<PhysicalInstanceLease>& lease, QString* error)
{
    for (const auto& path : paths) {
        QString parent = QFileInfo(path).path();
        while (parent != "." && !QFileInfo::exists(root + '/' + parent)) parent = QFileInfo(parent).path();
        if (!lease->pinDirectory(parent, error)) return false;
    }
    return true;
}
bool pinPlan(const QString& root, const QJsonObject& plan, const std::shared_ptr<PhysicalInstanceLease>& lease, QString* error)
{
    QSet<QString> paths;
    for (const auto& value : plan["files"].toArray()) paths.insert(value.toObject()["path"].toString());
    return pinParents(root, paths, lease, error);
}
bool protectedMetadata(const QString& path)
{
    return path == "instance.cfg" || path == "mmc-pack.json" || path == ".awake-integration/policies.json";
}
bool save(const QString& path, const QJsonObject& body, QString* error)
{
    const auto bytes = encode({{"body", body}, {"sha256", digest(body)}});
    if (bytes.size() > 8 * 1024 * 1024) { if (error) *error = "Transaction record exceeds 8 MiB."; return false; }
    return DurableIO::write(path, bytes, error);
}
QJsonObject load(const QString& path, QString* error)
{
    if (DurableIO::hash(path, error).isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = "Cannot read transaction record."; return {}; }
    const auto bytes = file.read(8 * 1024 * 1024 + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > 8 * 1024 * 1024) { if (error) *error = "Transaction record read failed."; return {}; }
    const auto envelope = QJsonDocument::fromJson(bytes).object();
    const auto body = envelope["body"].toObject();
    if (envelope.size() != 2 || body.isEmpty() || envelope["sha256"].toString() != digest(body)) {
        if (error) *error = "Transaction record is corrupt.";
        return {};
    }
    return body;
}
QString area(OperationStore& store, const QString& id)
{
    if (!store.get(id).contains("result")) return {};
    return QDir(store.directory()).filePath(id);
}
bool privateDirectory(const QString& directory)
{
    const QFileInfo info(directory);
    return !info.isSymLink() && !info.isJunction() && createPrivateStorageDirectory(directory);
}
bool validHash(const QString& hash)
{
    static const QRegularExpression pattern(QRegularExpression::anchoredPattern("[0-9a-f]{64}"));
    return hash == "missing" || pattern.match(hash).hasMatch();
}
QJsonObject planAt(OperationStore& store, const QString& id, const QString& root, const QString& mods, QString* error)
{
    const auto directory = area(store, id);
    if (directory.isEmpty()) { if (error) *error = "No such operation."; return {}; }
    auto plan = load(directory + "/plan.json", error);
    const auto claimed = plan.take("planDigest").toString();
    if (plan["format"].toInt() != 2 || plan["operationId"] != id || plan["physicalId"].toString() != physicalIdentityV2(root) ||
        plan["modsRelative"] != mods || !relative(mods) || !mods.endsWith("/mods") ||
        claimed != digest(plan) || plan["instanceRevision"].toString().size() != 64 || plan["files"].toArray().isEmpty() ||
        plan["files"].toArray().size() > 10000) { if (error) *error = "Transaction plan identity or digest is invalid."; return {}; }
    QSet<QString> paths;
    for (const auto& value : plan["files"].toArray()) {
        const auto file = value.toObject();
        const auto path = file["path"].toString(), stage = file["stagedName"].toString();
        if (!allowedPath(path, mods) || !safePath(root, path, error) || paths.contains(path.toCaseFolded()) ||
            !validHash(file["beforeHash"].toString()) || !validHash(file["afterHash"].toString()) ||
            (!stage.isEmpty() && !leaf(stage)) || (stage.isEmpty() != (file["afterHash"] == "missing")) ||
            (stage.isEmpty() && protectedMetadata(path))) {
            if (error) *error = "Transaction file table is invalid.";
            return {};
        }
        paths.insert(path.toCaseFolded());
    }
    plan["planDigest"] = claimed;
    return plan;
}
bool owns(const QString& root, const std::shared_ptr<PhysicalInstanceLease>& lease)
{
    if (!lease || lease->root() != root || lease->physicalId() != physicalIdentityV2(root) ||
        !InstanceAuthority::validate(root, lease->physicalId(), lease->registryDirectory())) return false;
    const auto game = GameProcessGroup::probe(lease->physicalId(), lease->registryDirectory());
    return game != GameProcessGroup::State::Running && game != GameProcessGroup::State::Unknown;
}
bool pinInventory(const QString& root, const QString& mods, const std::shared_ptr<PhysicalInstanceLease>& lease, QString* error)
{
    if (!lease->pinRoot(error)) return false;
    int count = 0;
    std::function<bool(const QString&)> walk = [&](const QString& path) {
        if (!QFileInfo::exists(root + '/' + path)) return true;
        if (++count > 100000 || !lease->pinDirectory(path, error)) return false;
        for (const auto& entry : QDir(root + '/' + path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            if (!walk(path + '/' + entry.fileName())) return false;
        return true;
    };
    QSet<QString> overrides;
    return walk(mods) && walk(".awake-integration") &&
        overridePaths(root, root + "/.awake-integration/policies.json", mods, overrides, error) && pinParents(root, overrides, lease, error);
}
QString markerPath(const QString& root) { return root + "/.awake-integration/recovery.json"; }
bool clearMarker(const QString& root, const QString& id, QString* error)
{
    const auto path = markerPath(root);
    if (!QFileInfo::exists(path)) return true;
    const auto marker = load(path, error);
    if (marker["operationId"] != id || marker["physicalId"].toString() != physicalIdentityV2(root)) {
        if (error) *error = "A different or invalid recovery marker owns the instance.";
        return false;
    }
    return DurableIO::remove(path, error);
}
bool writeJournal(const QString& directory, const QString& id, const QString& planDigest, const QString& phase, int cursor,
                  QString* error, const QJsonObject& completion = {})
{
    QJsonObject body{{"format", 1}, {"operationId", id}, {"planDigest", planDigest}, {"phase", phase}, {"cursor", cursor}};
    if (!completion.isEmpty()) body["completion"] = completion;
    return save(directory + "/journal.json", body, error);
}
bool verifyFiles(const QString& root, const QJsonArray& files, const QString& which, QString* error)
{
    for (const auto& value : files) {
        const auto file = value.toObject();
        const auto path = file["path"].toString();
        if (!safePath(root, path, error) || DurableIO::hash(root + '/' + path, error) != file[which].toString()) {
            if (error && error->isEmpty()) *error = "File verification failed: " + path;
            return false;
        }
    }
    return true;
}
QJsonObject recoveryRequired(OperationStore& store, const QString& id, const QString& message)
{
    store.transition(id, "recovery_required", {}, {{"code", "RECOVERY_FAILED"}, {"message", message}, {"retryable", false}});
    return failure("RECOVERY_REQUIRED", message);
}
}  // namespace
TransactionEngine::TransactionEngine(OperationStore& store, QString root, QString mods)
    : m_store(store), m_root(QFileInfo(root).canonicalFilePath()), m_mods(std::move(mods)) {}
QString TransactionEngine::revision(QString* error) const
{
    const auto state = snapshot(error);
    return state.isEmpty() ? QString{} : digest(state);
}
QJsonObject TransactionEngine::snapshot(QString* error) const
{
    if (m_root.isEmpty() || !relative(m_mods) || !m_mods.endsWith("/mods")) { if (error) *error = "Invalid instance root or mod directory."; return {}; }
    QMap<QString, QString> nodes;
    for (const auto* name : {"instance.cfg", "mmc-pack.json", ".awake-integration/policies.json"}) {
        if (!safePath(m_root, name, error)) return {};
        const auto hash = DurableIO::hash(m_root + '/' + name, error);
        if (hash.isEmpty() || (QString(name) == "instance.cfg" && hash == "missing")) { if (error && error->isEmpty()) *error = "Instance metadata is missing."; return {}; }
        nodes[name] = hash;
    }
    std::function<bool(const QString&)> walk = [&](const QString& directory) {
        if (!safePath(m_root, directory, error)) return false;
        const QFileInfo info(m_root + '/' + directory);
        if (!info.exists()) return true;
        if (!info.isDir() || !info.isReadable()) { if (error) *error = "An inventory directory cannot be read."; return false; }
        for (const auto& entry : QDir(info.absoluteFilePath()).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name)) {
            const auto path = directory + '/' + entry.fileName();
            if (nodes.size() >= 100000 || !safePath(m_root, path, error)) { if (error && error->isEmpty()) *error = "Inventory exceeds 100,000 paths."; return false; }
            if (entry.isDir()) {
                if (path != m_mods + "/.index") nodes[path] = "directory";
                if (!walk(path)) return false;
            } else {
                const auto hash = DurableIO::hash(entry.absoluteFilePath(), error);
                if (hash.isEmpty() || hash == "missing") return false;
                nodes[path] = hash;
            }
        }
        return true;
    };
    const auto identity = physicalIdentityV2(m_root);
    if (identity.isEmpty() || !walk(m_mods)) return {};
    QSet<QString> overrides;
    if (!overridePaths(m_root, m_root + "/.awake-integration/policies.json", m_mods, overrides, error)) return {};
    for (const auto& path : overrides) {
        if (nodes.size() >= 100000 && !nodes.contains(path)) { if (error) *error = "Inventory exceeds 100,000 paths."; return {}; }
        const auto hash = DurableIO::hash(m_root + '/' + path, error);
        if (hash.isEmpty()) return {};
        nodes[path] = hash;
    }
    if (identity != physicalIdentityV2(m_root)) return {};
    QJsonObject files;
    for (auto it = nodes.begin(); it != nodes.end(); ++it) files[it.key()] = it.value();
    return {{"physicalId", identity}, {"modsRelative", m_mods}, {"files", files}};
}
QJsonObject TransactionEngine::prepare(const QString& id, const QList<FileChange>& changes, const QString& expected, const QJsonObject& review)
{
    const auto directory = area(m_store, id);
    if (directory.isEmpty()) return failure("NOT_FOUND", "No such operation.");
    const auto state = m_store.get(id)["result"].toObject()["state"].toString();
    if (state != "downloading" && state != "resolving") return failure("INVALID_STATE", "Preparation requires a resolved operation.");
    if (changes.isEmpty() || changes.size() > 10000) return failure("LIMIT_EXCEEDED", "A plan requires 1 to 10,000 file changes.");
    QString error;
    const auto baseline = revision(&error);
    if (baseline.isEmpty()) return failure("IDENTITY_UNVERIFIABLE", error);
    if (baseline != expected) return failure("STALE_INSTANCE", "Instance content changed before preparation.");
    if (QFileInfo::exists(markerPath(m_root))) return failure("RECOVERY_REQUIRED", "The instance has unresolved recovery.");
    if (!privateDirectory(directory) || !privateDirectory(directory + "/staging")) return failure("IO_ERROR", "Private staging is unavailable.");
    QSet<QString> targets;
    QJsonArray files;
    auto expectedState = snapshot(&error);
    if (expectedState.isEmpty() || digest(expectedState) != baseline) return failure("STALE_INSTANCE", "Instance changed before planning.");
    auto expectedFiles = expectedState["files"].toObject();
    QSet<QString> oldOverrides, nextOverrides;
    if (!overridePaths(m_root, m_root + "/.awake-integration/policies.json", m_mods, oldOverrides, &error)) return failure("IDENTITY_UNVERIFIABLE", error);
    nextOverrides = oldOverrides;
    for (const auto& change : changes) if (change.relativePath == ".awake-integration/policies.json") {
        if (!leaf(change.stagedName) || !safePath(directory, "staging/" + change.stagedName, &error)) return failure("INVALID_REQUEST", "Invalid staged policies.");
        nextOverrides.clear();
        if (!overridePaths(m_root, directory + "/staging/" + change.stagedName, m_mods, nextOverrides, &error)) return failure("INVALID_REQUEST", error);
    }
    quint64 bytes = 16 * 1024 * 1024;
    for (const auto& change : changes) {
        const auto path = change.relativePath;
        if (path.startsWith(".minecraft/") && !path.startsWith(m_mods + '/', Qt::CaseInsensitive) && !nextOverrides.contains(path))
            return failure("INVALID_REQUEST", "Game file targets must be declared as managed overrides.");
        if (change.stagedName.isEmpty() && protectedMetadata(path)) return failure("INVALID_REQUEST", "Native instance and policy metadata must be serialized, not deleted.");
        if (!allowedPath(path, m_mods) || !safePath(m_root, path, &error) || targets.contains(path.toCaseFolded()) ||
            (!change.stagedName.isEmpty() && !leaf(change.stagedName))) return failure("IDENTITY_CONFLICT", "Invalid, linked or colliding target.");
        targets.insert(path.toCaseFolded());
        const auto destination = m_root + '/' + path;
        const auto beforeState = DurableIO::fingerprint(destination, &error);
        const auto before = beforeState.hash;
        const auto after = change.stagedName.isEmpty() ? QString("missing") : DurableIO::hash(directory + "/staging/" + change.stagedName, &error);
        if (before.isEmpty() || after.isEmpty() || (!change.stagedName.isEmpty() && after == "missing")) return failure("INTEGRITY_FAILED", error);
        if (before == "missing" && after == "missing") return failure("INVALID_REQUEST", "A removal target is absent.");
        const auto sizeBefore = before == "missing" ? 0 : QFileInfo(destination).size();
        const auto sizeAfter = after == "missing" ? 0 : QFileInfo(directory + "/staging/" + change.stagedName).size();
        if (sizeBefore < 0 || sizeAfter < 0 || quint64(sizeBefore) + quint64(sizeAfter) > (1ULL << 52) - bytes)
            return failure("LIMIT_EXCEEDED", "Transaction size cannot be represented safely.");
        bytes += quint64(sizeBefore) + quint64(sizeAfter);
        files.append(QJsonObject{{"path", path}, {"stagedName", change.stagedName}, {"beforeHash", before}, {"beforeIdentity", beforeState.identity}, {"afterHash", after},
                                 {"sizeBefore", QString::number(sizeBefore)}, {"sizeAfter", QString::number(sizeAfter)}});
        if (after == "missing") expectedFiles.remove(path); else expectedFiles[path] = after;
    }
    for (const auto& path : oldOverrides) if (!nextOverrides.contains(path) && (!path.startsWith(m_mods + '/') || expectedFiles.value(path) == "missing")) expectedFiles.remove(path);
    for (const auto& path : nextOverrides) {
        bool changed = false;
        for (const auto& value : files) if (value.toObject()["path"] == path) { expectedFiles[path] = value.toObject()["afterHash"]; changed = true; break; }
        if (!changed) {
            const auto hash = DurableIO::hash(m_root + '/' + path, &error);
            if (hash.isEmpty()) return failure("INTEGRITY_FAILED", error);
            expectedFiles[path] = hash;
        }
    }
    const QStorageInfo targetVolume(m_root), storeVolume(directory);
    if (!targetVolume.isValid() || !storeVolume.isValid() || targetVolume.bytesAvailable() < 0 || storeVolume.bytesAvailable() < 0 ||
        quint64(targetVolume.bytesAvailable()) < bytes || quint64(storeVolume.bytesAvailable()) < bytes)
        return failure("IO_ERROR", "Insufficient or unverifiable disk space for backup, replacement and recovery.");
    if (revision(&error) != baseline) return failure("STALE_INSTANCE", "Instance changed during preparation.");
    QJsonObject plan{{"format", 2}, {"operationId", id}, {"physicalId", physicalIdentityV2(m_root)}, {"modsRelative", m_mods},
                     {"instanceRevision", baseline}, {"requiredDiskBytes", QString::number(bytes)}, {"files", files}};
    expectedState["files"] = expectedFiles;
    plan["expectedInstanceRevision"] = digest(expectedState);
    if (!review.isEmpty()) plan["review"] = review;
    const auto planDigest = digest(plan); plan["planDigest"] = planDigest;
    if (!save(directory + "/plan.json", plan, &error) || !writeJournal(directory, id, planDigest, "prepared", 0, &error)) return failure("IO_ERROR", error);
    const auto prepared = m_store.transition(id, "prepared", plan);
    return prepared.contains("error") ? prepared : QJsonObject{{"result", plan}};
}
QJsonObject TransactionEngine::apply(const QString& id, const QString& claimed, const QString& expected,
                                    const std::shared_ptr<PhysicalInstanceLease>& lease, FaultHook fault)
{
    if (!owns(m_root, lease)) return failure("BUSY", "An exclusive physical-instance lease is required.", true);
    QString error;
    if (!pinInventory(m_root, m_mods, lease, &error)) return failure("IDENTITY_UNVERIFIABLE", error);
    const auto plan = planAt(m_store, id, m_root, m_mods, &error);
    if (plan.isEmpty()) return failure("RECOVERY_REQUIRED", error);
    if (!pinPlan(m_root, plan, lease, &error)) return failure("IDENTITY_UNVERIFIABLE", error);
    if (claimed != plan["planDigest"] || expected != plan["instanceRevision"]) return failure("STALE_PLAN", "Apply does not identify the reviewed plan.");
    if (plan["expectedInstanceRevision"].toString().size() != 64) return failure("STALE_PLAN", "This older plan requires fresh preparation and review.");
    const auto current = m_store.get(id)["result"].toObject();
    if (current["state"] != "prepared") return failure("INVALID_STATE", "The operation is not awaiting Apply.");
    const auto directory = area(m_store, id);
    const auto files = plan["files"].toArray();
    for (const auto& value : files) {
        const auto file = value.toObject();
        if (file["stagedName"].toString().isEmpty()) continue;
        if (!safePath(directory, "staging/" + file["stagedName"].toString(), &error) ||
            DurableIO::hash(directory + "/staging/" + file["stagedName"].toString(), &error) != file["afterHash"])
            return failure("INTEGRITY_FAILED", "A prepared artifact changed.");
    }
    if (revision(&error) != expected) return failure("STALE_INSTANCE", "Instance bytes differ from the reviewed baseline.");
    if (QFileInfo::exists(markerPath(m_root))) return failure("RECOVERY_REQUIRED", "Recovery must finish before Apply.");
    const auto budget = plan["requiredDiskBytes"].toString().toULongLong();
    const QStorageInfo targetVolume(m_root), storeVolume(directory);
    if (targetVolume.bytesAvailable() < 0 || storeVolume.bytesAvailable() < 0 || quint64(targetVolume.bytesAvailable()) < budget ||
        quint64(storeVolume.bytesAvailable()) < budget) return failure("IO_ERROR", "Disk space changed before Apply.");
    QJsonObject summary{{"planDigest", claimed}, {"instanceRevision", expected}, {"fileCount", files.size()}};
    for (const auto* state : {"acquiring", "revalidating", "backing_up"}) {
        const auto updated = m_store.transition(id, state, summary);
        if (updated.contains("error")) return updated;
    }
    if (!privateDirectory(directory + "/backups") || !privateDirectory(m_root + "/.awake-integration"))
        return failure("IO_ERROR", "Backup or recovery marker storage is unavailable.");
    if (!lease->pinDirectory(".awake-integration", &error)) return failure("IO_ERROR", error);
    const auto evidence = m_root + "/.awake-integration/transactions/" + id;
    if (!privateDirectory(m_root + "/.awake-integration/transactions") || !privateDirectory(evidence) ||
        !lease->pinDirectory(".awake-integration/transactions/" + id, &error)) return failure("IO_ERROR", "Cannot secure displaced-file storage.");
    if (!writeJournal(directory, id, claimed, "backing_up", 0, &error) ||
        !save(markerPath(m_root), {{"operationId", id}, {"physicalId", lease->physicalId()}, {"planDigest", claimed},
                                  {"storePath", m_store.directory()}, {"storePhysicalId", physicalIdentityV2(m_store.directory())}}, &error))
        return failure("IO_ERROR", error);
    const auto interrupted = [&] {
        if (error.isEmpty()) error = "File transaction failed; rollback was required.";
        m_store.transition(id, "recovery_required", {}, {{"code", "IO_ERROR"}, {"message", error}, {"retryable", false}});
        const auto restored = recover(id, lease);
        if (restored.contains("error")) return restored;
        return failure("IO_ERROR", error.isEmpty() ? "File transaction failed and was rolled back." : error);
    };
    const auto inject = [&](const QString& point, int index) {
        if (!fault || fault(point, index)) return true;
        const auto code = GetLastError();
        error = QString("Storage failure at %1 (Windows error %2)").arg(point).arg(code);
        return false;
    };
    for (qsizetype index = 0; index < files.size(); ++index) {
        const auto file = files[index].toObject();
        if (file["beforeHash"] == "missing") continue;
        const auto backup = directory + "/backups/" + QString::number(index);
        if (!inject("backup", int(index)) || !DurableIO::copy(m_root + '/' + file["path"].toString(), backup, &error) ||
            DurableIO::hash(backup, &error) != file["beforeHash"]) return interrupted();
    }
    if (!verifyFiles(m_root, files, "beforeHash", &error) || !writeJournal(directory, id, claimed, "applying", 0, &error)) return interrupted();
    const auto applying = m_store.transition(id, "applying", summary);
    if (applying.contains("error")) return interrupted();
    for (qsizetype index = 0; index < files.size(); ++index) {
        const auto file = files[index].toObject();
        const auto path = file["path"].toString(), destination = m_root + '/' + path;
        if (!safePath(m_root, path, &error) || DurableIO::hash(destination, &error) != file["beforeHash"] ||
            !writeJournal(directory, id, claimed, "applying", int(index), &error) || !inject("before_replace", int(index))) return interrupted();
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())) { error = "Cannot create target directory."; return interrupted(); }
        if (!lease->pinDirectory(QFileInfo(path).path(), &error)) return interrupted();
        const auto prefix = evidence + '/' + QString::number(index);
        const auto incoming = QFileInfo(destination).absolutePath() + "/.awake-" + id + '-' + QString::number(index) + ".incoming";
        QJsonObject intent{{"format", 1}, {"planDigest", claimed}, {"path", path}};
        if (!save(prefix + ".json", intent, &error)) return interrupted();
        QString source;
        DurableIO::FileState sourceState;
        if (file["afterHash"] != "missing") {
            if (!DurableIO::stage(directory + "/staging/" + file["stagedName"].toString(), incoming, file["afterHash"].toString(),
                [&](const QString& identity) { intent["incomingIdentity"] = identity; return save(prefix + ".json", intent, &error); }, &error)) return interrupted();
            source = incoming; sourceState = {file["afterHash"].toString(), intent["incomingIdentity"].toString()};
        }
        if (!inject("before_exchange", int(index))) return interrupted();
        const bool replaced = DurableIO::exchange(source, sourceState, destination,
            {file["beforeHash"].toString(), file["beforeIdentity"].toString()}, prefix + ".actual", &error,
            [&] { return inject("after_displace", int(index)); });
        if (!replaced || DurableIO::hash(destination, &error) != file["afterHash"] || !inject("after_replace", int(index))) return interrupted();
    }
    if (!writeJournal(directory, id, claimed, "verifying", int(files.size()), &error) || !verifyFiles(m_root, files, "afterHash", &error)) return interrupted();
    const auto expectedAfter = plan["expectedInstanceRevision"].toString();
    if (expectedAfter.size() != 64 || revision(&error) != expectedAfter) {
        error = "The final inventory differs from the reviewed changes.";
        return interrupted();
    }
    summary["instanceRevision"] = expectedAfter;
    const auto verified = m_store.transition(id, "verifying", summary);
    if (verified.contains("error")) return interrupted();
    return verified;
}
QJsonObject TransactionEngine::complete(const QString& id, const std::shared_ptr<PhysicalInstanceLease>& lease, FaultHook fault)
{
    if (!owns(m_root, lease)) return failure("BUSY", "An exclusive physical-instance lease is required.", true);
    QString error;
    if (!pinInventory(m_root, m_mods, lease, &error)) return failure("IDENTITY_UNVERIFIABLE", error);
    const auto plan = planAt(m_store, id, m_root, m_mods, &error);
    if (plan.isEmpty()) return failure("RECOVERY_REQUIRED", error);
    if (!pinPlan(m_root, plan, lease, &error)) return failure("IDENTITY_UNVERIFIABLE", error);
    const auto status = m_store.get(id)["result"].toObject();
    if (status["state"] != "verifying") return failure("INVALID_STATE", "File and native-model verification must precede completion.");
    if (!verifyFiles(m_root, plan["files"].toArray(), "afterHash", &error) ||
        revision(&error) != status["result"].toObject()["instanceRevision"].toString()) return recoveryRequired(m_store, id, "Final bytes changed before completion.");
    if (!writeJournal(area(m_store, id), id, plan["planDigest"].toString(), "completed", int(plan["files"].toArray().size()), &error,
                      status["result"].toObject())) return recoveryRequired(m_store, id, error);
    if (fault && !fault("after_commit_record", int(plan["files"].toArray().size()))) return recoveryRequired(m_store, id, "Completion recording was interrupted.");
    const auto result = m_store.transition(id, "completed", status["result"].toObject());
    if (result.contains("error")) return result;
    if (!clearMarker(m_root, id, &error)) return failure("RECOVERY_REQUIRED", error);
    return result;
}
QJsonObject TransactionEngine::recover(const QString& id, const std::shared_ptr<PhysicalInstanceLease>& lease, FaultHook fault)
{
    if (!owns(m_root, lease)) return failure("BUSY", "An exclusive physical-instance lease is required.", true);
    QString error;
    if (!pinInventory(m_root, m_mods, lease, &error)) return failure("IDENTITY_UNVERIFIABLE", error);
    const auto directory = area(m_store, id);
    const auto plan = planAt(m_store, id, m_root, m_mods, &error);
    if (plan.isEmpty()) return recoveryRequired(m_store, id, error);
    if (!pinPlan(m_root, plan, lease, &error)) return recoveryRequired(m_store, id, error);
    auto status = m_store.get(id)["result"].toObject();
    if (OperationStore::terminal(status["state"].toString())) {
        if (QFileInfo::exists(markerPath(m_root))) {
            const auto which = status["state"] == "completed" ? "afterHash" : "beforeHash";
            const auto expected = status["state"] == "completed" ? status["result"].toObject()["instanceRevision"].toString() : plan["instanceRevision"].toString();
            if (!verifyFiles(m_root, plan["files"].toArray(), which, &error) || revision(&error) != expected)
                return failure("RECOVERY_REQUIRED", "Retained recovery evidence cannot be cleared because instance bytes changed.");
        }
        if (!clearMarker(m_root, id, &error)) return failure("RECOVERY_REQUIRED", error);
        return m_store.get(id);
    }
    const auto journal = load(directory + "/journal.json", &error);
    if (journal["format"].toInt() != 1 || journal["operationId"] != id || journal["planDigest"] != plan["planDigest"])
        return recoveryRequired(m_store, id, error.isEmpty() ? "Recovery journal identity is invalid." : error);
    if (journal["phase"] == "prepared" && status["state"] == "prepared") return m_store.get(id);
    const auto files = plan["files"].toArray();
    if (journal["phase"] == "completed") {
        const auto completion = journal["completion"].toObject();
        if (completion["planDigest"] != plan["planDigest"] || completion["instanceRevision"].toString().size() != 64 ||
            !verifyFiles(m_root, files, "afterHash", &error) || revision(&error) != completion["instanceRevision"].toString())
            return recoveryRequired(m_store, id, "Committed files cannot be verified.");
        if (status["state"] != "verifying") {
            if (status["state"] != "recovery_required") {
                const auto pending = m_store.transition(id, "recovery_required");
                if (pending.contains("error")) return pending;
            }
            const auto verified = m_store.transition(id, "verifying", completion);
            if (verified.contains("error")) return verified;
        }
        const auto completed = m_store.transition(id, "completed", completion);
        if (completed.contains("error")) return completed;
        if (!clearMarker(m_root, id, &error)) return failure("RECOVERY_REQUIRED", error);
        return completed;
    }
    if (status["state"] != "recovery_required") {
        const auto pending = m_store.transition(id, "recovery_required");
        if (pending.contains("error")) return pending;
    }
    const auto rolling = m_store.transition(id, "rolling_back");
    if (rolling.contains("error")) return rolling;
    const auto blocked = [&] {
        m_store.transition(id, "recovery_required", {}, {{"code", "RECOVERY_FAILED"}, {"message", error}, {"retryable", false}});
        return failure("RECOVERY_REQUIRED", error.isEmpty() ? "Rollback requires attention." : error);
    };
    for (qsizetype index = files.size(); index-- > 0;) {
        const auto file = files[index].toObject();
        const auto path = file["path"].toString(), destination = m_root + '/' + path;
        if (!safePath(m_root, path, &error)) return blocked();
        const auto prefix = m_root + "/.awake-integration/transactions/" + id + '/' + QString::number(index);
        const auto incoming = QFileInfo(destination).absolutePath() + "/.awake-" + id + '-' + QString::number(index) + ".incoming";
        QJsonObject intent;
        if (QFileInfo::exists(prefix + ".json")) {
            intent = load(prefix + ".json", &error);
            if (intent["format"] != 1 || intent["planDigest"] != plan["planDigest"] || intent["path"] != path) return blocked();
        }
        if (!DurableIO::discardIncoming(incoming, intent["incomingIdentity"].toString(),
                                       directory + "/staging/" + file["stagedName"].toString(), &error)) return blocked();
        const auto original = DurableIO::fingerprint(prefix + ".actual", &error, destination);
        const DurableIO::FileState baseline{file["beforeHash"].toString(), file["beforeIdentity"].toString()};
        if (original.hash != "missing" && (original.hash != baseline.hash || original.identity != baseline.identity)) {
            error = "Unrecognized displaced file preserved: " + path + "; " + error; return blocked();
        }
        const auto current = DurableIO::fingerprint(destination, &error, destination);
        if (current.hash == baseline.hash && current.identity == baseline.identity) {
            if (!DurableIO::restoreAccess(destination, baseline, &error)) return blocked();
            continue;
        }
        const bool installed = current.hash == file["afterHash"] && current.identity == intent["incomingIdentity"].toString();
        const bool removed = current.hash == "missing" && original.hash == baseline.hash && original.identity == baseline.identity;
        if ((!installed && !removed) || current.hash.isEmpty() ||
            (baseline.hash != "missing" && original.hash == "missing")) {
            error = "Unrecognized bytes preserved during recovery: " + path; return blocked();
        }
        if (!writeJournal(directory, id, plan["planDigest"].toString(), "rolling_back", int(index), &error) ||
            (fault && !fault("before_restore", int(index)))) return blocked();
        if (!lease->pinDirectory(QFileInfo(path).path(), &error) ||
            !DurableIO::exchange(baseline.hash == "missing" ? QString{} : prefix + ".actual", baseline,
                                 destination, current, prefix + ".undo-actual", &error)) return blocked();
        if (DurableIO::hash(destination, &error) != file["beforeHash"] || (fault && !fault("after_restore", int(index)))) return blocked();
    }
    if (!verifyFiles(m_root, files, "beforeHash", &error) || revision(&error) != plan["instanceRevision"].toString() ||
        !writeJournal(directory, id, plan["planDigest"].toString(), "rolled_back", 0, &error)) return blocked();
    const auto restored = m_store.transition(id, "rolled_back", {{"planDigest", plan["planDigest"]},
        {"instanceRevision", plan["instanceRevision"]}, {"rolledBack", true}, {"fileCount", files.size()}});
    if (restored.contains("error")) return restored;
    if (!clearMarker(m_root, id, &error)) return failure("RECOVERY_REQUIRED", error);
    return restored;
}
}  // namespace Awake::Integration
