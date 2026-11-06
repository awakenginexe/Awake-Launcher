// SPDX-License-Identifier: GPL-3.0-only
#include "ProfileAccess.h"
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <aclapi.h>
#include <qt_windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace Awake::Integration {
bool profileAccessIsPrivate(const QString& directory)
{
    const auto path = QFileInfo(directory).canonicalFilePath();
    if (path.isEmpty() || !QFileInfo(path).isDir())
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
        if (header->AceFlags & INHERIT_ONLY_ACE)
            continue;
        if (header->AceType == ACCESS_DENIED_ACE_TYPE)
            continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            safe = false;
            break;
        }
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        auto* grantee = &ace->SidStart;
        const DWORD write = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE |
                            WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
        if ((ace->Mask & write) && !EqualSid(grantee, sid) && !EqualSid(grantee, system) && !EqualSid(grantee, admins))
            safe = false;
    }
    LocalFree(descriptor);
    return safe;
#else
    struct stat info{};
    return ::stat(QFile::encodeName(path).constData(), &info) == 0 && info.st_uid == geteuid() && !(info.st_mode & 0022);
#endif
}
}  // namespace Awake::Integration
