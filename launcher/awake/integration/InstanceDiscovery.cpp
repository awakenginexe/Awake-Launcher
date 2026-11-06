// SPDX-License-Identifier: GPL-3.0-only
#include "InstanceDiscovery.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include "Protocol.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <sys/stat.h>
#endif
namespace Awake::Integration {
QString physicalIdentityV2(const QString& path)
{
#ifdef Q_OS_WIN
    const auto canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty()) return {};
    const auto name = "\\\\?\\" + QDir::toNativeSeparators(canonical);
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    const auto flags = FILE_NAME_NORMALIZED | VOLUME_NAME_GUID;
    const auto needed = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
    FILE_ID_INFO information{};
    if (!needed || needed > 32768 || !GetFileInformationByHandleEx(handle, FileIdInfo, &information, sizeof(information))) {
        CloseHandle(handle);
        return {};
    }
    QString final;
    final.resize(needed);
    const auto count = GetFinalPathNameByHandleW(handle, reinterpret_cast<LPWSTR>(final.data()), needed, flags);
    CloseHandle(handle);
    if (!count || count >= needed) return {};
    final.truncate(count);
    const auto end = final.indexOf('}');
    if (!final.startsWith("\\\\?\\Volume{") || end < 11) return {};
    return "win2:" + final.mid(11, end - 11).toLower() + ':' +
        QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(information.FileId.Identifier), 16).toHex());
#else
    Q_UNUSED(path)
    return {};
#endif
}
QString physicalIdentity(const QString& path)
{
    const auto canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty())
        return {};
#ifdef Q_OS_WIN
    auto nativePath = QDir::toNativeSeparators(canonical);
    if (nativePath.startsWith("\\\\"))
        nativePath = "\\\\?\\UNC\\" + nativePath.mid(2);
    else
        nativePath = "\\\\?\\" + nativePath;
    const auto handle =
        CreateFileW(reinterpret_cast<LPCWSTR>(nativePath.utf16()), FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {};
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok = GetFileInformationByHandle(handle, &info) != 0;
    CloseHandle(handle);
    if (!ok)
        return {};
    return QString("win:%1:%2:%3").arg(info.dwVolumeSerialNumber, 0, 16).arg(info.nFileIndexHigh, 0, 16).arg(info.nFileIndexLow, 0, 16);
#else
    struct stat info{};
    if (::stat(QFile::encodeName(canonical).constData(), &info) != 0)
        return {};
    return QString("unix:%1:%2").arg(qulonglong(info.st_dev)).arg(qulonglong(info.st_ino));
#endif
}
namespace {
bool below(const QString& child, const QString& parent)
{
#ifdef Q_OS_WIN
    return child.startsWith(parent.endsWith('/') ? parent : parent + '/', Qt::CaseInsensitive);
#else
    return child.startsWith(parent.endsWith('/') ? parent : parent + '/');
#endif
}
void addWarning(QJsonArray& warnings, const QString& code, const QString& path)
{
    warnings.append(QJsonObject{ { "code", code }, { "path", path } });
}
}  // namespace
QJsonObject discover(const QStringList& roots, const QList<InstanceSeed>& loaded)
{
    QMap<QString, QJsonObject> instances;
    QMap<QString, QString> rootIds;
    QJsonArray warnings;
    QStringList canonicalRoots;
    QMap<QString, InstanceSeed> native;
    for (const auto& seed : loaded) {
        const auto key = physicalIdentity(seed.path);
        if (!key.isEmpty())
            native.insert(key, seed);
    }
    for (const auto& root : roots) {
        const auto path = QFileInfo(root).canonicalFilePath();
        const auto rootId = physicalIdentity(root);
        if (rootId.isEmpty()) {
            addWarning(warnings, "ROOT_UNAVAILABLE", QDir(root).absolutePath());
            continue;
        }
        if (rootIds.contains(rootId)) {
            addWarning(warnings, "ROOT_ALIAS", path);
            continue;
        }
        for (const auto& earlier : canonicalRoots)
            if (below(path, earlier) || below(earlier, path))
                addWarning(warnings, "ROOT_OVERLAP", path);
        canonicalRoots.append(path);
        rootIds.insert(rootId, path);
        for (const auto& info : QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
            if (!QFileInfo(QDir(info.absoluteFilePath()).filePath("instance.cfg")).isFile())
                continue;
            const auto key = physicalIdentity(info.absoluteFilePath());
            const auto canonical = info.canonicalFilePath();
            if (key.isEmpty()) {
                addWarning(warnings, "IDENTITY_UNVERIFIABLE", info.absoluteFilePath());
                continue;
            }
            if (instances.contains(key)) {
                addWarning(warnings, "INSTANCE_ALIAS", info.absoluteFilePath());
                continue;
            }
            const bool isLoaded = native.contains(key);
            const auto seed = native.value(key);
            instances.insert(
                key,
                QJsonObject{ { "physicalId", key },
                             { "canonicalPath", canonical },
                             { "instanceId", isLoaded ? seed.id : info.fileName() },
                             { "uuid", isLoaded ? QJsonValue(seed.uuid) : QJsonValue::Null },
                             { "name", isLoaded ? seed.name : info.fileName() },
                             { "loaded", isLoaded },
                             { "nativeRunning", isLoaded ? QJsonValue(seed.running) : QJsonValue::Null },
                             { "nativeActivity",
                               isLoaded ? QJsonValue(seed.activity.isEmpty() ? (seed.running ? "launch_active" : "idle") : seed.activity)
                                        : QJsonValue::Null },
                             { "gameState", "unknown" },
                             { "gamePhysicalId",
                               isLoaded ? QJsonValue(physicalIdentity(QFileInfo(seed.modsPath).dir().absolutePath())) : QJsonValue::Null },
                             { "modsPhysicalId", isLoaded ? QJsonValue(physicalIdentity(seed.modsPath)) : QJsonValue::Null },
                             { "identityStatus", "verified" } });
        }
    }
    QMap<QString, int> names, uuids, games, modRoots;
    for (const auto& instance : instances) {
        ++names[instance["instanceId"].toString()];
        const auto uuid = instance["uuid"].toString();
        if (!uuid.isEmpty())
            ++uuids[uuid];
        const auto game = instance["gamePhysicalId"].toString(), mods = instance["modsPhysicalId"].toString();
        if (!game.isEmpty())
            ++games[game];
        if (!mods.isEmpty())
            ++modRoots[mods];
    }
    QJsonArray output;
    for (auto instance : instances) {
        bool conflict = false;
        if (names.value(instance["instanceId"].toString()) > 1) {
            addWarning(warnings, "DUPLICATE_INSTANCE_ID", instance["canonicalPath"].toString());
            conflict = true;
        }
        if (uuids.value(instance["uuid"].toString()) > 1) {
            addWarning(warnings, "DUPLICATE_UUID", instance["canonicalPath"].toString());
            conflict = true;
        }
        if (games.value(instance["gamePhysicalId"].toString()) > 1 || modRoots.value(instance["modsPhysicalId"].toString()) > 1) {
            addWarning(warnings, "SHARED_GAME_FILES", instance["canonicalPath"].toString());
            conflict = true;
        }
        if (conflict)
            instance["identityStatus"] = "conflict";
        output.append(instance);
    }
    return { { "instances", output }, { "warnings", warnings } };
}
QJsonObject inventory(const InstanceSeed& instance, int offset, int limit, const QString& expectedRevision)
{
    if (offset < 0 || limit < 1 || limit > 500)
        return failure("INVALID_REQUEST", "Invalid inventory page.");
    const auto identity = physicalIdentity(instance.path);
    if (identity.isEmpty())
        return failure("IDENTITY_UNVERIFIABLE", "The instance directory cannot be identified.");
    if (!QFileInfo(QDir(instance.path).filePath("instance.cfg")).isFile())
        return failure("STALE_INSTANCE", "Instance metadata was removed.");
    const QDir mods(instance.modsPath);
    const auto modsIdentity = physicalIdentity(instance.modsPath);
    if (mods.exists() && !QFileInfo(mods.absolutePath()).isReadable())
        return failure("IO_ERROR", "The mod directory cannot be read.");
    QCryptographicHash revision(QCryptographicHash::Sha256);
    const auto add = [&revision](const QByteArray& bytes) {
        revision.addData(QByteArray::number(bytes.size()) + ':');
        revision.addData(bytes);
    };
    add(identity.toUtf8());
    add(modsIdentity.toUtf8());
    // Observation tokens intentionally exclude JAR content; Apply requires a separate hash baseline under the gate.
    const auto entries = mods.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
    QJsonArray items;
    const auto observe = [&add](const QFileInfo& info) {
        add(info.fileName().toUtf8());
        add(QByteArray::number(info.size()));
        add(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
        add(info.symLinkTarget().toUtf8());
    };
    for (const auto& info : entries) {
        if (info.fileName() == ".index")
            continue;
        observe(info);
        const auto name = info.fileName();
        const bool enabled = !name.endsWith(".disabled", Qt::CaseInsensitive);
        const auto effectiveName = enabled ? name : name.first(name.size() - 9);
        items.append(QJsonObject{ { "fileName", name },
                                  { "enabled", enabled },
                                  { "size", QString::number(info.size()) },
                                  { "modifiedMs", QString::number(info.lastModified().toMSecsSinceEpoch()) },
                                  { "linked", info.isSymLink() },
                                  { "kind", info.isDir()                                          ? "directory"
                                            : effectiveName.endsWith(".jar", Qt::CaseInsensitive) ? "jar"
                                                                                                  : "other" } });
    }
    QStringList metadata{ QDir(instance.path).filePath("instance.cfg"), QDir(instance.path).filePath("mmc-pack.json") };
    for (const auto& info : QDir(mods.filePath(".index")).entryInfoList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name))
        metadata.append(info.absoluteFilePath());
    for (const auto& path : metadata) {
        QFileInfo info(path);
        observe(info);
        if (!info.exists()) {
            add("missing");
            continue;
        }
        if (info.isSymLink()) {
            add("linked");
            continue;
        }
        QFile file(path);
        if (info.size() > 4 * 1024 * 1024)
            return failure("LIMIT_EXCEEDED", "An instance metadata file exceeds 4 MiB.");
        if (!file.open(QIODevice::ReadOnly))
            return failure("IO_ERROR", "Instance metadata cannot be read.");
        const auto bytes = file.read(4 * 1024 * 1024 + 1);
        if (file.error() != QFileDevice::NoError)
            return failure("IO_ERROR", "Instance metadata read failed.");
        if (bytes.size() > 4 * 1024 * 1024)
            return failure("LIMIT_EXCEEDED", "Instance metadata grew beyond 4 MiB.");
        add(bytes);
    }
    if (physicalIdentity(instance.path) != identity || physicalIdentity(instance.modsPath) != modsIdentity ||
        !QFileInfo(QDir(instance.path).filePath("instance.cfg")).isFile())
        return failure("STALE_INSTANCE", "The instance directory changed during the read.");
    const auto digest = QString::fromLatin1(revision.result().toHex());
    if (!expectedRevision.isEmpty() && expectedRevision != digest)
        return failure("STALE_INSTANCE", "Inventory changed; restart pagination.");
    QJsonArray page;
    const auto end = qMin(qsizetype(offset) + limit, items.size());
    for (qsizetype i = offset; i < end; ++i)
        page.append(items[i]);
    return { { "result", QJsonObject{ { "items", page },
                                      { "total", items.size() },
                                      { "nextOffset", end < items.size() ? QJsonValue(end) : QJsonValue(QJsonValue::Null) },
                                      { "instanceRevision", digest },
                                      { "revisionKind", "observation" } } } };
}
}  // namespace Awake::Integration
