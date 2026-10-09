// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include "archive/ArchiveWriter.h"
#include <QTest>
#include "awake/LocalInstanceImport.h"
#include "modplatform/atlauncher/ATLPackIndex.h"

using namespace Awake::LocalImport;
static void write(const QString& path, const QByteArray& bytes)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
class LocalInstanceImportTest : public QObject {
    Q_OBJECT
private slots:
    void formats_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("minecraft");
        QTest::addColumn<QString>("loader");
        QTest::addColumn<QString>("version");
        QTest::newRow("ATLauncher NeoForge") << "instance.json" << QByteArray(R"({"id":"1.21.1","launcher":{"name":"My pack","loaderVersion":{"type":"neoforge","version":"21.1.248"}}})") << "ATLauncher" << "1.21.1" << "net.neoforged" << "21.1.248";
        QTest::newRow("ATLauncher vanilla") << "instance.json" << QByteArray(R"({"id":"1.7.4","launcher":{"name":"My pack"}})") << "ATLauncher" << "1.7.4" << "" << "";
        QTest::newRow("CurseForge Forge") << "minecraftinstance.json" << QByteArray(R"({"name":"My pack","gameVersion":"1.12.2","baseModLoader":{"name":"forge-14.23.5.2854","forgeVersion":"14.23.5.2854"}})") << "CurseForge" << "1.12.2" << "net.minecraftforge" << "14.23.5.2854";
        QTest::newRow("CurseForge Fabric") << "minecraftinstance.json" << QByteArray(R"({"name":"My pack","gameVersion":"1.21.1","baseModLoader":{"name":"fabric-0.16.14-1.21.1"}})") << "CurseForge" << "1.21.1" << "net.fabricmc.fabric-loader" << "0.16.14";
        QTest::newRow("CurseForge NeoForge") << "minecraftinstance.json" << QByteArray(R"({"name":"My pack","gameVersion":"1.21.1","baseModLoader":{"name":"neoforge-21.1.248","forgeVersion":"21.1.248"}})") << "CurseForge" << "1.21.1" << "net.neoforged" << "21.1.248";
        QTest::newRow("FTB") << "instance.json" << QByteArray(R"({"name":"My pack","mcVersion":"1.20.1","modLoader":"forge-47.4.0","totalPlayTime":9000})") << "FTB App" << "1.20.1" << "net.minecraftforge" << "47.4.0";
    }
    void formats()
    {
        QFETCH(QString, file); QFETCH(QByteArray, json); QFETCH(QString, source);
        QFETCH(QString, minecraft); QFETCH(QString, loader); QFETCH(QString, version);
        QTemporaryDir dir;
        write(dir.path() + '/' + file, json);
        const auto item = inspect(dir.path());
        QVERIFY2(item.error.isEmpty(), qPrintable(item.error));
        QCOMPARE(item.name, "My pack"); QCOMPARE(item.source, source);
        QCOMPARE(item.minecraft, minecraft); QCOMPARE(item.loader, loader); QCOMPARE(item.loaderVersion, version);
    }
    void prismCopyPreservesSettingsAndOriginal()
    {
        QTemporaryDir source, target;
        write(source.path() + "/instance.cfg", "InstanceType=OneSix\nname=My pack\nMaxMemAlloc=6000\n");
        const QByteArray manifest = R"({"formatVersion":1,"components":[{"uid":"net.minecraft","version":"1.21.1"},{"uid":"net.fabricmc.fabric-loader","version":"0.16.14"}]})";
        write(source.path() + "/mmc-pack.json", manifest);
        write(source.path() + "/.minecraft/saves/world/level.dat", "world");
        write(source.path() + "/.minecraft/mods/test.jar.disabled", "disabled");
        write(source.path() + "/accounts.json", "private");
        const auto item = inspect(source.path());
        QVERIFY2(item.error.isEmpty(), qPrintable(item.error));
        QCOMPARE(item.minecraft, "1.21.1"); QCOMPARE(item.name, "My pack");
        QString error;
        QVERIFY2(copyInstance(item, target.path() + "/new", &error), qPrintable(error));
        QVERIFY(QFile::exists(target.path() + "/new/.minecraft/saves/world/level.dat"));
        QVERIFY(QFile::exists(target.path() + "/new/.minecraft/mods/test.jar.disabled"));
        QVERIFY(!QFile::exists(target.path() + "/new/accounts.json"));
        QFile config(target.path() + "/new/instance.cfg"); QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(config.readAll().contains("MaxMemAlloc=6000"));
        QFile original(source.path() + "/mmc-pack.json"); QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), manifest);
        QVERIFY(!copyInstance(item, source.path() + "/nested", &error));
    }
    void scanRootInstancesAndSingleFolder()
    {
        QTemporaryDir dir;
        write(dir.path() + "/Instances/one/instance.json", R"({"id":"1.7.4","launcher":{"name":"My pack"}})");
        write(dir.path() + "/Instances/broken/instance.json", "broken");
        QCOMPARE(scan(dir.path()).size(), 2);
        QCOMPARE(scan(dir.path() + "/Instances").size(), 2);
        QCOMPARE(scan(dir.path() + "/Instances/one").size(), 1);
        QCOMPARE(scan(dir.path() + "/Instances/one").first().minecraft, "1.7.4");
    }
    void rejectsMissingOrUnknownMetadata()
    {
        QTemporaryDir dir;
        QVERIFY(!inspect(dir.path()).error.isEmpty());
        write(dir.path() + "/instance.json", R"({"id":"1.21.1","launcher":{"name":"My pack","loaderVersion":{"type":"unknown","version":"1"}}})");
        QVERIFY(!inspect(dir.path()).error.isEmpty());
        write(dir.path() + "/instance.json", R"({"launcher":{"name":"My pack"}})");
        QVERIFY(!inspect(dir.path()).error.isEmpty());
    }
    void copyFailureDoesNotSucceed()
    {
        QTemporaryDir source, target;
        write(source.path() + "/instance.json", R"({"id":"1.7.4","launcher":{"name":"My pack"}})");
        write(source.path() + "/saves/world/level.dat", "world");
        write(target.path() + "/file", "block destination");
        QString error;
        QVERIFY(!copyInstance(inspect(source.path()), target.path() + "/file", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(QFile::exists(source.path() + "/saves/world/level.dat"));
    }
    void sourcePackChangeRequiresRescan()
    {
        QTemporaryDir source, target;
        write(source.path() + "/minecraftinstance.json", R"({"name":"Pack","gameVersion":"1.21.1","installedModpack":{"addonID":123,"fileID":456}})");
        const auto original = inspect(source.path());
        write(source.path() + "/minecraftinstance.json", R"({"name":"Pack","gameVersion":"1.21.1","installedModpack":{"addonID":123,"fileID":789}})");
        QString error;
        QVERIFY(!copyInstance(original, target.path() + "/new", &error));
        QVERIFY(!QFile::exists(target.path() + "/new"));
    }
    void atlauncherDisabledModsAreConverted()
    {
        QTemporaryDir source, target;
        write(source.path() + "/instance.json", R"({"id":"1.21.1","launcher":{"name":"My pack"}})");
        write(source.path() + "/disabledmods/test.jar", "disabled mod");
        write(source.path() + "/mods/active.jar", "active mod");
        QString error;
        QVERIFY2(copyInstance(inspect(source.path()), target.path() + "/new", &error), qPrintable(error));
        QVERIFY(QFile::exists(target.path() + "/new/minecraft/mods/test.jar.disabled"));
        QVERIFY(QFile::exists(target.path() + "/new/minecraft/mods/active.jar"));
        QVERIFY(QFile::exists(source.path() + "/disabledmods/test.jar"));
    }
    void atlauncherMemoryUsesUserOverride()
    {
        QTemporaryDir dir;
        write(dir.path() + "/instance.json", R"({"id":"1.21.1","launcher":{"name":"My pack","requiredMemory":4000,"maximumMemory":8000}})");
        QCOMPARE(inspect(dir.path()).memory, 8000);
    }
    void importedPackIdentity()
    {
        QTemporaryDir dir;
        write(dir.path() + "/minecraftinstance.json", R"({"name":"Pack","gameVersion":"1.21.1","installedModpack":{"addonID":123,"fileID":456,"name":"Online pack"}})");
        auto pack = inspect(dir.path()).pack;
        QCOMPARE(pack.provider, QString("curseforge"));
        QCOMPARE(pack.id, QString("123"));
        QCOMPARE(pack.versionId, QString("456"));
        QFile::remove(dir.path() + "/minecraftinstance.json");
        write(dir.path() + "/instance.json", R"({"id":"1.21.1","launcher":{"name":"Pack","curseForgeProject":{"id":123,"name":"Online pack"},"curseForgeFile":{"id":456,"displayName":"Release 1"}}})");
        QCOMPARE(inspect(dir.path()).pack.versionId, QString("456"));
        write(dir.path() + "/instance.json", R"({"id":"1.21.1","launcher":{"name":"Pack","modrinthProject":{"id":"project","title":"Online pack"},"modrinthVersion":{"id":"release","version_number":"1.0"}}})");
        QCOMPARE(inspect(dir.path()).pack.provider, QString("modrinth"));
        QCOMPARE(inspect(dir.path()).pack.versionId, QString("release"));
        write(dir.path() + "/instance.json", R"({"id":"1.21.1","launcher":{"name":"Custom pack","version":"1.0"}})");
        QVERIFY(inspect(dir.path()).pack.id.isEmpty());
        write(dir.path() + "/instance.json", R"({"id":"1.21.1","launcher":{"name":"Native AT pack","packId":22,"pack":"The Pack!","version":"1.0"}})");
        QCOMPARE(inspect(dir.path()).pack.provider, QString("atlauncher"));
        QCOMPARE(inspect(dir.path()).pack.id, QString("ThePack"));
    }
    void atlauncherInstalledIdMatchesCatalog()
    {
        QJsonObject catalog{{"id", 22}, {"position", 1}, {"name", "The Pack!"}, {"type", "public"}, {"versions", QJsonArray{}}};
        ATLauncher::IndexedPack pack{};
        QVERIFY(ATLauncher::loadIndexedPack(pack, catalog).has_value());
        QCOMPARE(pack.safeName, QString("thepack.png"));
        QTemporaryDir dir;
        write(dir.path() + "/instance.json", R"({"id":"1.21.1","launcher":{"packId":22,"pack":"The Pack!","version":"1.0"}})");
        QCOMPARE(ATLauncher::packId(pack.name), inspect(dir.path()).pack.id);
    }
    void rejectsUnusableReleaseBaselines_data()
    {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<QByteArray>("manifest");
        QTest::newRow("missing hash") << "modrinth" << QByteArray(R"({"formatVersion":1,"game":"minecraft","dependencies":{"minecraft":"1.21.1"},"files":[{"path":"mods/x.jar"}]})");
        QTest::newRow("string CF ID") << "curseforge" << QByteArray(R"({"manifestVersion":1,"manifestType":"minecraftModpack","minecraft":{"version":"1.21.1"},"files":[{"projectID":"123","fileID":456}]})");
    }
    void rejectsUnusableReleaseBaselines()
    {
        QFETCH(QString, provider); QFETCH(QByteArray, manifest);
        QTemporaryDir dir;
        MMCZip::ArchiveWriter zip(dir.path() + "/pack.zip");
        QVERIFY(zip.open());
        QVERIFY(zip.addFile(provider == "curseforge" ? "manifest.json" : "modrinth.index.json", manifest));
        QVERIFY(zip.close());
        QString error;
        QVERIFY(!writePackBaseline(dir.path() + "/pack.zip", dir.path() + "/baseline", provider, "1.21.1", {}, {}, &error));
        QVERIFY(!QFile::exists(dir.path() + "/baseline"));
    }
    void linkedReleaseBaselineDoesNotChangeGameFiles()
    {
        QTemporaryDir dir;
        const auto archive = dir.path() + "/pack.zip";
        MMCZip::ArchiveWriter zip(archive);
        QVERIFY(zip.open());
        QVERIFY(zip.addFile("manifest.json", QByteArray(R"({"manifestType":"minecraftModpack","manifestVersion":1,"name":"Pack","version":"1.0","author":"Author","minecraft":{"version":"1.21.1","modLoaders":[{"id":"neoforge-21.1.1","primary":true}]},"files":[]})")));
        QVERIFY(zip.addFile("overrides/config/test.txt", QByteArray("new config")));
        QVERIFY(zip.close());
        write(dir.path() + "/instance/minecraft/config/test.txt", "user config");
        write(dir.path() + "/instance/minecraft/saves/world/level.dat", "world");
        QString error;
        QVERIFY2(writePackBaseline(archive, dir.path() + "/baseline", "curseforge", "1.21.1", "net.neoforged", "21.1.1", &error), qPrintable(error));
        QVERIFY(QFile::exists(dir.path() + "/baseline/manifest.json"));
        QFile overrides(dir.path() + "/baseline/overrides.txt");
        QVERIFY(overrides.open(QIODevice::ReadOnly));
        QCOMPARE(overrides.readAll(), QByteArray("config/test.txt\n"));
        QFile config(dir.path() + "/instance/minecraft/config/test.txt");
        QVERIFY(config.open(QIODevice::ReadOnly));
        QCOMPARE(config.readAll(), QByteArray("user config"));
        QVERIFY(!writePackBaseline(archive, dir.path() + "/wrong", "curseforge", "1.20.1", "net.neoforged", "21.1.1", &error));
        QVERIFY(!writePackBaseline(archive, dir.path() + "/wrong-loader", "curseforge", "1.21.1", "net.fabricmc.fabric-loader", "0.16.0", &error));
    }
    void modrinthBaselineAndUnsafeArchives()
    {
        QTemporaryDir dir;
        const auto archive = dir.path() + "/pack.mrpack";
        {
            MMCZip::ArchiveWriter zip(archive);
            QVERIFY(zip.open());
            QVERIFY(zip.addFile("modrinth.index.json", QByteArray(R"({"formatVersion":1,"game":"minecraft","versionId":"1.0","name":"Pack","dependencies":{"minecraft":"1.21.1","fabric-loader":"0.16.0"},"files":[]})")));
            QVERIFY(zip.addFile("client-overrides/config/client.txt", QByteArray("config")));
            QVERIFY(zip.close());
        }
        QString error;
        QVERIFY2(writePackBaseline(archive, dir.path() + "/baseline", "modrinth", "1.21.1", "net.fabricmc.fabric-loader", "0.16.0", &error), qPrintable(error));
        QVERIFY(QFile::exists(dir.path() + "/baseline/modrinth.index.json"));
        {
            MMCZip::ArchiveWriter zip(dir.path() + "/unsafe.zip");
            QVERIFY(zip.open());
            QVERIFY(zip.addFile("../escape", QByteArray("invalid")));
            QVERIFY(zip.close());
        }
        QVERIFY(!writePackBaseline(dir.path() + "/unsafe.zip", dir.path() + "/unsafe", "modrinth", "1.21.1", "net.fabricmc.fabric-loader", "0.16.0", &error));
        QVERIFY(!QFile::exists(dir.path() + "/escape"));
        MMCZip::ArchiveWriter oversized(dir.path() + "/oversized.zip");
        QVERIFY(oversized.open());
        QVERIFY(oversized.addFile("modrinth.index.json", QByteArray(16 * 1024 * 1024 + 1, ' ')));
        QVERIFY(oversized.close());
        QVERIFY(!writePackBaseline(dir.path() + "/oversized.zip", dir.path() + "/oversized", "modrinth", "1.21.1", "net.fabricmc.fabric-loader", "0.16.0", &error));
    }
};
QTEST_GUILESS_MAIN(LocalInstanceImportTest)
#include "LocalInstanceImport_test.moc"
