// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QTemporaryDir>
#include <QVariantMap>
#include <functional>
#include "minecraft/auth/MinecraftAccount.h"

class QNetworkReply;
namespace Awake::Web {
class Assets;
class Skins final : public QObject {
    Q_OBJECT
public:
    Skins(Assets* assets, QObject* parent, const QString& libraryDir = {});
    ~Skins() override;
    QVariantMap state(const QString& accountId);
    QVariantMap command(const QString& requestId, const QString& accountId, const QString& command, const QVariantMap& payload);
    QString headUrl(const MinecraftAccountPtr& account);
signals:
    void finished(QString requestId, QVariantMap result);
    void changed();
    void modalChanged(bool active);
private:
    struct Entry { QString path, hash; QVariantMap view; };
    MinecraftAccountPtr account(const QString& id) const;
    QVariantMap addSkin(const QString& id, const QString& name, const QString& path, const QString& variant);
    QVariantMap importSkin(const QString& accountId, const QString& name, const QByteArray& bytes, const QString& variant);
    QVariantList savedSkins();
    bool validSelection(const QString& id, const QString& accountId, const QString& variant) const;
    void download(const QUrl& url, std::function<void(QByteArray, QString)> done);
    void loadCapes(const QString& requestId, const QString& accountId, QStringList ids);
    void complete(const QString& requestId, const QString& accountId, const QString& error = {});
    QPointer<Assets> m_assets;
    QTemporaryDir m_files;
    QString m_libraryDir;
    QHash<QString, Entry> m_skins;
    QHash<QString, QPair<QByteArray, QString>> m_heads;
    QHash<QString, QPair<QByteArray, QString>> m_capes;
    QPointer<QNetworkReply> m_reply;
    Task::Ptr m_task;
    bool m_busy = false;
};
}
