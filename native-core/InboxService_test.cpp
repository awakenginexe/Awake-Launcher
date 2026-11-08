// SPDX-License-Identifier: GPL-3.0-only
#include "InboxService.h"
#include "ModrinthService.h"
#include "awake/integration/ProfileAccess.h"
#include "awake/integration/DurableIO.h"
#include "WorkspaceServices.h"
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class InboxServiceTest : public QObject {
    Q_OBJECT
private slots:
    void transferAndRestart();
    void rejection();
    void capacity();
    void interruptedAppend();
    void aggregateCapacity();
    void discardReleasesQuotaAndPreservesTombstone();
    void discardResumesIntentAndRejectsChanges();
};

#ifndef Q_MOC_RUN
using namespace Awake::Core;
static QString sha(const QByteArray &b) { return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex()); }
static QJsonObject request(const QString &key, const QByteArray &b) { return {{"idempotencyKey",key},{"purpose","import"},{"size",QString::number(b.size())},{"sha256",sha(b)}}; }
template<typename F> static QString error(F f) { try { f(); } catch (const ServiceError &e) { return e.code; } return {}; }
void InboxServiceTest::transferAndRestart() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); QVERIFY(Awake::Integration::createPrivateStorageDirectory(dir.filePath("profile")));
    const QByteArray data="hello world"; const auto params=request("key",data); QString token;
    {
        InboxService inbox(dir.filePath("profile")); const auto begun=inbox.begin(params); token=begun.value("token").toString(); QVERIFY(!token.isEmpty());
        QCOMPARE(inbox.begin(params).value("token").toString(),token);
        QCOMPARE(error([&]{ inbox.begin(request("key","different")); }),QString("IDEMPOTENCY_CONFLICT"));
        const QJsonObject first{{"token",token},{"offset","0"},{"bytesBase64",QString::fromLatin1(data.left(5).toBase64())}};
        QCOMPARE(inbox.write(first).value("received").toString(),QString("5")); QCOMPARE(inbox.write(first).value("received").toString(),QString("5"));
        QCOMPARE(error([&]{ inbox.write({{"token",token},{"offset","0"},{"bytesBase64","eA=="}}); }),QString("IDEMPOTENCY_CONFLICT"));
    }
    {
        InboxService inbox(dir.filePath("profile")); QCOMPARE(inbox.status({{"token",token}}).value("received").toString(),QString("5"));
        QCOMPARE(inbox.write({{"token",token},{"offset","3"},{"bytesBase64",QString::fromLatin1(data.mid(3).toBase64())}}).value("received").toString(),QString("11"));
        QVERIFY(inbox.commit({{"token",token}}).value("complete").toBool()); QVERIFY(!inbox.committedPath(token).isEmpty());
        QFile file(inbox.committedPath(token)); QVERIFY(file.open(QIODevice::ReadWrite)); QVERIFY(file.seek(0)); QCOMPARE(file.write("X",1),qint64(1)); file.close();
        QCOMPARE(error([&]{ inbox.status({{"token",token}}); }),QString("INTEGRITY_FAILED"));
    }
}
void InboxServiceTest::rejection() {
    QTemporaryDir dir; QVERIFY(Awake::Integration::createPrivateStorageDirectory(dir.filePath("profile"))); InboxService inbox(dir.filePath("profile"));
    const auto params=request("a","abc"); const auto token=inbox.begin(params).value("token").toString();
    QCOMPARE(error([&]{ inbox.status({{"token","../../file"}}); }),QString("INVALID_REQUEST"));
    QCOMPARE(error([&]{ inbox.write({{"token",token},{"offset","0"},{"bytesBase64","@@@"}}); }),QString("INVALID_REQUEST"));
    QCOMPARE(error([&]{ inbox.write({{"token",token},{"offset","1"},{"bytesBase64","YQ=="}}); }),QString("INVALID_REQUEST"));
    QCOMPARE(error([&]{ inbox.commit({{"token",token}}); }),QString("INTEGRITY_FAILED"));
    inbox.write({{"token",token},{"offset","0"},{"bytesBase64","eHl6"}});
    QCOMPARE(error([&]{ inbox.commit({{"token",token}}); }),QString("INTEGRITY_FAILED"));
}
void InboxServiceTest::capacity() {
    QTemporaryDir dir; QVERIFY(Awake::Integration::createPrivateStorageDirectory(dir.filePath("profile"))); InboxService inbox(dir.filePath("profile"));
    auto tooBig=request("large",{}); tooBig["size"]="1073741825"; QCOMPARE(error([&]{ inbox.begin(tooBig); }),QString("LIMIT_EXCEEDED"));
    for (int i=0;i<16;++i) inbox.begin(request(QString::number(i),"x"));
    QCOMPARE(error([&]{ inbox.begin(request("overflow","x")); }),QString("LIMIT_EXCEEDED"));
}
void InboxServiceTest::interruptedAppend() {
    QTemporaryDir dir; QVERIFY(Awake::Integration::createPrivateStorageDirectory(dir.filePath("profile")));
    const QByteArray data="abcdef"; QString token;
    { InboxService inbox(dir.filePath("profile")); token=inbox.begin(request("replay",data)).value("token").toString(); }
    QFile partial(dir.filePath("profile/inbox/"+token+".bin")); QVERIFY(partial.open(QIODevice::Append)); QCOMPARE(partial.write("abc",3),qint64(3)); partial.close();
    InboxService inbox(dir.filePath("profile")); QCOMPARE(inbox.status({{"token",token}}).value("received").toString(),QString("3"));
    QCOMPARE(inbox.write({{"token",token},{"offset","0"},{"bytesBase64",QString::fromLatin1(data.toBase64())}}).value("received").toString(),QString("6"));
    QVERIFY(inbox.commit({{"token",token}}).value("complete").toBool());
}
void InboxServiceTest::aggregateCapacity() {
    QTemporaryDir dir; QVERIFY(Awake::Integration::createPrivateStorageDirectory(dir.filePath("profile"))); InboxService inbox(dir.filePath("profile"));
    QString first; for (int i=0;i<4;++i) { auto p=request(QString::number(i),"x"); p["size"]="1073741824"; const auto token=inbox.begin(p).value("token").toString(); if (!i) first=token; }
    QCOMPARE(error([&]{ inbox.begin(request("overflow","x")); }),QString("LIMIT_EXCEEDED"));
    QVERIFY(inbox.discard({{"token",first}}).value("discarded").toBool());
    QVERIFY(!inbox.begin(request("overflow","x")).value("token").toString().isEmpty());
}
void InboxServiceTest::discardReleasesQuotaAndPreservesTombstone() {
    QTemporaryDir dir; const auto profile=dir.filePath("profile"); QVERIFY(Awake::Integration::createPrivateStorageDirectory(profile));
    const auto params=request("complete","abc"); QString token;
    {
        InboxService inbox(profile); token=inbox.begin(params).value("token").toString();
        inbox.write({{"token",token},{"offset","0"},{"bytesBase64","YWJj"}}); inbox.commit({{"token",token}});
        for (int i=0;i<16;++i) inbox.begin(request("partial-"+QString::number(i),"x"));
        QCOMPARE(error([&]{ inbox.begin(request("overflow","x")); }),QString("LIMIT_EXCEEDED"));
        const auto partial=inbox.begin(request("partial-0","x")).value("token").toString();
        QVERIFY(inbox.discard({{"token",partial}}).value("discarded").toBool());
        QVERIFY(!inbox.begin(request("replacement","x")).value("token").toString().isEmpty());
        QVERIFY(inbox.discard({{"token",token}}).value("discarded").toBool());
        QVERIFY(!QFile::exists(profile+"/inbox/"+token+".bin"));
    }
    InboxService restarted(profile);
    QVERIFY(restarted.discard({{"token",token}}).value("discarded").toBool());
    QCOMPARE(error([&]{ restarted.begin(params); }),QString("TRANSFER_DISCARDED"));
    QCOMPARE(error([&]{ restarted.status({{"token",token}}); }),QString("TRANSFER_DISCARDED"));
    QCOMPARE(error([&]{ restarted.write({{"token",token},{"offset","0"},{"bytesBase64","YQ=="}}); }),QString("TRANSFER_DISCARDED"));
    QCOMPARE(error([&]{ restarted.commit({{"token",token}}); }),QString("TRANSFER_DISCARDED"));
    QCOMPARE(error([&]{ restarted.committedPath(token); }),QString("TRANSFER_DISCARDED"));
    const auto spare=restarted.begin(request("partial-1","x")).value("token").toString();
    restarted.discard({{"token",spare}});
    const auto failed=restarted.begin(request("hash-failure","abc")).value("token").toString();
    restarted.write({{"token",failed},{"offset","0"},{"bytesBase64","eHl6"}});
    QCOMPARE(error([&]{ restarted.commit({{"token",failed}}); }),QString("INTEGRITY_FAILED"));
    QVERIFY(restarted.discard({{"token",failed}}).value("discarded").toBool());
}
void InboxServiceTest::discardResumesIntentAndRejectsChanges() {
    QTemporaryDir dir; const auto profile=dir.filePath("profile"); QVERIFY(Awake::Integration::createPrivateStorageDirectory(profile));
    const auto params=request("pending","abc"); QString token;
    { InboxService inbox(profile); token=inbox.begin(params).value("token").toString(); inbox.write({{"token",token},{"offset","0"},{"bytesBase64","YWJj"}}); }
    const auto data=profile+"/inbox/"+token+".bin", recordPath=profile+"/inbox/"+sha("pending")+".json";
    const auto state=Awake::Integration::DurableIO::fingerprint(data); auto record=readRecord(recordPath);
    record["discardState"]="discarding"; record["discardHash"]=state.hash; record["discardIdentity"]=state.identity; writeRecord(recordPath,record);
    { QFile changed(data); QVERIFY(changed.open(QIODevice::WriteOnly)); QCOMPARE(changed.write("xyz"),qint64(3)); }
    InboxService inbox(profile);
    QCOMPARE(error([&]{ inbox.discard({{"token",token}}); }),QString("RECOVERY_REQUIRED")); QVERIFY(QFile::exists(data));
    QCOMPARE(error([&]{ inbox.status({{"token",token}}); }),QString("TRANSFER_DISCARDED"));
    { QFile restore(data); QVERIFY(restore.open(QIODevice::WriteOnly)); QCOMPARE(restore.write("abc"),qint64(3)); }
    QVERIFY(inbox.discard({{"token",token}}).value("discarded").toBool()); QVERIFY(!QFile::exists(data));
    const auto missingParams=request("already-deleted","abc"); const auto missing=inbox.begin(missingParams).value("token").toString();
    const auto missingData=profile+"/inbox/"+missing+".bin", missingRecord=profile+"/inbox/"+sha("already-deleted")+".json";
    const auto missingState=Awake::Integration::DurableIO::fingerprint(missingData); auto pending=readRecord(missingRecord);
    pending["discardState"]="discarding"; pending["discardHash"]=missingState.hash; pending["discardIdentity"]=missingState.identity; writeRecord(missingRecord,pending);
    QVERIFY(QFile::remove(missingData)); QVERIFY(inbox.discard({{"token",missing}}).value("discarded").toBool());
}
#endif
QTEST_GUILESS_MAIN(InboxServiceTest)
#include "InboxService_test.moc"
