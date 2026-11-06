// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>
#include "PhysicalInstanceLease.h"
namespace Awake::Integration {
class OperationStore;
struct FileChange {
    QString relativePath;
    QString stagedName;
};
class TransactionEngine {
   public:
    using FaultHook = std::function<bool(const QString& point, int fileIndex)>;
    TransactionEngine(OperationStore& store, QString instanceRoot, QString modsRelative);
    QString revision(QString* error = nullptr) const;
    QJsonObject prepare(const QString& operationId, const QList<FileChange>& changes, const QString& expectedRevision);
    QJsonObject apply(const QString& operationId, const QString& planDigest, const QString& expectedRevision,
                      const std::shared_ptr<PhysicalInstanceLease>& lease, FaultHook fault = {});
    QJsonObject complete(const QString& operationId, const std::shared_ptr<PhysicalInstanceLease>& lease, FaultHook fault = {});
    QJsonObject recover(const QString& operationId, const std::shared_ptr<PhysicalInstanceLease>& lease, FaultHook fault = {});
   private:
    QJsonObject snapshot(QString* error = nullptr) const;
    OperationStore& m_store;
    QString m_root, m_mods;
};
}  // namespace Awake::Integration
