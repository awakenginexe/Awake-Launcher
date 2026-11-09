// SPDX-License-Identifier: GPL-3.0-only
#include "InstanceAuthority.h"
#include "DurableIO.h"
#include "InstanceDiscovery.h"
#include "ProfileAccess.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QUuid>
#include <qt_windows.h>
#include <shlobj.h>
namespace Awake::Integration::InstanceAuthority {
namespace {
QString base(QString* error)
{
    const auto parent = privateCoordinationBase(), directory = parent + "/authority-v1";
    if (parent.isEmpty() || !createPrivateStorageDirectory(directory)) {
        if (error) *error = "Canonical ownership storage is not private.";
        return {};
    }
    return QFileInfo(directory).canonicalFilePath();
}
QString stem(const QString& id, QString* error)
{
    static const QRegularExpression valid(QRegularExpression::anchoredPattern("win2:[0-9a-f-]{36}:[0-9a-f]{32}"));
    if (!valid.match(id).hasMatch()) { if (error) *error = "Strong physical identity is required."; return {}; }
    const auto directory = base(error); if (directory.isEmpty()) return {};
    return directory + '/' + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QJsonObject read(const QString& path, QString* error)
{
    if (!storageAccessIsPrivate(path) || DurableIO::hash(path, error).isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) return {};
    const auto envelope = QJsonDocument::fromJson(file.readAll()).object();
    const auto object = envelope["body"].toObject();
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    if (envelope.size() != 2 || object.isEmpty() || envelope["sha256"] != hash) {
        if (error) *error = "Ownership evidence is missing or corrupt.";
        return {};
    }
    return object;
}
QJsonObject withoutGame(QJsonObject object) { object.remove("gameRecordRequired"); return object; }
bool write(const QString& path, const QJsonObject& object, QString* error)
{
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    return DurableIO::write(path, QJsonDocument(QJsonObject{{"body", object}, {"sha256", hash}}).toJson(QJsonDocument::Compact), error);
}
}
void* lock(const QString& id, QString* error)
{
    const auto name = stem(id, error); if (name.isEmpty()) return nullptr;
    const auto path = "\\\\?\\" + QDir::toNativeSeparators(name + ".lock");
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || !storageAccessIsPrivate(name + ".lock")) {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (error) *error = "The physical instance is busy or canonical ownership cannot be locked.";
        return nullptr;
    }
    return handle;
}
QJsonObject binding(const QString& id, QString* error)
{
    const auto name = stem(id, error); if (name.isEmpty()) return {};
    const auto object = read(name + ".json", error);
    if (object.size() != 9 || object["format"] != 1 || object["kernelMajor"] != 2 || object["published"] != true || object["physicalId"] != id ||
        object["ownerProfile"].toString().isEmpty() || QUuid(object["registrationId"].toString()).isNull() ||
        !object["gameRecordRequired"].isBool() || object["registryIdentity"].toString().isEmpty() ||
        physicalIdentityV2(object["registry"].toString()) != object["registryIdentity"].toString()) {
        if (error) *error = "Unknown, incompatible or incomplete instance ownership.";
        return {};
    }
    return object;
}
bool validate(const QString& root, const QString& id, const QString& registry, QString* error)
{
    const auto object = binding(id, error);
    if (object.isEmpty() || physicalIdentityV2(root) != id || QFileInfo(registry).canonicalFilePath() != object["registry"].toString() ||
        read(root + "/.awake-integration/ownership.json", error) != withoutGame(object)) {
        if (error && error->isEmpty()) *error = "Enrollment and canonical ownership do not match.";
        return false;
    }
    return true;
}
namespace {
bool enrollLocked(const QString& root, const QString& id, const QString& registry, const QString& owner, const QString& registration, QString* error)
{
    const auto markerDirectory = root + "/.awake-integration";
    if (!createPrivateStorageDirectory(markerDirectory)) {
        if (error) *error = "The instance's coordination folder cannot be created.";
        return false;
    }
    const auto name = stem(id, error), marker = markerDirectory + "/ownership.json";
    QJsonObject expected{{"format", 1}, {"kernelMajor", 2}, {"published", true}, {"physicalId", id}, {"ownerProfile", owner},
        {"registrationId", registration}, {"registry", QFileInfo(registry).canonicalFilePath()}, {"registryIdentity", physicalIdentityV2(registry)}};
    if (expected["registryIdentity"].toString().isEmpty()) return false;
    if (QFileInfo::exists(name + ".json")) {
        const auto current = binding(id, error);
        if (withoutGame(current) != expected) return false;
        auto pending = expected; pending["published"] = false;
        const auto local = read(marker, error);
        if (local != expected && (local != pending || current["gameRecordRequired"] != false || !write(marker, expected, error))) return false;
        return validate(root, id, registry, error);
    }
    auto pending = expected; pending["published"] = false;
    // A lost published authority must never reset detached-game or recovery evidence.
    if (QFileInfo::exists(marker) && read(marker, error) != pending) return false;
    if (!write(marker, pending, error)) return false;
    auto global = expected; global["gameRecordRequired"] = false;
    return write(name + ".json", global, error) && write(marker, expected, error) && validate(root, id, registry, error);
}
}
bool enroll(const QString& root, const QString& id, const QString& registry, const QString& owner, const QString& registration, QString* error)
{
    if (owner.isEmpty() || owner.size() > 128 || QUuid(registration).isNull() || physicalIdentityV2(root) != id ||
        !profileAccessIsPrivate(root) || !createPrivateStorageDirectory(registry)) return false;
    const auto name = stem(id, error); if (name.isEmpty()) return false;
    auto held = lock(id, error); if (!held) return false;
    auto release = qScopeGuard([&] { if (held) CloseHandle(held); });
    const auto markerDirectory = root + "/.awake-integration";
    const bool fresh = !QFileInfo::exists(name + ".json") && QDir(markerDirectory).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    if (enrollLocked(root, id, registry, owner, registration, error)) return true;
    if (!fresh) return false;
    // The Launcher hides an instance that has any of these, so a first attempt that failed must not leave them.
    QDir(markerDirectory).removeRecursively();
    QFile::remove(name + ".json");
    CloseHandle(held); held = nullptr;
    QFile::remove(name + ".lock");
    return false;
}
bool clearAbandoned(const QString& root, QString* error)
{
    const auto directory = root + "/.awake-integration";
    if (QDir(directory).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot) != QStringList{"ownership.json"}) return false;
    QFile marker(directory + "/ownership.json");
    if (!marker.open(QIODevice::ReadOnly) || marker.size() > 16384) return false;
    const auto body = QJsonDocument::fromJson(marker.readAll()).object()["body"].toObject();
    marker.close();
    // Enrollment publishes before it returns, so an unpublished marker with no enrollment running was abandoned.
    const auto id = physicalIdentityV2(root), registration = body["registrationId"].toString();
    if (body["published"] != false || id.isEmpty() || body["physicalId"] != id || QUuid(registration).isNull()) return false;
    return unenroll(root, id, registration, error);
}
bool unenroll(const QString& root, const QString& id, const QString& registration, QString* error)
{
    const auto name = stem(id, error); if (name.isEmpty()) return false;
    auto held = lock(id, error); if (!held) return false;
    auto release = qScopeGuard([&] { if (held) CloseHandle(held); });
    const auto markerDirectory = root + "/.awake-integration", marker = markerDirectory + "/ownership.json";
    QFile existing(marker);
    if (existing.open(QIODevice::ReadOnly)) {
        const auto owner = QJsonDocument::fromJson(existing.read(16384)).object()["body"].toObject()["registrationId"].toString();
        existing.close();
        if (owner != registration) { if (error) *error = "Another registration owns this instance."; return false; }
    }
    if (QFileInfo::exists(markerDirectory) && !QDir(markerDirectory).removeRecursively()) { if (error) *error = "The coordination folder is in use."; return false; }
    if (QFileInfo::exists(name + ".json") && !QFile::remove(name + ".json")) { if (error) *error = "Ownership evidence cannot be removed."; return false; }
    CloseHandle(held); held = nullptr;
    if (!QFile::remove(name + ".lock")) { if (error) *error = "The instance lock cannot be removed."; return false; }
    return true;
}
bool requireGameRecord(const QString& id, QString* error)
{
    auto object = binding(id, error); if (object.isEmpty()) return false;
    object["gameRecordRequired"] = true;
    return write(stem(id, error) + ".json", object, error);
}
bool blocksNativeLoad(const QString& root)
{
    const auto parent = coordinationBasePath(); if (parent.isEmpty()) return true;
    const auto directory = parent + "/authority-v1";
    const auto attributes = [](const QString& path) {
        const auto native = "\\\\?\\" + QDir::toNativeSeparators(path);
        const auto flags = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
        return std::pair{flags, flags == INVALID_FILE_ATTRIBUTES ? GetLastError() : DWORD(ERROR_SUCCESS)};
    };
    const auto [flags, error] = attributes(directory);
    if (flags == INVALID_FILE_ATTRIBUTES) return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
    if (!(flags & FILE_ATTRIBUTE_DIRECTORY) || (flags & FILE_ATTRIBUTE_REPARSE_POINT) || !storageAccessIsPrivate(parent) || !storageAccessIsPrivate(directory)) return true;
    const auto id = physicalIdentityV2(root); if (id.isEmpty()) return true;
    const auto key = QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
    for (const auto* suffix : {".json", ".lock"}) {
        const auto [recordFlags, recordError] = attributes(directory + '/' + key + suffix);
        if (recordFlags != INVALID_FILE_ATTRIBUTES || (recordError != ERROR_FILE_NOT_FOUND && recordError != ERROR_PATH_NOT_FOUND)) return true;
    }
    return false;
}
}
