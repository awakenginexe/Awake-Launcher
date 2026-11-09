// SPDX-License-Identifier: GPL-3.0-only
#include "DurableIO.h"
#include <QHash>
#include <QMutex>
#include <atomic>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSet>
#include <QUuid>
#include <functional>
#include <qt_windows.h>
#include <aclapi.h>
namespace Awake::Integration::DurableIO {
namespace {
QString native(const QString& path)
{
    auto result = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    return result.startsWith("\\\\") ? "\\\\?\\UNC\\" + result.mid(2) : "\\\\?\\" + result;
}
LPCWSTR wide(const QString& path) { return reinterpret_cast<LPCWSTR>(path.utf16()); }
bool fail(QString* error, const QString& reason, DWORD code = GetLastError())
{
    if (error) *error = reason + " (Windows error " + QString::number(code) + ")";
    return false;
}
bool defaultAccess(HANDLE file, const QString& path, bool moved = false, bool restore = false)
{
    PSECURITY_DESCRIPTOR actual = nullptr, parent = nullptr, expected = nullptr;
    HANDLE token = nullptr;
    auto cleanup = qScopeGuard([&] {
        if (actual) LocalFree(actual);
        if (parent) LocalFree(parent);
        if (expected) DestroyPrivateObjectSecurity(&expected);
        if (token) CloseHandle(token);
    });
    const auto information = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    auto parentPath = native(QFileInfo(path).absolutePath());
    if (GetSecurityInfo(file, SE_FILE_OBJECT, information, nullptr, nullptr, nullptr, nullptr, &actual) != ERROR_SUCCESS ||
        GetNamedSecurityInfoW(reinterpret_cast<LPWSTR>(parentPath.data()), SE_FILE_OBJECT, information,
            nullptr, nullptr, nullptr, nullptr, &parent) != ERROR_SUCCESS ||
        !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    if (!CreatePrivateObjectSecurityEx(parent, nullptr, &expected, nullptr, FALSE, SEF_DACL_AUTO_INHERIT, token, &mapping)) return false;
    PSID actualOwner = nullptr, expectedOwner = nullptr, actualGroup = nullptr, expectedGroup = nullptr;
    BOOL ignored = FALSE, actualPresent = FALSE, expectedPresent = FALSE;
    PACL actualAcl = nullptr, expectedAcl = nullptr;
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
    if (!GetSecurityDescriptorOwner(actual, &actualOwner, &ignored) || !GetSecurityDescriptorOwner(expected, &expectedOwner, &ignored) ||
        !GetSecurityDescriptorGroup(actual, &actualGroup, &ignored) || !GetSecurityDescriptorGroup(expected, &expectedGroup, &ignored) ||
        !actualOwner || !expectedOwner || !actualGroup || !expectedGroup ||
        !EqualSid(actualOwner, expectedOwner) || !EqualSid(actualGroup, expectedGroup) ||
        !GetSecurityDescriptorControl(actual, &control, &revision) || (!moved && (control & SE_DACL_PROTECTED)) ||
        !GetSecurityDescriptorDacl(actual, &actualPresent, &actualAcl, &ignored) ||
        !GetSecurityDescriptorDacl(expected, &expectedPresent, &expectedAcl, &ignored) ||
        !actualPresent || !expectedPresent || !actualAcl || !expectedAcl || actualAcl->AceCount != expectedAcl->AceCount) return false;
    QSet<DWORD> matched;
    for (DWORD index = 0; index < actualAcl->AceCount; ++index) {
        void* left = nullptr; void* right = nullptr;
        if (!GetAce(actualAcl, index, &left)) return false;
        const auto size = static_cast<ACE_HEADER*>(left)->AceSize;
        bool found = false;
        for (DWORD candidate = 0; candidate < expectedAcl->AceCount; ++candidate) {
            if (matched.contains(candidate) || (!moved && candidate != index) || !GetAce(expectedAcl, candidate, &right) ||
                size != static_cast<ACE_HEADER*>(right)->AceSize) continue;
            QByteArray a(static_cast<const char*>(left), size), b(static_cast<const char*>(right), size);
            if (moved) {
                reinterpret_cast<ACE_HEADER*>(a.data())->AceFlags &= ~INHERITED_ACE;
                reinterpret_cast<ACE_HEADER*>(b.data())->AceFlags &= ~INHERITED_ACE;
            }
            if (a == b) { matched.insert(candidate); found = true; break; }
        }
        if (!found) return false;
    }
    if (restore && SetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                                   nullptr, nullptr, expectedAcl, nullptr) != ERROR_SUCCESS) return false;
    return true;
}
bool singleStream(const QString& path)
{
    WIN32_FIND_STREAM_DATA stream{};
    const auto name = native(path);
    const auto search = FindFirstStreamW(wide(name), FindStreamInfoStandard, &stream, 0);
    if (search == INVALID_HANDLE_VALUE) return false;
    auto close = qScopeGuard([&] { FindClose(search); });
    return QString::fromWCharArray(stream.cStreamName) == "::$DATA" && !FindNextStreamW(search, &stream) && GetLastError() == ERROR_HANDLE_EOF;
}
// Files take these from their folder and they do not change how a file reads or moves. The Launcher's staging
// folder is marked not-indexed, so every file of a pack installed through it carries that attribute.
constexpr DWORD harmlessAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_COMPRESSED;
QString refusal(HANDLE handle, const QString& path, const QString& accessReference)
{
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) return "its details cannot be read";
    if (info.nNumberOfLinks != 1) return "it is hard-linked";
    if (const auto other = info.dwFileAttributes & ~harmlessAttributes) return QString("it has special attributes (0x%1)").arg(other, 0, 16);
    if (!defaultAccess(handle, path) && (accessReference.isEmpty() || !defaultAccess(handle, accessReference, true))) return "it has custom permissions";
    if (!singleStream(path)) return "it has alternate data streams";
    return {};
}
HANDLE read(const QString& path, QString* error, DWORD access = GENERIC_READ, const QString& accessReference = {})
{
    const auto name = native(path);
    const auto handle = CreateFileW(wide(name), access, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        fail(error, "Cannot open file for a stable read");
        return handle;
    }
    if (const auto reason = refusal(handle, path, accessReference); !reason.isEmpty()) {
        CloseHandle(handle);
        fail(error, "Cannot safely use " + QFileInfo(path).fileName() + ": " + reason, ERROR_NOT_SUPPORTED);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}
bool replace(const QString& path, const std::function<bool(HANDLE)>& writer, QString* error)
{
    const auto target = native(path);
    const auto attributes = GetFileAttributesW(wide(target));
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        const auto existing = read(path, error);
        if (existing == INVALID_HANDLE_VALUE) return false;
        CloseHandle(existing);
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
        return fail(error, "Cannot inspect replacement target");
    }
    const auto temporary = native(path + ".tmp-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto handle = CreateFileW(wide(temporary), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return fail(error, "Cannot create durable replacement");
    bool closed = false;
    auto cleanup = qScopeGuard([&] {
        if (!closed) CloseHandle(handle);
        DeleteFileW(wide(temporary));
    });
    if (!writer(handle)) return fail(error, "Replacement write failed");
    if (!FlushFileBuffers(handle)) return fail(error, "Replacement flush failed");
    if (!CloseHandle(handle)) {
        closed = true;
        return fail(error, "Replacement close failed");
    }
    closed = true;
    if (!MoveFileExW(wide(temporary), wide(target), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return fail(error, "Replacement rename failed: " + path);
    return true;
}
bool writeBytes(HANDLE handle, const char* data, qsizetype size)
{
    qsizetype offset = 0;
    while (offset < size) {
        DWORD written = 0;
        const auto amount = DWORD(qMin<qsizetype>(size - offset, 1024 * 1024));
        if (!WriteFile(handle, data + offset, amount, &written, nullptr) || !written) return false;
        offset += written;
    }
    return true;
}
FileState state(HANDLE handle, QString* error)
{
    FILE_ID_INFO id{};
    LARGE_INTEGER beginning{};
    if (!GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id)) ||
        !SetFilePointerEx(handle, beginning, nullptr, FILE_BEGIN)) { fail(error, "Cannot identify file"); return {}; }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    QByteArray buffer(1024 * 1024, Qt::Uninitialized); DWORD count = 0;
    while (ReadFile(handle, buffer.data(), DWORD(buffer.size()), &count, nullptr)) {
        if (!count) return {QString::fromLatin1(digest.result().toHex()),
            QString("file1:%1:%2").arg(id.VolumeSerialNumber, 16, 16, QChar('0'))
                .arg(QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(id.FileId.Identifier), 16).toHex()))};
        digest.addData(QByteArrayView(buffer.constData(), count));
    }
    fail(error, "File hash read failed"); return {};
}
bool matches(const FileState& actual, const FileState& expected)
{ return !actual.hash.isEmpty() && actual.hash == expected.hash && actual.identity == expected.identity; }
bool renameHandle(HANDLE handle, const QString& destination, QString* error)
{
    const auto path = native(destination);
    QByteArray storage(sizeof(FILE_RENAME_INFO) + path.size() * sizeof(wchar_t), 0);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    info->ReplaceIfExists = FALSE;
    info->FileNameLength = DWORD(path.size() * sizeof(wchar_t));
    memcpy(info->FileName, path.utf16(), info->FileNameLength);
    if (!SetFileInformationByHandle(handle, FileRenameInfo, info, DWORD(storage.size())))
        return fail(error, "Cannot move file without overwriting existing data");
    return FlushFileBuffers(handle) || fail(error, "Cannot flush moved file");
}
}  // namespace
FileState fingerprint(const QString& path, QString* error, const QString& accessReference)
{
    const auto name = native(path);
    if (GetFileAttributesW(wide(name)) == INVALID_FILE_ATTRIBUTES &&
        (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) return {"missing", "missing"};
    const auto handle = read(path, error, GENERIC_READ, accessReference);
    if (handle == INVALID_HANDLE_VALUE) return {};
    auto close = qScopeGuard([&] { CloseHandle(handle); });
    return state(handle, error);
}
bool stage(const QString& source, const QString& incoming, const QString& expectedHash,
           const std::function<bool(const QString&)>& recordIdentity, QString* error)
{
    const auto input = read(source, error);
    if (input == INVALID_HANDLE_VALUE) return false;
    auto closeInput = qScopeGuard([&] { CloseHandle(input); });
    const auto name = native(incoming);
    const auto output = CreateFileW(wide(name), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr,
                                    CREATE_NEW, FILE_FLAG_WRITE_THROUGH, nullptr);
    if (output == INVALID_HANDLE_VALUE) return fail(error, "Cannot create private incoming file");
    auto closeOutput = qScopeGuard([&] { CloseHandle(output); });
    const auto identity = state(output, error).identity;
    if (identity.isEmpty() || !recordIdentity(identity)) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(output, FileDispositionInfo, &disposition, sizeof(disposition));
        return false;
    }
    QByteArray buffer(1024 * 1024, Qt::Uninitialized); DWORD count = 0;
    for (;;) {
        if (!ReadFile(input, buffer.data(), DWORD(buffer.size()), &count, nullptr)) return fail(error, "Cannot read staged artifact");
        if (!count) break;
        if (!writeBytes(output, buffer.constData(), count)) return fail(error, "Cannot copy incoming artifact");
    }
    if (!FlushFileBuffers(output)) return fail(error, "Cannot flush incoming artifact");
    return state(output, error).hash == expectedHash || fail(error, "Incoming artifact hash mismatch", ERROR_INVALID_DATA);
}
bool exchange(const QString& source, const FileState& sourceState, const QString& destination,
              const FileState& expected, const QString& displaced, QString* error, const std::function<bool()>& afterDisplace)
{
    HANDLE input = INVALID_HANDLE_VALUE, target = INVALID_HANDLE_VALUE;
    auto close = qScopeGuard([&] { if (input != INVALID_HANDLE_VALUE) CloseHandle(input); if (target != INVALID_HANDLE_VALUE) CloseHandle(target); });
    if (!source.isEmpty()) {
        input = read(source, error, GENERIC_READ | GENERIC_WRITE | DELETE | WRITE_DAC, destination);
        if (input == INVALID_HANDLE_VALUE || !matches(state(input, error), sourceState))
            return fail(error, "Replacement source changed", ERROR_INVALID_DATA);
    }
    if (expected.hash != "missing") {
        target = read(destination, error, GENERIC_READ | GENERIC_WRITE | DELETE);
        if (target == INVALID_HANDLE_VALUE || !matches(state(target, error), expected))
            return fail(error, "Live target changed; preserving its bytes", ERROR_INVALID_DATA);
        if (!renameHandle(target, displaced, error)) return false;
    } else {
        if (!matches(fingerprint(destination, error), expected)) return fail(error, "New target already exists", ERROR_ALREADY_EXISTS);
    }
    if (afterDisplace && !afterDisplace()) return false;
    if (source.isEmpty()) return true;
    if (!renameHandle(input, destination, error)) return false;
    // Moving between protected parents can turn inherited ACEs into explicit ACEs.
    return defaultAccess(input, destination, true, true) && FlushFileBuffers(input);
}
bool discardIncoming(const QString& incoming, const QString& identity, const QString& staged, QString* error)
{
    if (fingerprint(incoming, error).hash == "missing") return true;
    const auto input = read(incoming, error, GENERIC_READ | DELETE);
    if (input == INVALID_HANDLE_VALUE) return false;
    auto closeInput = qScopeGuard([&] { CloseHandle(input); });
    if (identity.isEmpty() || state(input, error).identity != identity) return fail(error, "Unrecognized incoming file preserved", ERROR_INVALID_DATA);
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(input, start, nullptr, FILE_BEGIN)) return false;
    const auto original = read(staged, error);
    if (original == INVALID_HANDLE_VALUE) return false;
    auto closeOriginal = qScopeGuard([&] { CloseHandle(original); });
    QByteArray left(1024 * 1024, Qt::Uninitialized), right(left.size(), Qt::Uninitialized);
    for (;;) {
        DWORD count = 0, expected = 0;
        if (!ReadFile(input, left.data(), DWORD(left.size()), &count, nullptr)) return fail(error, "Cannot verify incoming file");
        if (!count) break;
        if (!ReadFile(original, right.data(), count, &expected, nullptr) || count != expected || memcmp(left.data(), right.data(), count))
            return fail(error, "Changed incoming bytes preserved", ERROR_INVALID_DATA);
    }
    FILE_DISPOSITION_INFO disposition{TRUE};
    return SetFileInformationByHandle(input, FileDispositionInfo, &disposition, sizeof(disposition)) || fail(error, "Cannot discard verified incoming file");
}
bool restoreAccess(const QString& path, const FileState& expected, QString* error)
{
    if (expected.hash == "missing") return fingerprint(path, error).hash == "missing";
    const auto handle = read(path, error, GENERIC_READ | GENERIC_WRITE | WRITE_DAC, path);
    if (handle == INVALID_HANDLE_VALUE) return false;
    auto close = qScopeGuard([&] { CloseHandle(handle); });
    return matches(state(handle, error), expected) && defaultAccess(handle, path, true, true) && FlushFileBuffers(handle);
}
bool write(const QString& path, const QByteArray& bytes, QString* error)
{
    return replace(path, [&bytes](HANDLE handle) { return writeBytes(handle, bytes.constData(), bytes.size()); }, error);
}
bool copy(const QString& source, const QString& destination, QString* error)
{
    const auto input = read(source, error);
    if (input == INVALID_HANDLE_VALUE) return false;
    auto close = qScopeGuard([&] { CloseHandle(input); });
    return replace(destination, [input](HANDLE output) {
        QByteArray buffer(1024 * 1024, Qt::Uninitialized);
        DWORD count = 0;
        while (ReadFile(input, buffer.data(), DWORD(buffer.size()), &count, nullptr)) {
            if (!count) return true;
            if (!writeBytes(output, buffer.constData(), count)) return false;
        }
        return false;
    }, error);
}
namespace {
struct CachedHash { DWORD volume, indexHigh, indexLow, sizeHigh, sizeLow, writeHigh, writeLow; QString value; };
std::atomic_bool hashCacheEnabled{false};
QMutex hashCacheLock;
QHash<QString, CachedHash> hashCache;
}
void setHashCacheEnabled(bool enabled) { hashCacheEnabled.store(enabled); }
QString hash(const QString& path, QString* error, QCryptographicHash::Algorithm algorithm)
{
    const auto name = native(path);
    if (GetFileAttributesW(wide(name)) == INVALID_FILE_ATTRIBUTES &&
        (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) return "missing";
    const auto handle = read(path, error);
    if (handle == INVALID_HANDLE_VALUE) return {};
    auto close = qScopeGuard([&] { CloseHandle(handle); });
    BY_HANDLE_FILE_INFORMATION info{};
    const bool cacheable = hashCacheEnabled.load() && GetFileInformationByHandle(handle, &info);
    const auto key = name + QLatin1Char('|') + QString::number(int(algorithm));
    if (cacheable) {
        QMutexLocker locker(&hashCacheLock);
        const auto it = hashCache.constFind(key);
        if (it != hashCache.constEnd() && it->volume == info.dwVolumeSerialNumber && it->indexHigh == info.nFileIndexHigh && it->indexLow == info.nFileIndexLow &&
            it->sizeHigh == info.nFileSizeHigh && it->sizeLow == info.nFileSizeLow && it->writeHigh == info.ftLastWriteTime.dwHighDateTime && it->writeLow == info.ftLastWriteTime.dwLowDateTime)
            return it->value;
    }
    QCryptographicHash digest(algorithm);
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    DWORD count = 0;
    while (ReadFile(handle, buffer.data(), DWORD(buffer.size()), &count, nullptr)) {
        if (!count) {
            const auto value = QString::fromLatin1(digest.result().toHex());
            if (cacheable) {
                QMutexLocker locker(&hashCacheLock);
                if (hashCache.size() >= 200000) hashCache.clear();
                hashCache.insert(key, {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow, info.nFileSizeHigh, info.nFileSizeLow, info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime, value});
            }
            return value;
        }
        digest.addData(QByteArrayView(buffer.constData(), count));
    }
    fail(error, "File hash read failed");
    return {};
}
bool remove(const QString& path, QString* error)
{
    const auto before = hash(path, error);
    if (before == "missing") return true;
    if (before.isEmpty()) return false;
    const auto name = native(path);
    return DeleteFileW(wide(name)) || fail(error, "File removal failed");
}
}  // namespace Awake::Integration::DurableIO
