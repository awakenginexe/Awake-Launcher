// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ModrinthService.h"
#include "CurseForgeService.h"
#include "awake/integration/OperationStore.h"
#include <QJsonArray>
#include "awake/integration/PhysicalInstanceLease.h"
namespace Awake::Core {
QJsonObject readRecord(const QString& path);
void writeRecord(const QString& path, const QJsonObject& value);
void requireResult(const QJsonObject& response);
class WorkspaceServices {
public:
    WorkspaceServices(QString profile, Awake::Integration::OperationStore& store, std::shared_ptr<std::atomic_bool> cancel = {}, ModrinthService::Fetch fixture = {});
    void saveContext(const QString& id, const QString& method, const QJsonObject& request);
    QJsonObject context(const QString& id) const;
    void create(const QString& id, const QJsonObject& request);
    void prepare(const QString& id, const QJsonObject& request, const QJsonArray& overrides = {}, const QJsonArray& importWarnings = {});
    QJsonObject inspectImport(const QJsonObject& request);
    void prepareImport(const QString& id, const QJsonObject& request);
    void apply(const QString& id, const QJsonObject& request);
    void recover(const QString& id);
    void startupRecovery();
    QJsonObject list(const QJsonObject& params);
    QJsonObject inventory(const QJsonObject& params);
    QJsonObject updates(const QJsonObject& params);
    QJsonObject plan(const QJsonObject& params);
    QJsonObject operation(const QString& id) const;
    QJsonObject recoveryStatus();
    bool unsafeToExit() const;
    bool legacyCompatible(const QString& physicalId) const;
    QString operationDirectory(const QString& id) const;
private:
    QString m_profile;
    Awake::Integration::OperationStore& m_store;
    ModrinthService m_provider;
    CurseForgeService m_curseforge;
    QJsonObject workspaceRecord(const QString& physicalId) const;
    QJsonObject workspaceView(const QJsonObject& record) const;
    QString root(const QJsonObject& record) const;
    std::shared_ptr<Awake::Integration::PhysicalInstanceLease> acquire(const QJsonObject& record) const;
    QJsonArray scan(const QJsonObject& record) const;
    QJsonObject policies(const QJsonObject& record) const;
};
}
