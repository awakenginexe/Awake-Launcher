// SPDX-License-Identifier: GPL-3.0-only
#include "PhysicalInstanceLease.h"
#include "InstanceDiscovery.h"
#include "ProfileAccess.h"
#include "GameProcessGroup.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <qt_windows.h>
namespace Awake::Integration {
PhysicalInstanceLease::PhysicalInstanceLease(QString root, QString id, QString registry, void* handle)
    : m_root(std::move(root)), m_physicalId(std::move(id)), m_registry(std::move(registry)), m_handle(handle) {}
std::shared_ptr<PhysicalInstanceLease> PhysicalInstanceLease::acquire(const QString& root, const QString& id,
                                                                   const QString& directory, QString* error)
{
    const auto reject = [error](const QString& reason) -> std::shared_ptr<PhysicalInstanceLease> { if (error) *error = reason; return {}; };
    const auto canonical = QFileInfo(root).canonicalFilePath();
    if (id.isEmpty() || physicalIdentityV2(canonical) != id || !profileAccessIsPrivate(canonical))
        return reject("The physical instance or its access control cannot be verified.");
    const QFileInfo before(directory);
    if (before.isSymLink() || before.isJunction() || !createPrivateStorageDirectory(directory))
        return reject("Private instance coordination storage is unavailable.");
    const auto key = QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
    const auto path = "\\\\?\\" + QDir::toNativeSeparators(QDir(QFileInfo(directory).canonicalFilePath()).filePath(key + ".lock"));
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return reject("The physical instance is busy or cannot be locked.");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || physicalIdentityV2(canonical) != id) {
        CloseHandle(handle);
        return reject("The physical instance lock identity changed.");
    }
    const auto game = GameProcessGroup::probe(id, directory, error);
    if (game == GameProcessGroup::State::Running || game == GameProcessGroup::State::Unknown) {
        CloseHandle(handle);
        return reject("A recorded game process is running or its state cannot be verified.");
    }
    return std::shared_ptr<PhysicalInstanceLease>(new PhysicalInstanceLease(canonical, id, QFileInfo(directory).canonicalFilePath(), handle));
}
bool PhysicalInstanceLease::pinRoot(QString* error)
{
    if (!pinAbsoluteDirectory(m_root, error) || physicalIdentityV2(m_root) != m_physicalId) {
        if (error && error->isEmpty()) *error = "The physical instance identity changed during pinning.";
        return false;
    }
    return true;
}
bool PhysicalInstanceLease::pinDirectory(const QString& path, QString* error)
{
    if (path == ".") return pinRoot(error);
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains('\\') || QDir::cleanPath(path) != path ||
        path == ".." || path.startsWith("../") || !pinRoot(error)) return false;
    return pinAbsoluteDirectory(m_root + '/' + path, error);
}
bool PhysicalInstanceLease::pinAbsoluteDirectory(const QString& directory, QString* error)
{
    auto position = QDir(directory).rootPath();
    const auto parts = directory.mid(position.size()).split('/', Qt::SkipEmptyParts);
    QStringList paths{position};
    for (const auto& part : parts) { position = QDir(position).filePath(part); paths.append(position); }
    for (const auto& name : paths) {
        if (m_pins.contains(name)) continue;
        const auto native = "\\\\?\\" + QDir::toNativeSeparators(name);
        // Attribute-only handles do not enforce Windows delete-sharing restrictions.
        const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &info) ||
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            const auto code = GetLastError();
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            if (error) *error = "Cannot pin directory " + name + " (Windows error " + QString::number(code) + ").";
            return false;
        }
        m_pins.insert(name, handle);
    }
    return true;
}
PhysicalInstanceLease::~PhysicalInstanceLease()
{
    for (const auto handle : m_pins) CloseHandle(handle);
    CloseHandle(m_handle);
}
}  // namespace Awake::Integration
