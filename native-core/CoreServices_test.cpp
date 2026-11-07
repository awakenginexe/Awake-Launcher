// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include <QJsonArray>
#include <QTemporaryDir>
#include "Contract.h"
#include "ModrinthService.h"
using namespace Awake::Core;
class CoreServicesTest : public QObject {
    Q_OBJECT
private slots:
    void contractRejectsUnknownFieldsAndUnsafeIdentities()
    {
        Contract contract;
        QVERIFY(contract.valid("operations.get.params", QJsonObject{{"operationId", "9999c466-d65a-4d30-9bbc-fbdf385144dd"}}));
        QVERIFY(!contract.valid("operations.get.params", QJsonObject{{"operationId", "../elsewhere"}}));
        QVERIFY(!contract.valid("core.info.params", QJsonObject{{"path", "C:/private"}}));
        QVERIFY(!contract.valid("plans.prepare.params", QJsonObject{}));
        QVERIFY(contract.capabilities(false)["transactions.apply"] == false);
        const QJsonObject client{{"name", "fixture"}, {"version", "1"}};
        QVERIFY(!contract.valid("hello.params", QJsonObject{{"versions", QJsonArray{}}, {"client", client}}));
        QVERIFY(contract.valid("hello.params", QJsonObject{{"versions", QJsonArray{QJsonObject{{"major", 3}, {"minor", 0}}, QJsonObject{{"major", 2}, {"minor", 0}}}}, {"client", client}}));
    }
    void projectLookupUsesProviderMetadata()
    {
        ModrinthService provider({},[](const QUrl& url,qint64) {
            if (url.path() != "/v2/project/project-id") throw ServiceError{"INVALID_REQUEST","Unexpected route"};
            return QByteArray(R"({"id":"project-id","slug":"friendly","title":"Friendly Name","description":"Description","project_type":"mod","client_side":"required","server_side":"required"})");
        });
        const auto project=provider.lookup("project-id");
        QCOMPARE(project["title"].toString(),QString("Friendly Name"));
        QCOMPARE(project["provider"].toString(),QString("modrinth"));
        QVERIFY(Contract().valid("providers.project.result",QJsonObject{{"project",project}}));
        QVERIFY(!Contract().valid("providers.project.params",QJsonObject{{"provider","modrinth"},{"projectId","../escape"}}));
        QVERIFY(!Contract().valid("outbox.read.params",QJsonObject{{"physicalId","id"},{"instanceRevision",QString(64,'0')},{"path","config/a.txt"},{"offset","0"},{"length",262145}}));
    }
    void downloadIntegrityAndCancellation()
    {
        QTemporaryDir temporary;
        auto cancel = std::make_shared<std::atomic_bool>(false);
        ModrinthService provider(cancel, [](const QUrl&, qint64) { return QByteArray("actual jar bytes"); });
        QJsonObject artifact{{"url", "https://cdn.modrinth.com/data/a.jar"}, {"size", "16"}, {"sha512", QString(128, '0')}, {"sha1", QString(40, '0')}};
        QVERIFY_THROWS_EXCEPTION(ServiceError, provider.download(artifact, temporary.filePath("bad.jar")));
        QVERIFY(!QFileInfo::exists(temporary.filePath("bad.jar")));
        cancel->store(true);
        QVERIFY_THROWS_EXCEPTION(ServiceError, provider.search({{"query", "sodium"}, {"minecraftVersion", "1.21.1"}, {"loader", "fabric"}}));
    }
    void liveModrinthDownload()
    {
        if (!qEnvironmentVariableIsSet("AWAKE_CORE_LIVE_TESTS")) QSKIP("Explicit network fixture opt-in required.");
        QTemporaryDir temporary; ModrinthService provider;
        try {
            const QJsonObject params{{"provider", "modrinth"}, {"minecraftVersion", "1.21.1"}, {"loader", "fabric"}, {"query", "sodium extra"}};
            const auto search = provider.search(params); Contract contract;
            QVERIFY(contract.valid("providers.search.result", search)); QVERIFY(!search["projects"].toArray().isEmpty());
            auto versionsParams = params; versionsParams.remove("query"); versionsParams["projectId"] = "sodium-extra";
            const auto versions = provider.versions(versionsParams);
            QVERIFY(contract.valid("providers.versions.result", versions)); QVERIFY(!versions["versions"].toArray().isEmpty());
            const auto selected = versions["versions"].toArray().first().toObject();
            const auto resolved = provider.version(selected["versionId"].toString(), "1.21.1", "fabric");
            const auto artifact = resolved.wire["files"].toArray().first().toObject();
            provider.download(artifact, temporary.filePath("real.jar"));
            QCOMPARE(QFileInfo(temporary.filePath("real.jar")).size(), artifact["size"].toString().toLongLong());
            qInfo() << "Real Modrinth artifact verified:" << resolved.wire["projectId"] << resolved.wire["versionId"] << artifact["sha512"];
        } catch (const ServiceError& error) { QFAIL(qPrintable(error.code + ": " + error.message)); }
    }
};
QTEST_GUILESS_MAIN(CoreServicesTest)
#include "CoreServices_test.moc"
