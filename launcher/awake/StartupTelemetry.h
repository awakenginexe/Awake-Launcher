// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QUrl>
class QNetworkAccessManager;

namespace Awake {
class StartupTelemetry : public QObject {
public:
    StartupTelemetry(QNetworkAccessManager* network, QString token, QUrl host, QString version, QObject* parent = nullptr);
    void start();
private:
    QNetworkAccessManager* m_network;
    QString m_token, m_version;
    QUrl m_host;
    bool m_sent = false;
};
}
