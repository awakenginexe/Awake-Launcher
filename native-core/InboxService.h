// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QString>

namespace Awake::Core {
class InboxService {
public:
    explicit InboxService(QString profile);
    QJsonObject begin(const QJsonObject &params);
    QJsonObject write(const QJsonObject &params);
    QJsonObject commit(const QJsonObject &params);
    QJsonObject status(const QJsonObject &params);
    QJsonObject discard(const QJsonObject &params);
    QString committedPath(const QString &token);
private:
    QString m_directory;
};
}
