// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Contract.h"
#include "WorkspaceServices.h"
#include "awake/integration/IntegrationServer.h"
#include <QHash>
#include <QElapsedTimer>
#include <QSet>
#include <QThreadPool>
namespace Awake::Core {
class CoreBackend : public QObject {
    friend struct CoreBackendTestAccess;
public:
    explicit CoreBackend(QObject* parent = nullptr);
    ~CoreBackend() override;
    bool start(const QString& profile, QString* error);
private:
    QString m_profile, m_profileId;
    Contract m_contract;
    Awake::Integration::IntegrationServer m_server;
    std::unique_ptr<Awake::Integration::OperationStore> m_store;
    QThreadPool m_pool;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_jobs;
    QSet<QString> m_critical, m_reserved;
    QElapsedTimer m_idle;
    void handle(const Awake::Integration::Request& request, Awake::Integration::IntegrationServer::Reply reply);
    void submit(const Awake::Integration::Request& request, Awake::Integration::IntegrationServer::Reply reply);
    QJsonObject read(const Awake::Integration::Request& request);
};
}
