// SPDX-License-Identifier: GPL-3.0-only
#include "InboxService.h"
#include "Contract.h"
#include "WorkspaceServices.h"
#include "awake/integration/DurableIO.h"
#include "awake/integration/ProfileAccess.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QUuid>
#include <io.h>
#include <qt_windows.h>

namespace Awake::Core {
using namespace Awake::Integration;
namespace {
constexpr quint64 maxFile=1024ULL*1024*1024, maxTotal=4*maxFile, maxChunk=256*1024;
QMutex mutex;
QString sha(const QByteArray &data) { return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex()); }
bool uuid(const QString &s) { const QUuid id(s); return !id.isNull() && id.toString(QUuid::WithoutBraces)==s; }
quint64 number(const QJsonValue &value) { bool ok=false; const auto n=value.toString().toULongLong(&ok); if (!ok) throw ServiceError{"INVALID_REQUEST","Invalid transfer size or offset."}; return n; }
void validate(const char *method, const QJsonObject &p) { if (!Contract().valid(QString::fromLatin1(method)+".params",p)) throw ServiceError{"INVALID_REQUEST","Invalid inbox request."}; }
void privateDirectory(const QString &path) { if (!createPrivateStorageDirectory(path) || !storageAccessIsPrivate(path)) throw ServiceError{"IO_ERROR","Private inbox storage is unavailable."}; }
QString dataPath(const QString &directory, const QString &token) { if (!uuid(token)) throw ServiceError{"INVALID_REQUEST","Invalid transfer token."}; return directory+'/'+token+".bin"; }
QString recordPath(const QString &directory, const QString &key) { return directory+'/'+sha(key.toUtf8())+".json"; }
void flush(QFile &file) {
    if (!file.flush() || file.handle()<0 || !FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle())))) throw ServiceError{"IO_ERROR","Cannot durably write transfer."};
}
QList<QPair<QString,QJsonObject>> records(const QString &directory) {
    QList<QPair<QString,QJsonObject>> result;
    const auto list=QDir(directory).entryInfoList({"*.json"},QDir::Files|QDir::Hidden|QDir::System);
    for (const auto &info: list) {
        const auto r=readRecord(info.absoluteFilePath()); const auto request=r.value("request").toObject();
        if (r.value("format")!=1 || !uuid(r.value("token").toString()) || !Contract().valid("inbox.begin.params",request) || info.absoluteFilePath()!=recordPath(directory,request.value("idempotencyKey").toString()) || !r.value("complete").isBool() || !r.value("received").isString() || number(r.value("received"))>number(request.value("size"))) throw ServiceError{"RECOVERY_REQUIRED","Inbox transfer record is invalid."};
        const auto state=r.value("discardState").toString();
        if ((!state.isEmpty() && state!="discarding" && state!="discarded") ||
            (r.contains("discardState") && !r.value("discardState").isString()) ||
            (!state.isEmpty() && (!QRegularExpression("^[0-9a-f]{64}$").match(r.value("discardHash").toString()).hasMatch() ||
                !QRegularExpression("^file1:[0-9a-f]{16}:[0-9a-f]{32}$").match(r.value("discardIdentity").toString()).hasMatch())))
            throw ServiceError{"RECOVERY_REQUIRED","Inbox discard record is invalid."};
        result.append({info.absoluteFilePath(),r});
    }
    return result;
}
void requireActive(const QJsonObject &record) {
    if (!record.value("discardState").toString().isEmpty()) throw ServiceError{"TRANSFER_DISCARDED","Transfer has been discarded."};
}
DurableIO::FileState lockedState(HANDLE file) {
    FILE_ID_INFO identity{}; BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER start{};
    if (!GetFileInformationByHandle(file,&info) || !GetFileInformationByHandleEx(file,FileIdInfo,&identity,sizeof(identity)) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || info.nNumberOfLinks!=1 ||
        !SetFilePointerEx(file,start,nullptr,FILE_BEGIN)) throw ServiceError{"RECOVERY_REQUIRED","Transfer identity is unsafe."};
    QCryptographicHash digest(QCryptographicHash::Sha256); QByteArray buffer(1024*1024,Qt::Uninitialized); DWORD count=0;
    while (ReadFile(file,buffer.data(),DWORD(buffer.size()),&count,nullptr)) {
        if (!count) return {QString::fromLatin1(digest.result().toHex()),
            QString("file1:%1:%2").arg(identity.VolumeSerialNumber,16,16,QChar('0'))
                .arg(QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(identity.FileId.Identifier),16).toHex()))};
        digest.addData(QByteArrayView(buffer.constData(),count));
    }
    throw ServiceError{"RECOVERY_REQUIRED","Transfer data could not be verified."};
}
QPair<QString,QJsonObject> findRecord(const QString &directory, const QString &token) {
    if (!uuid(token)) throw ServiceError{"INVALID_REQUEST","Invalid transfer token."};
    for (const auto &record: records(directory)) if (record.second.value("token").toString()==token) return record;
    throw ServiceError{"NOT_FOUND","Transfer not found."};
}
quint64 currentSize(const QString &path, const QJsonObject &record) {
    const QFileInfo info(path); const quint64 recorded=number(record.value("received")), total=number(record.value("request").toObject().value("size"));
    if (!info.exists()) {
        if (recorded || record.value("complete").toBool()) throw ServiceError{"RECOVERY_REQUIRED","Transfer data is missing."};
        QFile file(path); if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) throw ServiceError{"IO_ERROR","Cannot create transfer data."}; flush(file); file.close();
    }
    if (!storageAccessIsPrivate(path)) throw ServiceError{"RECOVERY_REQUIRED","Transfer data is not private."};
    const QFileInfo actual(path);
    if (!actual.isFile() || actual.size()<0 || quint64(actual.size())<recorded || quint64(actual.size())>total) throw ServiceError{"RECOVERY_REQUIRED","Transfer data length is inconsistent."};
    return quint64(actual.size());
}
bool verified(const QString &path, const QJsonObject &record, quint64 size) {
    const auto request=record.value("request").toObject();
    if (size!=number(request.value("size"))) return false;
    QString error; const auto hash=DurableIO::hash(path,&error);
    if (hash.isEmpty()) throw ServiceError{"RECOVERY_REQUIRED","Transfer integrity cannot be verified."};
    return hash==request.value("sha256").toString();
}
QJsonObject view(const QJsonObject &record, quint64 received) {
    const auto request=record.value("request").toObject();
    return {{"token",record.value("token")},{"size",request.value("size")},{"received",QString::number(received)},{"sha256",request.value("sha256")},{"complete",record.value("complete")}};
}
}
InboxService::InboxService(QString profile) : m_directory(QDir::cleanPath(profile+"/inbox")) { QMutexLocker lock(&mutex); privateDirectory(m_directory); }
QJsonObject InboxService::begin(const QJsonObject &p) {
    QMutexLocker lock(&mutex); validate("inbox.begin",p); privateDirectory(m_directory);
    const auto size=number(p.value("size")); if (size>maxFile) throw ServiceError{"LIMIT_EXCEEDED","Transfer exceeds the size limit."};
    const auto path=recordPath(m_directory,p.value("idempotencyKey").toString()); quint64 total=0; int active=0;
    for (const auto &item: records(m_directory)) {
        const auto r=item.second;
        if (item.first==path) {
            if (r.value("request").toObject()!=p) throw ServiceError{"IDEMPOTENCY_CONFLICT","Idempotency key already identifies another transfer."};
            requireActive(r);
            const auto data=dataPath(m_directory,r.value("token").toString()); const auto received=currentSize(data,r);
            if (r.value("complete").toBool() && !verified(data,r,received)) throw ServiceError{"INTEGRITY_FAILED","Committed transfer changed."};
            return view(r,received);
        }
        if (r.value("discardState")!="discarded") { total+=number(r.value("request").toObject().value("size")); if (!r.value("complete").toBool()) ++active; }
    }
    if (active>=16 || total>maxTotal-size) throw ServiceError{"LIMIT_EXCEEDED","Inbox transfer capacity exceeded."};
    const auto token=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject record{{"format",1},{"token",token},{"request",p},{"received","0"},{"complete",false}};
    writeRecord(path,record); const auto received=currentSize(dataPath(m_directory,token),record);
    return view(record,received);
}
QJsonObject InboxService::write(const QJsonObject &p) {
    QMutexLocker lock(&mutex); validate("inbox.write",p); privateDirectory(m_directory);
    auto item=findRecord(m_directory,p.value("token").toString()); auto record=item.second;
    requireActive(record);
    const auto path=dataPath(m_directory,p.value("token").toString()); const auto received=currentSize(path,record), offset=number(p.value("offset"));
    const auto encoded=p.value("bytesBase64").toString().toLatin1(); const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size()>maxChunk || decoded.decoded.toBase64()!=encoded) throw ServiceError{"INVALID_REQUEST","Invalid transfer chunk."};
    const auto data=decoded.decoded; const auto total=number(record.value("request").toObject().value("size"));
    if (offset>received || offset>total || quint64(data.size())>total-offset) throw ServiceError{"INVALID_REQUEST","Transfer offset or length is invalid."};
    QFile file(path); if (!file.open(QIODevice::ReadWrite) || !file.seek(qint64(offset))) throw ServiceError{"IO_ERROR","Cannot open transfer data."};
    const auto existing=qMin<quint64>(received-offset,quint64(data.size()));
    if (file.read(qint64(existing))!=data.left(qsizetype(existing))) throw ServiceError{"IDEMPOTENCY_CONFLICT","Replayed chunk differs from stored data."};
    if (existing<quint64(data.size())) {
        if (record.value("complete").toBool()) throw ServiceError{"INVALID_REQUEST","Transfer is already complete."};
        if (file.write(data.constData()+existing,data.size()-qsizetype(existing))!=data.size()-qsizetype(existing)) throw ServiceError{"IO_ERROR","Cannot write transfer data."};
    }
    flush(file);
    file.close(); const auto now=qMax(received,offset+quint64(data.size()));
    if (!record.value("complete").toBool()) { record["received"]=QString::number(now); writeRecord(item.first,record); }
    else if (!verified(path,record,now)) throw ServiceError{"INTEGRITY_FAILED","Committed transfer changed."};
    return view(record,now);
}
QJsonObject InboxService::commit(const QJsonObject &p) {
    QMutexLocker lock(&mutex); validate("inbox.commit",p); privateDirectory(m_directory);
    auto item=findRecord(m_directory,p.value("token").toString()); auto record=item.second;
    requireActive(record);
    const auto path=dataPath(m_directory,p.value("token").toString()); const auto received=currentSize(path,record);
    if (!verified(path,record,received)) throw ServiceError{"INTEGRITY_FAILED","Transfer size or hash does not match."};
    if (!record.value("complete").toBool()) { record["received"]=QString::number(received); record["complete"]=true; writeRecord(item.first,record); }
    return view(record,received);
}
QJsonObject InboxService::status(const QJsonObject &p) {
    QMutexLocker lock(&mutex); validate("inbox.status",p); privateDirectory(m_directory);
    const auto record=findRecord(m_directory,p.value("token").toString()).second;
    requireActive(record);
    const auto path=dataPath(m_directory,p.value("token").toString()); const auto received=currentSize(path,record);
    if (record.value("complete").toBool() && !verified(path,record,received)) throw ServiceError{"INTEGRITY_FAILED","Committed transfer changed."};
    return view(record,received);
}
QString InboxService::committedPath(const QString &token) {
    QMutexLocker lock(&mutex); privateDirectory(m_directory); const auto record=findRecord(m_directory,token).second;
    requireActive(record);
    if (!record.value("complete").toBool()) throw ServiceError{"INVALID_REQUEST","Transfer is not committed."};
    const auto path=dataPath(m_directory,token); const auto received=currentSize(path,record);
    if (!verified(path,record,received)) throw ServiceError{"INTEGRITY_FAILED","Committed transfer changed."};
    return path;
}
QJsonObject InboxService::discard(const QJsonObject &p) {
    QMutexLocker lock(&mutex); privateDirectory(m_directory);
    if (p.size()!=1 || !p.value("token").isString() || !uuid(p.value("token").toString())) throw ServiceError{"INVALID_REQUEST","Invalid transfer token."};
    const auto token=p.value("token").toString(); auto item=findRecord(m_directory,token); auto record=item.second;
    if (record.value("discardState")=="discarded") return {{"token",token},{"discarded",true}};
    const auto path=dataPath(m_directory,token); const bool pending=record.value("discardState")=="discarding";
    if (!pending) currentSize(path,record);
    if (QFileInfo::exists(path) && !storageAccessIsPrivate(path)) throw ServiceError{"RECOVERY_REQUIRED","Transfer data is not private."};
    const auto native=QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    const auto handle=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if (handle==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();
        if (!pending || (error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)) throw ServiceError{"RECOVERY_REQUIRED","Transfer data is unavailable or in use."};
    } else {
        auto close=qScopeGuard([&] { CloseHandle(handle); });
        const auto actual=lockedState(handle);
        if (pending) {
            if (actual.hash!=record.value("discardHash").toString() || actual.identity!=record.value("discardIdentity").toString())
                throw ServiceError{"RECOVERY_REQUIRED","Transfer changed after discard began; its data was preserved."};
        } else {
            record["discardState"]="discarding"; record["discardHash"]=actual.hash; record["discardIdentity"]=actual.identity;
            writeRecord(item.first,record);
        }
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(handle,FileDispositionInfo,&disposition,sizeof(disposition))) throw ServiceError{"RECOVERY_REQUIRED","Cannot remove transfer data."};
    }
    record["discardState"]="discarded"; writeRecord(item.first,record);
    return {{"token",token},{"discarded",true}};
}
}
