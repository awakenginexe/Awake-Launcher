// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QSignalSpy>
#include "updater/AwakeUpdateChecker.h"

class LocalUpdateNetwork : public QNetworkAccessManager {
public:
    QUrl endpoint;
    QStringList paths;
protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* outgoingData) override {
        paths.append(request.url().path());
        auto local = request;
        auto url = endpoint;
        url.setPath(request.url().path());
        local.setUrl(url);
        return QNetworkAccessManager::createRequest(operation, local, outgoingData);
    }
};

class AwakeUpdateCheckerTest : public QObject {
    Q_OBJECT
    QByteArray release(QString tag = "v0.3.0", bool prerelease = false, QString url = {}) {
        const QString base = "https://github.com/awakenginexe/Awake-Launcher/releases/download/" + tag + "/";
        return QJsonDocument(QJsonObject{
            {"tag_name", tag}, {"draft", false}, {"prerelease", prerelease}, {"body", "Release notes <script>test</script>"},
            {"html_url", "https://github.com/awakenginexe/Awake-Launcher/releases/tag/" + tag},
            {"assets", QJsonArray{
                QJsonObject{{"name", "Awake-Launcher-" + tag + "-Windows-x64-Setup.exe"}, {"size", 5}, {"digest", "sha256:2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"}, {"browser_download_url", url.isEmpty() ? base + "Awake-Launcher-" + tag + "-Windows-x64-Setup.exe" : url}},
                QJsonObject{{"name", "Awake-Launcher-" + tag + "-Windows-x64.zip"}, {"browser_download_url", base + "Awake-Launcher-" + tag + "-Windows-x64.zip"}}
            }}
        }).toJson();
    }
private slots:
    void installerDownload_data() {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<int>("httpStatus");
        QTest::addColumn<bool>("valid");
        QTest::newRow("verified") << QByteArray("hello") << 200 << true;
        QTest::newRow("tampered") << QByteArray("evil!") << 200 << false;
        QTest::newRow("truncated") << QByteArray("hell") << 200 << false;
        QTest::newRow("oversized") << QByteArray("hello!") << 200 << false;
        QTest::newRow("http-error") << QByteArray("hello") << 404 << false;
    }
    void installerDownload() {
        QFETCH(QByteArray, payload);
        QFETCH(int, httpStatus);
        QFETCH(bool, valid);
        QTemporaryDir data;
        QSettings settings(data.filePath("awake_update.cfg"), QSettings::IniFormat);
        settings.setValue("automatic", false); settings.sync();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const bool metadata = socket->readAll().startsWith("GET /latest ");
                const auto body = metadata ? release() : payload;
                socket->write("HTTP/1.1 " + QByteArray::number(metadata ? 200 : httpStatus) + " Response\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        LocalUpdateNetwork network;
        network.endpoint = QUrl(QString("http://127.0.0.1:%1/latest").arg(server.serverPort()));
        Awake::UpdateChecker checker(&network, "0.2.0", data.path(), false, nullptr, network.endpoint);
        QSignalSpy ready(&checker, &Awake::UpdateChecker::installerReady);
        QVERIFY(!checker.installUpdate(data.path()));
        checker.checkForUpdates();
        QTRY_COMPARE(checker.state().value("status").toString(), "available");
        QVERIFY(checker.openDownload("setup"));
        QCOMPARE(checker.state().value("status").toString(), "downloading");
        QVERIFY(!checker.openDownload("setup"));
        checker.checkForUpdates();
        QTRY_COMPARE(checker.state().value("status").toString(), valid ? "installing" : "available");
        QCOMPARE(ready.count(), valid ? 1 : 0);
        QCOMPARE(network.paths.size(), 2);
        if (valid) QCOMPARE(checker.state().value("progress").toInt(), 100);
        else {
            QVERIFY(!checker.state().value("error").toString().isEmpty());
            QVERIFY(!checker.installUpdate(data.path()));
        }
    }
    void startupChecksEvenAfterRecentDismissal() {
        QTemporaryDir data;
        QSettings settings(data.filePath("awake_update.cfg"), QSettings::IniFormat);
        settings.setValue("last_check", QDateTime::currentSecsSinceEpoch());
        settings.setValue("notified_version", "0.3.0"); settings.sync();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const auto body = release();
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [socket, body] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        QNetworkAccessManager network;
        Awake::UpdateChecker checker(&network, "0.2.0", data.path(), false, nullptr,
                                    QUrl(QString("http://127.0.0.1:%1/latest").arg(server.serverPort())));
        QTRY_COMPARE(checker.state().value("status").toString(), "available");
        QCOMPARE(checker.state().value("presentation").toInt(), 1);
    }
    void requiresInstallerIntegrityMetadata() {
        auto document = QJsonDocument::fromJson(release()).object();
        auto assets = document.value("assets").toArray();
        auto setup = assets[0].toObject();
        for (const auto& digest : {QString(), QString("sha256:bad")}) {
            setup["digest"] = digest;
            assets[0] = setup;
            document["assets"] = assets;
            QVERIFY(Awake::parseUpdateRelease(QJsonDocument(document).toJson(), "0.2.0").value("setupDigest").toString().isEmpty());
        }
    }
    void newerRelease() {
        const auto state = Awake::parseUpdateRelease(release(), "0.2.0");
        QCOMPARE(state.value("status").toString(), "available");
        QCOMPARE(state.value("latestVersion").toString(), "0.3.0");
        QVERIFY(state.value("setupUrl").toString().endsWith("-Setup.exe"));
        QVERIFY(state.value("portableUrl").toString().endsWith(".zip"));
        QCOMPARE(state.value("notes").toString(), "Release notes <script>test</script>");
        QCOMPARE(state.value("setupSize").toLongLong(), 5);
        QCOMPARE(state.value("setupDigest").toByteArray(), QByteArray("2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"));
    }
    void numericComparison() {
        QCOMPARE(Awake::parseUpdateRelease(release("v0.10.0"), "0.9.0").value("status").toString(), "available");
        QCOMPARE(Awake::parseUpdateRelease(release("v0.3.0"), "0.3.0").value("status").toString(), "upToDate");
        QCOMPARE(Awake::parseUpdateRelease(release("v0.2.0"), "0.3.0").value("status").toString(), "upToDate");
    }
    void rejectsInvalidReleases() {
        for (const auto& body : {QByteArray("garbage"), release("v0.4.0-rc.1"), release("v0.4.0", true), release("v0.4.0/../../evil")})
            QCOMPARE(Awake::parseUpdateRelease(body, "0.3.0").value("status").toString(), "error");
    }
    void rejectsForeignDownload() {
        for (const QString& url : {QString("https://github.com/attacker/other/releases/download/v0.4.0/setup.exe"), QString("file:///C:/evil.exe"), QString("https://github.com.evil.test/setup.exe")}) {
            const auto state = Awake::parseUpdateRelease(release("v0.4.0", false, url), "0.3.0");
            QVERIFY(state.value("setupUrl").toString().isEmpty());
        }
    }
    void asynchronousCheckDeduplicatesRequests() {
        QTemporaryDir data;
        QSettings settings(data.filePath("awake_update.cfg"), QSettings::IniFormat);
        settings.setValue("automatic", false); settings.sync();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests = 0;
        const auto body = release();
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll(); ++requests;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        QNetworkAccessManager network;
        Awake::UpdateChecker checker(&network, "0.2.0", data.path(), true, nullptr,
                                    QUrl(QString("http://127.0.0.1:%1/latest").arg(server.serverPort())));
        checker.checkForUpdates();
        checker.checkForUpdates();
        QCOMPARE(checker.state().value("status").toString(), "checking");
        QTRY_COMPARE_WITH_TIMEOUT(checker.state().value("status").toString(), "available", 3000);
        QCOMPARE(requests, 1);
        QCOMPARE(checker.state().value("presentation").toInt(), 2);
        QVERIFY(settings.value("notified_version").toString().isEmpty());
        checker.acknowledgeNotification();
        QCOMPARE(settings.value("notified_version").toString(), "0.3.0");
        QVERIFY(checker.state().value("portable").toBool());
        checker.setAutomaticallyChecksForUpdates(true);
        QTest::qWait(100);
        QCOMPARE(requests, 1);
        QVERIFY(settings.value("last_check").toLongLong() > 0);
        checker.setAutomaticallyChecksForUpdates(false);
        QCOMPARE(settings.value("automatic").toBool(), false);
        QVERIFY(!checker.openDownload("arbitrary"));
    }
    void networkErrorsAreNotReportedAsUpToDate() {
        QTemporaryDir data;
        QSettings settings(data.filePath("awake_update.cfg"), QSettings::IniFormat);
        settings.setValue("automatic", false); settings.sync();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll(); socket->write("HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        QNetworkAccessManager network;
        Awake::UpdateChecker checker(&network, "0.2.0", data.path(), false, nullptr,
                                    QUrl(QString("http://127.0.0.1:%1/latest").arg(server.serverPort())));
        checker.checkForUpdates();
        QTRY_COMPARE_WITH_TIMEOUT(checker.state().value("status").toString(), "error", 3000);
        QVERIFY(checker.state().value("error").toString().contains("limit"));
    }
};
QTEST_GUILESS_MAIN(AwakeUpdateCheckerTest)
#include "AwakeUpdateChecker_test.moc"
