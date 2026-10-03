// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QVariantMap>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <atomic>
#include <functional>
#include <memory>

class MinecraftInstance;
namespace Awake::Web {
class Assets;
class Bridge final : public QObject {
    Q_OBJECT
public:
    using Select = std::function<bool(const QString&)>;
    using Action = std::function<QVariantMap(const QString&, const QString&)>;
    Bridge(Assets* assets, Select select, Action action, QObject* parent = nullptr);
    ~Bridge() override;
    void scheduleState();
    void setActive(bool active);

    Q_INVOKABLE QVariantMap snapshot();
    Q_INVOKABLE QVariantMap selectInstance(const QString& id);
    Q_INVOKABLE QVariantMap launchInstance(const QString& id);
    Q_INVOKABLE QVariantMap invokeAction(const QString& action, const QString& id);
    Q_INVOKABLE QVariantMap setPreference(const QString& key, const QVariant& value);
    Q_INVOKABLE void frontendReady();
signals:
    void stateChanged(QVariantMap state);
    void artworkChanged(QString id, QString url, QString error);
    void operationFailed(QString operation, QString detail);
    void ready();
private:
    QVariantMap fail(const QString& operation, const QString& detail);
    void observeInstances();
    void requestArtwork(const QString& id);
    void invalidateArtwork();
    QString iconUrl(MinecraftInstance* instance);
    QPointer<Assets> m_assets;
    Select m_select;
    Action m_action;
    QSet<MinecraftInstance*> m_observed;
    QHash<QString, QPair<QString, QString>> m_icons;
    QString m_artworkId, m_artworkUrl;
    std::shared_ptr<std::atomic_bool> m_canceled;
    quint64 m_artworkGeneration = 0;
    bool m_stateScheduled = false;
    bool m_active = true;
    bool m_actionPending = false;
};
}  // namespace Awake::Web
