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

class AwakeUpdateCheckerTest : public QObject {
    Q_OBJECT
    QByteArray release(QString tag = "v0.3.0", bool prerelease = false, QString url = {}) {
        const QString base = "https://github.com/awakenginexe/Awake-Launcher/releases/download/" + tag + "/";
        return QJsonDocument(QJsonObject{
            {"tag_name", tag}, {"draft", false}, {"prerelease", prerelease}, {"body", "Release notes <script>test</script>"},
            {"html_url", "https://github.com/awakenginexe/Awake-Launcher/releases/tag/" + tag},
            {"assets", QJsonArray{
                QJsonObject{{"name", "Awake-Launcher-" + tag + "-Windows-x64-Setup.exe"}, {"browser_download_url", url.isEmpty() ? base + "Awake-Launcher-" + tag + "-Windows-x64-Setup.exe" : url}},
                QJsonObject{{"name", "Awake-Launcher-" + tag + "-Windows-x64.zip"}, {"browser_download_url", base + "Awake-Launcher-" + tag + "-Windows-x64.zip"}}
            }}
        }).toJson();
    }
private slots:
    void newerRelease() {
        const auto state = Awake::parseUpdateRelease(release(), "0.2.0");
        QCOMPARE(state.value("status").toString(), "available");
        QCOMPARE(state.value("latestVersion").toString(), "0.3.0");
        QVERIFY(state.value("setupUrl").toString().endsWith("-Setup.exe"));
        QVERIFY(state.value("portableUrl").toString().endsWith(".zip"));
        QCOMPARE(state.value("notes").toString(), "Release notes <script>test</script>");
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
