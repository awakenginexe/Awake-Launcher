// SPDX-License-Identifier: GPL-3.0-only
#include "GameProcessGroup.h"
#include "DurableIO.h"
#include "PhysicalInstanceLease.h"
#include "ProfileAccess.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QUuid>
#include <qt_windows.h>
#include <sddl.h>
namespace Awake::Integration {
namespace {
QString key(const QString& id) { return QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()); }
QString path(const QString& directory, const QString& id) { return directory + "/games/" + key(id) + ".json"; }
QString certificatePath(const QString& directory, const QString& id, const QString& nonce)
{
    return directory + "/games/" + key(id) + '-' + nonce + ".done";
}
QString checksum(const QJsonObject& body)
{
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(body).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
QString userSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    auto close = qScopeGuard([&] { CloseHandle(token); });
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (!size) return {};
    QByteArray data(int(size), Qt::Uninitialized);
    if (!GetTokenInformation(token, TokenUser, data.data(), size, &size)) return {};
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &text)) return {};
    const auto result = QString::fromWCharArray(text); LocalFree(text); return result;
}
}  // namespace
struct GameProcessGroup::Data {
    std::shared_ptr<PhysicalInstanceLease> lease;
    HANDLE job = nullptr;
    HANDLE released = nullptr;
    QByteArray attributes;
    STARTUPINFOEXW startup{};
    bool initialized = false;
    ~Data()
    {
        if (released) { SetEvent(released); CloseHandle(released); }
        if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
        if (job) CloseHandle(job);
    }
};
GameProcessGroup::GameProcessGroup(std::unique_ptr<Data> data) : m_data(std::move(data)) {}
GameProcessGroup::~GameProcessGroup() = default;
GameProcessGroup::State GameProcessGroup::probe(const QString& id, const QString& directory, QString* error)
{
    const auto unknown = [error](const QString& message) { if (error) *error = message; return State::Unknown; };
    if (!id.startsWith("win2:")) return unknown("Strong physical identity is required for game ownership.");
    const auto record = path(directory, id);
    if (QFileInfo::exists(directory) && !storageAccessIsPrivate(directory)) return unknown("Private coordination storage cannot be verified.");
    if (QFileInfo::exists(directory + "/games") && !storageAccessIsPrivate(directory + "/games")) return unknown("Private game storage cannot be verified.");
    if (!QFileInfo::exists(record)) {
        if (QFileInfo(record).isSymLink() || QFileInfo(record).isJunction()) return unknown("Linked game ownership record.");
        return State::Untracked;
    }
    if (!storageAccessIsPrivate(directory) || !storageAccessIsPrivate(directory + "/games") || !storageAccessIsPrivate(record) ||
        DurableIO::hash(record).isEmpty()) return unknown("Private game ownership cannot be verified.");
    QFile file(record);
    if (!file.open(QIODevice::ReadOnly)) return unknown("Cannot read game ownership.");
    const auto bytes = file.read(16385);
    if (bytes.size() > 16384 || file.error() != QFileDevice::NoError) return unknown("Invalid game ownership size.");
    const auto envelope = QJsonDocument::fromJson(bytes).object(), body = envelope["body"].toObject();
    const auto sid = userSid(), name = body["jobName"].toString();
    const auto prefix = "Global\\AwakeLauncherGame-" + sid + '-';
    const auto nonce = name.mid(prefix.size());
    if (envelope.size() != 2 || envelope["sha256"].toString() != checksum(body) || body.size() != 3 || body["format"].toInt() != 1 ||
        body["physicalId"] != id || sid.isEmpty() || !name.startsWith(prefix) ||
        QUuid(nonce).isNull() || QUuid(nonce).toString(QUuid::WithoutBraces) != nonce)
        return unknown("Corrupt game ownership record.");
    const auto job = OpenJobObjectW(JOB_OBJECT_QUERY, FALSE, reinterpret_cast<LPCWSTR>(name.utf16()));
    if (!job) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) return unknown("Recorded job cannot be inspected.");
        const auto certificate = certificatePath(directory, id, nonce);
        if (!storageAccessIsPrivate(certificate) || DurableIO::hash(certificate).isEmpty())
            return unknown("Game ownership was interrupted without a verified completion certificate.");
        QFile finished(certificate);
        if (!finished.open(QIODevice::ReadOnly) || finished.read(100) != nonce.toUtf8() || !finished.atEnd())
            return unknown("Game completion certificate is invalid.");
        return State::Inactive;
    }
    auto close = qScopeGuard([&] { CloseHandle(job); });
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) ||
        !QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr) ||
        (limits.BasicLimitInformation.LimitFlags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)))
        return unknown("Recorded job accounting or descendant ownership cannot be verified.");
    return accounting.ActiveProcesses ? State::Running : State::Inactive;
}
std::shared_ptr<GameProcessGroup> GameProcessGroup::create(const std::shared_ptr<PhysicalInstanceLease>& lease, QString* error)
{
    const auto reject = [error](const QString& message) -> std::shared_ptr<GameProcessGroup> { if (error) *error = message; return {}; };
    if (!lease) return reject("An exclusive physical-instance lease is required for launch.");
    if (!lease->pinRoot(error)) return {};
    const auto state = probe(lease->physicalId(), lease->registryDirectory(), error);
    if (state == State::Running || state == State::Unknown) return reject("Existing game ownership is busy or unverifiable.");
    const auto sid = userSid();
    if (sid.isEmpty() || !createPrivateStorageDirectory(lease->registryDirectory() + "/games")) return reject("Private game ownership storage is unavailable.");
    const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto name = "Global\\AwakeLauncherGame-" + sid + '-' + nonce;
    const auto sddl = "O:" + sid + "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" + sid + ')';
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr))
        return reject("Private job access control is unavailable.");
    auto free = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    auto data = std::make_unique<Data>(); data->lease = lease;
    data->job = CreateJobObjectW(&security, reinterpret_cast<LPCWSTR>(name.utf16()));
    if (!data->job || GetLastError() == ERROR_ALREADY_EXISTS) return reject("Cannot create unique game ownership.");
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    if (!size || size > 65536) return reject("Process creation attributes are unavailable.");
    data->attributes.resize(qsizetype(size));
    data->startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(data->attributes.data());
    if (!InitializeProcThreadAttributeList(data->startup.lpAttributeList, 1, 0, &size)) return reject("Cannot initialize process ownership attributes.");
    data->initialized = true;
    if (!UpdateProcThreadAttribute(data->startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &data->job, sizeof(HANDLE), nullptr, nullptr))
        return reject("Atomic process ownership is unavailable on this Windows version.");
    data->released = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!data->released) return reject("Cannot create game ownership lifetime signal.");
    const auto certificate = "\\\\?\\" + QDir::toNativeSeparators(certificatePath(lease->registryDirectory(), lease->physicalId(), nonce) + ".pending");
    const auto output = CreateFileW(reinterpret_cast<LPCWSTR>(certificate.utf16()), GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        CREATE_NEW, FILE_FLAG_WRITE_THROUGH, nullptr);
    if (output == INVALID_HANDLE_VALUE) return reject("Cannot create game completion storage.");
    auto closeOutput = qScopeGuard([&] { CloseHandle(output); });
    // Retain a query handle in a per-game keeper: Windows can remove the job name when the last handle closes.
    const auto parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    const auto ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto closeSignals = qScopeGuard([&] { if (parent) CloseHandle(parent); if (ready) CloseHandle(ready); });
    if (!parent || !ready) return reject("Cannot create keeper startup signals.");
    HANDLE handles[5]{};
    auto closeInherited = qScopeGuard([&] { for (const auto handle : handles) if (handle) CloseHandle(handle); });
    const HANDLE sources[5]{data->job, data->released, parent, output, ready};
    const DWORD access[5]{JOB_OBJECT_QUERY, SYNCHRONIZE, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, GENERIC_WRITE | DELETE, EVENT_MODIFY_STATE};
    for (int index = 0; index < 5; ++index)
        if (!DuplicateHandle(GetCurrentProcess(), sources[index], GetCurrentProcess(), &handles[index], access[index], TRUE, 0))
            return reject("Cannot restrict keeper handles.");
    SIZE_T keeperSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &keeperSize);
    QByteArray keeperAttributes(qsizetype(keeperSize), Qt::Uninitialized);
    STARTUPINFOEXW keeperStartup{}; keeperStartup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    keeperStartup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(keeperAttributes.data());
    if (!InitializeProcThreadAttributeList(keeperStartup.lpAttributeList, 1, 0, &keeperSize)) return reject("Cannot initialize keeper handles.");
    auto destroyAttributes = qScopeGuard([&] { DeleteProcThreadAttributeList(keeperStartup.lpAttributeList); });
    if (!UpdateProcThreadAttribute(keeperStartup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr))
        return reject("Cannot restrict keeper handle inheritance.");
    const auto executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    auto command = '"' + executable + "\" --integration-game-keeper";
    for (const auto handle : handles) command += ' ' + QString::number(quintptr(handle));
    command += ' ' + nonce;
    PROCESS_INFORMATION keeper{};
    if (!CreateProcessW(reinterpret_cast<LPCWSTR>(executable.utf16()), reinterpret_cast<LPWSTR>(command.data()), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &keeperStartup.StartupInfo, &keeper))
        return reject("Cannot start the game ownership keeper.");
    auto closeKeeper = qScopeGuard([&] { CloseHandle(keeper.hThread); CloseHandle(keeper.hProcess); });
    const HANDLE startup[2]{ready, keeper.hProcess};
    if (WaitForMultipleObjects(2, startup, FALSE, 5000) != WAIT_OBJECT_0) return reject("Game ownership keeper did not become ready.");
    const QJsonObject body{{"format", 1}, {"physicalId", lease->physicalId()}, {"jobName", name}};
    if (!DurableIO::write(path(lease->registryDirectory(), lease->physicalId()),
            QJsonDocument(QJsonObject{{"body", body}, {"sha256", checksum(body)}}).toJson(QJsonDocument::Compact), error)) return {};
    return std::shared_ptr<GameProcessGroup>(new GameProcessGroup(std::move(data)));
}
bool GameProcessGroup::attach(QProcess& process, QString* error)
{
    if (process.state() != QProcess::NotRunning || process.createProcessArgumentsModifier()) {
        if (error) *error = "Process ownership requires an idle process with no other creation modifier.";
        return false;
    }
    process.setCreateProcessArgumentsModifier([self = shared_from_this()](QProcess::CreateProcessArguments* arguments) {
        self->m_data->startup.StartupInfo = *arguments->startupInfo;
        self->m_data->startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        arguments->startupInfo = &self->m_data->startup.StartupInfo;
        arguments->flags |= EXTENDED_STARTUPINFO_PRESENT;
    });
    return true;
}
int GameProcessGroup::runKeeper(int argc, char** argv)
{
    if (argc < 2 || QByteArray(argv[1]) != "--integration-game-keeper") return -1;
    if (argc != 8) return 61;
    HANDLE handles[5]{};
    for (int index = 0; index < 5; ++index) {
        bool valid = false;
        const auto number = QString::fromLatin1(argv[index + 2]).toULongLong(&valid);
        handles[index] = reinterpret_cast<HANDLE>(quintptr(number));
        DWORD flags = 0;
        if (!valid || !number || handles[index] == INVALID_HANDLE_VALUE || !GetHandleInformation(handles[index], &flags)) return 62;
        SetHandleInformation(handles[index], HANDLE_FLAG_INHERIT, 0);
    }
    auto close = qScopeGuard([&] { for (const auto handle : handles) CloseHandle(handle); });
    const auto nonce = QString::fromLatin1(argv[7]);
    if (QUuid(nonce).isNull() || QUuid(nonce).toString(QUuid::WithoutBraces) != nonce ||
        GetFileType(handles[3]) != FILE_TYPE_DISK || !GetProcessId(handles[2])) return 63;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    if (!QueryInformationJobObject(handles[0], JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) ||
        !SetEvent(handles[4])) return 64;
    const HANDLE lifetime[2]{handles[1], handles[2]};
    if (WaitForMultipleObjects(2, lifetime, FALSE, INFINITE) == WAIT_FAILED) return 65;
    while (true) {
        if (!QueryInformationJobObject(handles[0], JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr)) return 66;
        if (!accounting.ActiveProcesses) break;
        Sleep(100);
    }
    const auto bytes = nonce.toUtf8(); DWORD written = 0;
    if (!WriteFile(handles[3], bytes.constData(), DWORD(bytes.size()), &written, nullptr) || written != DWORD(bytes.size()) ||
        !FlushFileBuffers(handles[3])) return 67;
    const auto needed = GetFinalPathNameByHandleW(handles[3], nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!needed || needed > 32768) return 68;
    QString pending; pending.resize(needed);
    const auto count = GetFinalPathNameByHandleW(handles[3], reinterpret_cast<LPWSTR>(pending.data()), needed, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count || count >= needed) return 68;
    pending.truncate(count);
    const auto filename = QFileInfo(pending).fileName();
    const QRegularExpression pattern(QRegularExpression::anchoredPattern("[0-9a-f]{64}-" + nonce + "\\.done\\.pending"));
    if (!pattern.match(filename).hasMatch() || !storageAccessIsPrivate(QFileInfo(pending).absolutePath())) return 69;
    const auto committed = pending.chopped(8);
    // A failed flush or publication must leave no accepted completion proof.
    return MoveFileExW(reinterpret_cast<LPCWSTR>(pending.utf16()), reinterpret_cast<LPCWSTR>(committed.utf16()), MOVEFILE_WRITE_THROUGH) ? 0 : 70;
}
}  // namespace Awake::Integration
