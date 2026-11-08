// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QDeadlineTimer>
#include <QHash>
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
    using LaunchStarter = std::function<bool(MinecraftInstance*)>;
    Coordinator(InstanceList* instances,
                const QString& installRoot,
                const QString& dataRoot,
                bool portable,
                ActivityReader activityReader,
                LaunchStarter launchStarter,
                QObject* parent = nullptr);
    bool start(QString* reason = nullptr);
    void stop();
    // True only during the moments the Manager is connecting or disconnecting this instance.
    bool isCoordinating(MinecraftInstance* instance);

   private:
    struct LaunchOperation { QString instanceId, state = "queued"; bool started = false; };
    void handle(const Request& request, IntegrationServer::Reply reply);
    bool handleManagerRequest(const Request& request, IntegrationServer::Reply& reply);
    MinecraftInstance* findInstance(const QString& physicalId) const;
    QJsonObject launchStatus(const QString& operationId);
    InstanceList* m_instances;
    ActivityReader m_activityReader;
    LaunchStarter m_launchStarter;
    QHash<QString, QDeadlineTimer> m_coordinating;
    QHash<QString, LaunchOperation> m_launches;
    QHash<QString, QString> m_launchKeys;
    QString m_dataRoot, m_profileIdentity;
    QJsonObject m_information;
    IntegrationServer m_server;
    QThreadPool m_workers;
    int m_reads = 0;
};
}  // namespace Awake::Integration
