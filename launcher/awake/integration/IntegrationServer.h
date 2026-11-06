// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QSet>
#include <functional>
#include <memory>
#include "Protocol.h"
class QLocalSocket;
namespace Awake::Integration {
class IntegrationServer : public QObject {
   public:
    using Reply = std::function<void(QJsonObject)>;
    using Handler = std::function<void(const Request&, Reply)>;
    struct Options {
        int major = 1;
        QString directory = "v1", prefix = "awake-manager-v1-";
        QJsonObject capabilities, hello;
        std::function<bool(const QString&, const QJsonObject&)> validate;
    };
    explicit IntegrationServer(QObject* parent = nullptr);
    ~IntegrationServer() override;
    bool start(const QString& dataRoot, QJsonObject info, Handler handler, QString* reason = nullptr);
    bool start(const QString& dataRoot, QJsonObject info, Handler handler, Options options, QString* reason = nullptr);
    void stop();
    QString serverName() const { return m_server.serverName(); }
    QJsonObject information() const { return m_info; }
    QString descriptorPath() const { return m_descriptor; }
    qsizetype clientCount() const { return m_clients.size(); }

   private:
    void acceptClients();
    QLocalServer m_server;
    QSet<QLocalSocket*> m_clients;
    std::unique_ptr<QLockFile> m_owner;
    QString m_descriptor;
    QJsonObject m_info;
    Handler m_handler;
    Options m_options;
    QJsonObject m_capabilities;
};
}  // namespace Awake::Integration
