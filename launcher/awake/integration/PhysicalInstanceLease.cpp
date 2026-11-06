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
    const auto reject = [error] { if (error) *error = "The physical instance directory cannot be pinned for replacement."; return false; };
    if (m_rootPin) return physicalIdentityV2(m_root) == m_physicalId || reject();
    const auto path = "\\\\?\\" + QDir::toNativeSeparators(m_root);
    // Attribute-only handles do not enforce Windows delete-sharing restrictions.
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return reject();
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        physicalIdentityV2(m_root) != m_physicalId) {
        CloseHandle(handle);
        return reject();
    }
    m_rootPin = handle;
    return true;
}
PhysicalInstanceLease::~PhysicalInstanceLease()
{
    if (m_rootPin) CloseHandle(m_rootPin);
    CloseHandle(m_handle);
}
}  // namespace Awake::Integration
