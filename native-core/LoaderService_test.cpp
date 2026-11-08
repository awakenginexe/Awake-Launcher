// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthService.h"
#include "WorkspaceServices.h"
#include "awake/integration/ProfileAccess.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
using namespace Awake::Core;
using namespace Awake::Integration;

class LoaderServiceTest : public QObject {
    Q_OBJECT
private slots:
    void catalogFiltersMinecraftRequirements()
    {
        ModrinthService service({}, [](const QUrl& url, qint64) {
            if (url.host() == "piston-meta.mojang.com")
                return QByteArray(R"({"versions":[{"id":"1.20.1"},{"id":"1.21.1"}]})");
            if (url.path().contains("net.fabricmc.intermediary"))
                return QByteArray(R"({"formatVersion":1,"uid":"net.fabricmc.intermediary","versions":[{"version":"1.20.1","requires":[{"uid":"net.minecraft","equals":"1.20.1"}]}]})");
            const auto uid = url.path().section('/', -2, -2);
            const QJsonArray requirements = uid == "org.quiltmc.quilt-loader"
                ? QJsonArray{QJsonObject{{"uid", "net.fabricmc.intermediary"}}}
                : QJsonArray{QJsonObject{{"uid", "net.minecraft"}, {"equals", "1.20.1"}}};
            return QJsonDocument(QJsonObject{{"formatVersion", 1}, {"uid", uid}, {"versions", QJsonArray{
                QJsonObject{{"version", "valid"}, {"requires", requirements}}}}}).toJson();
        });
        for (const auto& loader : {"forge", "neoforge", "quilt"}) {
            QVERIFY(service.catalog({{"minecraftVersion", "1.20.1"}, {"loader", loader}})["loaderVersions"].toArray().contains("valid"));
            QVERIFY(service.catalog({{"minecraftVersion", "1.21.1"}, {"loader", loader}})["loaderVersions"].toArray().isEmpty());
        }
    }
    void liveWorkspaceCreation_data()
    {
        QTest::addColumn<QString>("game");
        QTest::addColumn<QString>("loader");
        QTest::addColumn<QString>("uid");
        QTest::newRow("forge") << QString("1.20.1") << QString("forge") << QString("net.minecraftforge");
        QTest::newRow("fabric") << QString("1.20.1") << QString("fabric") << QString("net.fabricmc.fabric-loader");
        QTest::newRow("quilt") << QString("1.20.1") << QString("quilt") << QString("org.quiltmc.quilt-loader");
        QTest::newRow("neoforge") << QString("1.21.1") << QString("neoforge") << QString("net.neoforged");
        QTest::newRow("vanilla") << QString("1.20.1") << QString("vanilla") << QString();
    }
    void liveWorkspaceCreation()
    {
        if (qEnvironmentVariableIsEmpty("AWAKE_CORE_LIVE_TESTS")) QSKIP("Set AWAKE_CORE_LIVE_TESTS to enable network tests.");
        QFETCH(QString, game); QFETCH(QString, loader); QFETCH(QString, uid);
        try {
            ModrinthService provider;
            const auto versions = provider.catalog({{"minecraftVersion", game}, {"loader", loader}})["loaderVersions"].toArray();
            if (loader != "vanilla") QVERIFY2(!versions.isEmpty(), "No loader versions were available for the requested game.");
            const auto version = loader == "vanilla" ? QString() : versions.first().toString();
            QTemporaryDir temporary; QVERIFY(temporary.isValid());
            const auto profile = temporary.path() + "/core";
            QVERIFY(Awake::Integration::createPrivateStorageDirectory(profile));
            for (const auto* child : {"catalog", "ownership", "workspaces"})
                QVERIFY(Awake::Integration::createPrivateStorageDirectory(profile + '/' + child));
            OperationStore store(profile + "/operations"); QVERIFY(store.open());
            WorkspaceServices services(profile, store);
            const QJsonObject request{{"idempotencyKey", "live-create"}, {"name", "Disposable live fixture"},
                {"minecraftVersion", game}, {"loader", QJsonObject{{"id", loader}, {"version", version}}}};
            const auto begun = store.begin("workspaces.create", "live-create", request); requireResult(begun);
            const auto id = begun["result"].toObject()["operationId"].toString(); QVERIFY(!id.isEmpty());
            services.saveContext(id, "workspaces.create", request); services.create(id, request);
            const auto result = services.operation(id); QCOMPARE(result["state"].toString(), QString("completed"));
            QFile metadata(profile + "/workspaces/" + id + "/mmc-pack.json"); QVERIFY(metadata.open(QIODevice::ReadOnly));
            const auto pack = QJsonDocument::fromJson(metadata.readAll()).object(); QCOMPARE(pack["formatVersion"].toInt(), 1);
            const auto components = pack["components"].toArray(); bool foundGame = false, foundLoader = uid.isEmpty(), foundIntermediary = false;
            for (const auto& value : components) {
                const auto component = value.toObject();
                if (component["uid"] == "net.minecraft" && component["version"] == game) foundGame = true;
                if (component["uid"] == uid && component["version"] == version) foundLoader = true;
                if (component["uid"] == "net.fabricmc.intermediary" && component["version"] == game) foundIntermediary = true;
            }
            QVERIFY(foundGame); QVERIFY(foundLoader);
            QCOMPARE(foundIntermediary, loader == "fabric" || loader == "quilt");
            if (loader == "neoforge") {
                const auto other = provider.catalog({{"minecraftVersion", "1.20.1"}, {"loader", "neoforge"}})["loaderVersions"].toArray();
                QVERIFY2(!other.contains(version), "The 1.21.1 NeoForge version must not be accepted for 1.20.1.");
            }
        } catch (const ServiceError& error) { QFAIL(qPrintable(error.code + ": " + error.message)); }
    }
};
QTEST_GUILESS_MAIN(LoaderServiceTest)
#include "LoaderService_test.moc"
