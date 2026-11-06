// SPDX-License-Identifier: GPL-3.0-only
#include "DurableIO.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QUuid>
#include <functional>
#include <qt_windows.h>
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
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(handle);
        fail(error, "Linked or nonregular files are not writable integration targets", ERROR_INVALID_DATA);
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
