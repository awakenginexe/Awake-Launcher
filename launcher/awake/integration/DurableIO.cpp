// SPDX-License-Identifier: GPL-3.0-only
#include "DurableIO.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
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
bool defaultAccess(HANDLE file, const QString& path)
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
        !GetSecurityDescriptorControl(actual, &control, &revision) || (control & SE_DACL_PROTECTED) ||
        !GetSecurityDescriptorDacl(actual, &actualPresent, &actualAcl, &ignored) ||
        !GetSecurityDescriptorDacl(expected, &expectedPresent, &expectedAcl, &ignored) ||
        !actualPresent || !expectedPresent || !actualAcl || !expectedAcl || actualAcl->AceCount != expectedAcl->AceCount) return false;
    for (DWORD index = 0; index < actualAcl->AceCount; ++index) {
        void* left = nullptr; void* right = nullptr;
        if (!GetAce(actualAcl, index, &left) || !GetAce(expectedAcl, index, &right)) return false;
        const auto size = static_cast<ACE_HEADER*>(left)->AceSize;
        if (size != static_cast<ACE_HEADER*>(right)->AceSize || memcmp(left, right, size)) return false;
    }
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
HANDLE read(const QString& path, QString* error)
{
    const auto name = native(path);
    const auto handle = CreateFileW(wide(name), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        fail(error, "Cannot open file for a stable read");
        return handle;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & ~(FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_ARCHIVE)) || !defaultAccess(handle, path) || !singleStream(path)) {
        CloseHandle(handle);
        fail(error, "Linked files, alternate streams, custom permissions or special attributes are unsupported integration targets", ERROR_NOT_SUPPORTED);
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
        return fail(error, "Replacement rename failed");
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
}  // namespace
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
QString hash(const QString& path, QString* error)
{
    const auto name = native(path);
    if (GetFileAttributesW(wide(name)) == INVALID_FILE_ATTRIBUTES &&
        (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) return "missing";
    const auto handle = read(path, error);
    if (handle == INVALID_HANDLE_VALUE) return {};
    auto close = qScopeGuard([&] { CloseHandle(handle); });
    QCryptographicHash digest(QCryptographicHash::Sha256);
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    DWORD count = 0;
    while (ReadFile(handle, buffer.data(), DWORD(buffer.size()), &count, nullptr)) {
        if (!count) return QString::fromLatin1(digest.result().toHex());
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
