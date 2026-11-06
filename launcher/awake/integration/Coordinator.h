// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QObject>
#include <QThreadPool>
#include "IntegrationServer.h"
class InstanceList;
class MinecraftInstance;
namespace Awake::Integration {
class Coordinator final : public QObject {
   public:
    using ActivityReader = std::function<QString(MinecraftInstance*)>;
    Coordinator(InstanceList* instances,
                const QString& installRoot,
                const QString& dataRoot,
                bool portable,
                ActivityReader activityReader,
                QObject* parent = nullptr);
    bool start(QString* reason = nullptr);
    void stop();

   private:
    void handle(const Request& request, IntegrationServer::Reply reply);
    InstanceList* m_instances;
    ActivityReader m_activityReader;
    QString m_dataRoot, m_profileIdentity;
    QJsonObject m_information;
    IntegrationServer m_server;
    QThreadPool m_workers;
    int m_reads = 0;
};
}  // namespace Awake::Integration
