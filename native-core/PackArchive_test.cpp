// SPDX-License-Identifier: GPL-3.0-only
#include "PackArchive.h"
#include <archive.h>
#include <archive_entry.h>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <QtEndian>

#include "PackArchive_test.h"

    static void zip(const QString &path, const QList<QPair<QByteArray,QByteArray>> &files)
    {
        auto *a=archive_write_new(); archive_write_set_format_zip(a);
        const auto name=path.toStdWString(); QVERIFY(archive_write_open_filename_w(a,name.c_str())==ARCHIVE_OK);
        for (const auto &file: files) {
            auto *e=archive_entry_new(); archive_entry_set_pathname(e,file.first.constData());
            archive_entry_set_filetype(e,AE_IFREG); archive_entry_set_perm(e,0644); archive_entry_set_size(e,file.second.size());
            QVERIFY(archive_write_header(a,e)==ARCHIVE_OK); QCOMPARE(archive_write_data(a,file.second.constData(),file.second.size()),la_ssize_t(file.second.size())); archive_entry_free(e);
        }
        QCOMPARE(archive_write_close(a),ARCHIVE_OK); archive_write_free(a);
    }
    static QByteArray modrinth() { return R"({"formatVersion":1,"game":"minecraft","dependencies":{"minecraft":"1.20.1","forge":"47.2.0"},"files":[]})"; }

void PackArchiveTest::validMrpack() {
        QTemporaryDir d; const QString pack=d.filePath("pack.zip"), stage=d.filePath("stage"); QVERIFY(QDir().mkdir(stage));
        zip(pack,{{"modrinth.index.json",modrinth()},{"overrides/config/a.txt","common"},{"client-overrides/config/a.txt","client"}});
        const auto r=PackArchive::inspect(pack,"mrpack",stage); QVERIFY2(r.value("ok").toBool(),qPrintable(r.value("error").toString()));
        QCOMPARE(r.value("minecraftVersion").toString(),"1.20.1"); QCOMPARE(r.value("files").toArray().size(),1);
        auto f=r.value("files").toArray().at(0).toObject(); QFile out(QDir(stage).filePath(f.value("stagedName").toString())); QVERIFY(out.open(QIODevice::ReadOnly)); QCOMPARE(out.readAll(),QByteArray("client"));
    }
void PackArchiveTest::validCurseForge() {
        QTemporaryDir d; const QString pack=d.filePath("pack.zip"), stage=d.filePath("stage"); QVERIFY(QDir().mkdir(stage));
        zip(pack,{{"manifest.json",R"({"manifestType":"minecraftModpack","manifestVersion":1,"minecraft":{"version":"1.20.1","modLoaders":[{"id":"forge-47.2.0","primary":true}]},"files":[{"projectID":123,"fileID":456,"required":true}],"overrides":"overrides"})"},{"overrides/config/x","x"}});
        const auto r=PackArchive::inspect(pack,"curseforge",stage); QVERIFY2(r.value("ok").toBool(),qPrintable(r.value("error").toString())); QCOMPARE(r.value("projects").toArray().size(),1); QCOMPARE(r.value("files").toArray().size(),1);
    }
void PackArchiveTest::rejectUnsafe_data() { QTest::addColumn<QByteArray>("path"); QTest::newRow("traversal")<<QByteArray("../evil"); QTest::newRow("device")<<QByteArray("overrides/CON.txt"); QTest::newRow("ads")<<QByteArray("overrides/a:b"); QTest::newRow("absolute")<<QByteArray("/evil"); }
void PackArchiveTest::rejectUnsafe() { QFETCH(QByteArray,path); QTemporaryDir d; const QString stage=d.filePath("stage"); QVERIFY(QDir().mkdir(stage)); zip(d.filePath("x.zip"),{{"modrinth.index.json",modrinth()},{path,"x"}}); QVERIFY(!PackArchive::inspect(d.filePath("x.zip"),"mrpack",stage).value("ok").toBool()); QVERIFY(QDir(stage).entryList(QDir::Files).isEmpty()); }
void PackArchiveTest::rejectMissingManifestAndHashes() { QTemporaryDir d; const QString stage=d.filePath("stage"); QVERIFY(QDir().mkdir(stage)); zip(d.filePath("x.zip"),{{"overrides/x","x"}}); QVERIFY(!PackArchive::inspect(d.filePath("x.zip"),"mrpack",stage).value("ok").toBool()); zip(d.filePath("x.zip"),{{"modrinth.index.json",R"({"formatVersion":1,"game":"minecraft","dependencies":{"minecraft":"1.20.1"},"files":[{"path":"mods/a.jar","downloads":["https://example.com/a"],"fileSize":1}]})"}}); QVERIFY(!PackArchive::inspect(d.filePath("x.zip"),"mrpack",stage).value("ok").toBool()); }
void PackArchiveTest::rejectCaseDuplicate() { QTemporaryDir d; const QString stage=d.filePath("stage"); QVERIFY(QDir().mkdir(stage)); zip(d.filePath("x.zip"),{{"modrinth.index.json",modrinth()},{"overrides/A","x"},{"overrides/a","y"}}); QVERIFY(!PackArchive::inspect(d.filePath("x.zip"),"mrpack",stage).value("ok").toBool()); }
void PackArchiveTest::rejectDangerousMetadata_data() { QTest::addColumn<int>("offset"); QTest::addColumn<quint32>("value"); QTest::newRow("encrypted")<<8<<quint32(1); QTest::newRow("unsupported-method")<<10<<quint32(99); QTest::newRow("symlink")<<38<<quint32(0120000u<<16); QTest::newRow("bomb")<<24<<quint32(0x40000000); }
void PackArchiveTest::rejectDangerousMetadata() {
        QFETCH(int,offset); QFETCH(quint32,value); QTemporaryDir d; const QString path=d.filePath("x.zip"), stage=d.filePath("stage"); QVERIFY(QDir().mkdir(stage)); zip(path,{{"modrinth.index.json",modrinth()}});
        QFile f(path); QVERIFY(f.open(QIODevice::ReadWrite)); QByteArray data=f.readAll(); const int at=data.indexOf(QByteArray::fromHex("504b0102")); QVERIFY(at>=0);
        if (offset==8 || offset==10) qToLittleEndian<quint16>(quint16(value),data.data()+at+offset); else qToLittleEndian<quint32>(value,data.data()+at+offset);
        QVERIFY(f.seek(0)); QCOMPARE(f.write(data),qint64(data.size())); f.close(); QVERIFY(!PackArchive::inspect(path,"mrpack",stage).value("ok").toBool());
    }
void PackArchiveTest::strictManifest() {
    QTemporaryDir d; const QString path=d.filePath("x.zip"), stage=d.filePath("stage"); QVERIFY(QDir().mkdir(stage));
    QJsonObject file{{"path","mods/a.jar"},{"hashes",QJsonObject{{"sha1",QString(40,'A')},{"sha512",QString(128,'B')}}},{"downloads",QJsonArray{"https://example.com/a"}},{"fileSize",1}};
    QJsonObject manifest{{"formatVersion",1},{"game","minecraft"},{"dependencies",QJsonObject{{"minecraft","1.20.1"}}},{"files",QJsonArray{file}}};
    auto inspect=[&](const QJsonObject &value, const QString &format) { zip(path,{{format=="mrpack" ? "modrinth.index.json" : "manifest.json",QJsonDocument(value).toJson(QJsonDocument::Compact)}}); return PackArchive::inspect(path,format,stage); };
    auto result=inspect(manifest,"mrpack"); QVERIFY2(result.value("ok").toBool(),qPrintable(result.value("error").toString()));
    QCOMPARE(result.value("downloads").toArray().at(0).toObject().value("hashes").toObject().value("sha1").toString(),QString(40,'a'));
    file["env"]=QJsonObject{{"client",1}}; manifest["files"]=QJsonArray{file}; QVERIFY(!inspect(manifest,"mrpack").value("ok").toBool());
    file["env"]=QJsonObject{{"server",true}}; manifest["files"]=QJsonArray{file}; QVERIFY(!inspect(manifest,"mrpack").value("ok").toBool());
    manifest["formatVersion"]=1.5; QVERIFY(!inspect(manifest,"mrpack").value("ok").toBool());
    manifest["formatVersion"]=1; manifest["dependencies"]=QJsonObject{{"minecraft",QString(97,'x')}}; QVERIFY(!inspect(manifest,"mrpack").value("ok").toBool());
    QJsonArray many; for (int i=0;i<10001;++i) many.append(i); manifest["files"]=many; QVERIFY(!inspect(manifest,"mrpack").value("ok").toBool());
    QJsonObject cf{{"manifestType","minecraftModpack"},{"manifestVersion",1.5},{"minecraft",QJsonObject{{"version","1.20.1"},{"modLoaders",QJsonArray{}}}},{"files",QJsonArray{}}};
    QVERIFY(!inspect(cf,"curseforge").value("ok").toBool()); cf["manifestVersion"]=1; QJsonArray projects; for (int i=0;i<1025;++i) projects.append(i); cf["files"]=projects; QVERIFY(!inspect(cf,"curseforge").value("ok").toBool());
}
QTEST_GUILESS_MAIN(PackArchiveTest)

