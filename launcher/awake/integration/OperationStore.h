// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QString>
#include <memory>
class QLockFile;
namespace Awake::Integration {
class OperationStore {
   public:
    explicit OperationStore(QString directory);
    ~OperationStore();
    bool open(QString* error = nullptr);
    QJsonObject begin(const QString& method, const QString& key, const QJsonObject& request, const QString& operationId = {});
    QJsonObject get(const QString& operationId) const;
    QJsonObject transition(const QString& operationId, const QString& state, const QJsonObject& result = {}, const QJsonObject& error = {});
    QString directory() const { return m_directory; }
    static bool terminal(const QString& state);

   private:
    QString m_directory;
    std::unique_ptr<QLockFile> m_owner;
    QMap<QString, QJsonObject> m_records;
    mutable QMutex m_mutex;
    quint64 m_sequence = 0;
    QJsonObject status(const QJsonObject& record) const;
    bool save(QJsonObject& record, QString* error);
};
}  // namespace Awake::Integration
