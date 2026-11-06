// SPDX-License-Identifier: GPL-3.0-only
#include "ProfileAccess.h"
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#ifdef Q_OS_WIN
#include <aclapi.h>
#include <qt_windows.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace Awake::Integration {
static bool accessIsPrivate(const QString& directory, bool privateRead, bool allowFiles = false, bool ownershipOnly = false)
{
    const auto path = QFileInfo(directory).canonicalFilePath();
    if (path.isEmpty() || (!QFileInfo(path).isDir() && !(allowFiles && QFileInfo(path).isFile())))
        return false;
#ifdef Q_OS_WIN
    const auto drive = QDir::toNativeSeparators(QDir(path).rootPath());
    if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(drive.utf16())) != DRIVE_FIXED)
        return false;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray user(int(size), Qt::Uninitialized);
    const bool gotUser = size > 0 && GetTokenInformation(token, TokenUser, user.data(), size, &size);
    CloseHandle(token);
    if (!gotUser)
        return false;
    auto* sid = reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
    const auto nativePath = QDir::toNativeSeparators(path);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PSID owner = nullptr;
    PACL acl = nullptr;
    if (GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(nativePath.utf16())), SE_FILE_OBJECT,
                              OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &acl, nullptr,
                              &descriptor) != ERROR_SUCCESS)
        return false;
    bool safe = owner && EqualSid(owner, sid) && acl;
    if (ownershipOnly) { LocalFree(descriptor); return safe; }
    BYTE system[SECURITY_MAX_SID_SIZE], admins[SECURITY_MAX_SID_SIZE];
    DWORD systemSize = sizeof(system), adminSize = sizeof(admins);
    safe = safe && CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &systemSize) &&
           CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins, &adminSize);
    for (DWORD i = 0; safe && i < acl->AceCount; ++i) {
        void* raw = nullptr;
        if (!GetAce(acl, i, &raw)) {
            safe = false;
            break;
        }
        auto* header = static_cast<ACE_HEADER*>(raw);
        if (!privateRead && (header->AceFlags & INHERIT_ONLY_ACE))
            continue;
        if (header->AceType == ACCESS_DENIED_ACE_TYPE)
            continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            safe = false;
            break;
        }
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        auto* grantee = &ace->SidStart;
        DWORD write = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE |
                            WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
        if (privateRead) write |= FILE_GENERIC_READ | FILE_GENERIC_EXECUTE | GENERIC_READ | GENERIC_EXECUTE;
        if ((ace->Mask & write) && !EqualSid(grantee, sid) && !EqualSid(grantee, system) && !EqualSid(grantee, admins))
            safe = false;
    }
    LocalFree(descriptor);
    return safe;
#else
    Q_UNUSED(privateRead)
    Q_UNUSED(ownershipOnly)
    struct stat info{};
    return ::stat(QFile::encodeName(path).constData(), &info) == 0 && info.st_uid == geteuid() && !(info.st_mode & 0022);
#endif
}
bool profileAccessIsPrivate(const QString& directory) { return accessIsPrivate(directory, false); }
bool storageAccessIsPrivate(const QString& path)
{
#ifdef Q_OS_WIN
    const QFileInfo info(path);
    return !info.isSymLink() && !info.isJunction() && accessIsPrivate(path, true, true);
#else
    Q_UNUSED(path)
    return false;
#endif
}
static bool createPrivateStorageDirectoryImpl(const QString& directory, bool knownUserDataParent)
{
#ifdef Q_OS_WIN
    const QFileInfo info(directory);
    if (info.exists() || info.isSymLink() || info.isJunction()) return storageAccessIsPrivate(directory) && info.isDir();
    if (!profileAccessIsPrivate(info.absolutePath()) && !(knownUserDataParent && accessIsPrivate(info.absolutePath(), false, false, true))) return false;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    auto close = qScopeGuard([&] { CloseHandle(token); });
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (!size) return false;
    QByteArray user(int(size), Qt::Uninitialized);
    if (!GetTokenInformation(token, TokenUser, user.data(), size, &size)) return false;
    auto* sid = reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
    BYTE system[SECURITY_MAX_SID_SIZE], admins[SECURITY_MAX_SID_SIZE];
    DWORD systemSize = sizeof(system), adminSize = sizeof(admins);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &systemSize) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins, &adminSize)) return false;
    EXPLICIT_ACCESSW entries[3]{};
    PSID trustees[3]{sid, system, admins};
    for (int index = 0; index < 3; ++index) {
        entries[index].grfAccessPermissions = FILE_ALL_ACCESS;
        entries[index].grfAccessMode = SET_ACCESS;
        entries[index].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        entries[index].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entries[index].Trustee.ptstrName = reinterpret_cast<LPWSTR>(trustees[index]);
    }
    PACL acl = nullptr;
    if (SetEntriesInAclW(3, entries, nullptr, &acl) != ERROR_SUCCESS) return false;
    auto free = qScopeGuard([&] { LocalFree(acl); });
    SECURITY_DESCRIPTOR descriptor{};
    if (!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) || !SetSecurityDescriptorOwner(&descriptor, sid, FALSE) ||
        !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) ||
        !SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) return false;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};
    const auto native = "\\\\?\\" + QDir::toNativeSeparators(info.absoluteFilePath());
    if (!CreateDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()), &attributes))
        return GetLastError() == ERROR_ALREADY_EXISTS && storageAccessIsPrivate(directory);
    if (SetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(native.utf16())), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr) != ERROR_SUCCESS) return false;
    return storageAccessIsPrivate(directory);
#else
    Q_UNUSED(directory)
    Q_UNUSED(knownUserDataParent)
    return false;
#endif
}
bool createPrivateStorageDirectory(const QString& directory)
{
    return createPrivateStorageDirectoryImpl(directory, false);
}
QString privateCoordinationBase()
{
#ifdef Q_OS_WIN
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &value))) return {};
    const auto parent = QFileInfo(QString::fromWCharArray(value)).canonicalFilePath(); CoTaskMemFree(value);
    if (parent.isEmpty()) return {};
    // The OS-selected parent can have capability ACEs; the new child has a protected user-only DACL.
    const auto directory = parent + "/AwakeSharedCore";
    if (!createPrivateStorageDirectoryImpl(directory, true)) return {};
    return QFileInfo(directory).canonicalFilePath();
#else
    return {};
#endif
}
}  // namespace Awake::Integration
