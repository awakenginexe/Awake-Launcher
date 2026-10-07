// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSysInfo>
#include <QUuid>
#include "awake/StartupTelemetry.h"

class StartupTelemetryTest : public QObject {
    Q_OBJECT
private slots:
    void sendsOnlyOneAnonymousEventEvenWhenTheServerFails()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray request;
        int captures = 0;
        QJsonObject payload;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                request += socket->readAll();
                const auto split = request.indexOf("\r\n\r\n");
                if (split < 0) return;
                payload = QJsonDocument::fromJson(request.mid(split + 4)).object();
                if (payload.isEmpty()) return;
                ++captures;
                socket->write("HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        });
        QNetworkAccessManager network;
        const auto host = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        network.cookieJar()->setCookiesFromUrl({QNetworkCookie("private_fixture", "must_not_be_sent")}, host);
        Awake::StartupTelemetry disabled(&network, "", host, "1.3.0");
        disabled.start();
        QTest::qWait(20);
        QCOMPARE(captures, 0);
        Awake::StartupTelemetry telemetry(&network, "phc_fixture", host, "1.3.0");
        telemetry.start(); telemetry.start();
        QTRY_COMPARE(captures, 1);
        QVERIFY(request.startsWith("POST /i/v0/e/ HTTP/1.1"));
        QVERIFY(!request.contains("Cookie:") && !request.contains("Authorization:"));
        QCOMPARE(payload.keys(), QStringList({"api_key", "distinct_id", "event", "properties"}));
        QCOMPARE(payload["event"].toString(), QString("app_started"));
        QCOMPARE(payload["api_key"].toString(), QString("phc_fixture"));
        QVERIFY(!QUuid(payload["distinct_id"].toString()).isNull());
        QCOMPARE(payload["properties"].toObject(), (QJsonObject{{"app_version", "1.3.0"}, {"os", QSysInfo::kernelType()}, {"$process_person_profile", false}}));
        telemetry.start(); QTest::qWait(50);
        QCOMPARE(captures, 1);
        const auto firstId = payload["distinct_id"].toString();
        request.clear();
        Awake::StartupTelemetry nextSession(&network, "phc_fixture", host, "1.3.0");
        nextSession.start();
        QTRY_COMPARE(captures, 2);
        QVERIFY(payload["distinct_id"].toString() != firstId);
    }
};
QTEST_GUILESS_MAIN(StartupTelemetryTest)
#include "StartupTelemetry_test.moc"
