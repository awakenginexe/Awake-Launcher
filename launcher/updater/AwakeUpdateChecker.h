// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QByteArray>
#include <QVariantMap>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include "ExternalUpdater.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace Awake {
QVariantMap parseUpdateRelease(const QByteArray& body, const QString& currentVersion);

class UpdateChecker final : public ExternalUpdater {
    Q_OBJECT
public:
    UpdateChecker(QNetworkAccessManager* network, const QString& currentVersion, const QString& dataDir,
                  bool portable, QObject* parent = nullptr,
                  QUrl endpoint = QUrl("https://api.github.com/repos/awakenginexe/Awake-Launcher/releases/latest"));
    ~UpdateChecker() override;
    QVariantMap state() const;
    bool openDownload(const QString& kind);
    void acknowledgeNotification();
    void checkForUpdates() override { check(true); }
    bool getAutomaticallyChecksForUpdates() override;
    double getUpdateCheckInterval() override { return 86400; }
    bool getBetaAllowed() override { return false; }
    void setAutomaticallyChecksForUpdates(bool enabled) override;
    void setUpdateCheckInterval(double) override {}
    void setBetaAllowed(bool) override {}
signals:
    void stateChanged();
private:
    void check(bool manual);
    void armTimer();
    QNetworkAccessManager* m_network;
    QSettings m_settings;
    QTimer m_timer;
    QTimer m_deadline;
    QPointer<QNetworkReply> m_reply;
    QUrl m_endpoint;
    QString m_currentVersion;
    QVariantMap m_state;
    QByteArray m_body;
    bool m_portable;
    bool m_tooLarge = false;
    bool m_manual = false;
    int m_presentation = 0;
};
}
