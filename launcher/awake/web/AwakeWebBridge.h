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
#include "tasks/Task.h"

class MinecraftInstance;
class Task;
class QTimer;
namespace Awake::Web {
class Assets;
class PackCatalog;
class InstanceEditor;
class Bridge final : public QObject {
    Q_OBJECT
public:
    using Select = std::function<bool(const QString&)>;
    using Action = std::function<QVariantMap(const QString&, const QString&)>;
    Bridge(Assets* assets, Select select, Action action, QObject* parent = nullptr);
    ~Bridge() override;
    void scheduleState();
    void setActive(bool active);
    void setArtworkFocused(bool focused);
    void setModalActive(bool active);
    bool isModalActive() const { return m_modalActive; }

    Q_INVOKABLE QVariantMap snapshot();
    Q_INVOKABLE QVariantMap selectInstance(const QString& id);
    Q_INVOKABLE QVariantMap launchInstance(const QString& id);
    Q_INVOKABLE QVariantMap invokeAction(const QString& action, const QString& id);
    Q_INVOKABLE QVariantMap setPreference(const QString& key, const QVariant& value);
    Q_INVOKABLE QVariantMap searchPacks(const QString& requestId, const QString& provider, const QString& query, int offset);
    Q_INVOKABLE QVariantMap packVersions(const QString& requestId, const QString& provider, const QString& packId);
    Q_INVOKABLE QVariantMap minecraftVersions(const QString& requestId);
    Q_INVOKABLE QVariantMap browseArchive(const QString& requestId);
    Q_INVOKABLE QVariantMap instanceDetails(const QString& id, const QString& section);
    Q_INVOKABLE QVariantMap instanceCommand(const QString& id, const QString& command, const QVariant& payload);
    Q_INVOKABLE QVariantMap javaSettings(const QString& id);
    Q_INVOKABLE QVariantMap gpuSettings();
    Q_INVOKABLE QVariantMap openUpdateDownload(const QString& kind);
    Q_INVOKABLE QVariantMap setAutomaticUpdates(bool enabled);
    Q_INVOKABLE QVariantMap acknowledgeUpdateNotification();
    Q_INVOKABLE QVariantMap setGpuPreference(const QString& mode);
    Q_INVOKABLE QVariantMap openGpuSettings();
    Q_INVOKABLE QVariantMap setJavaProfile(const QString& id, const QString& profile);
    Q_INVOKABLE QVariantMap browseJava(const QString& requestId, const QString& id);
    PackCatalog* packCatalog() const { return m_packCatalog; }
    Q_INVOKABLE void frontendReady();
signals:
    void stateChanged(QVariantMap state);
    void modalChanged(bool active);
    void artworkChanged(QString id, QString url, QString error);
    void operationFailed(QString operation, QString detail);
    void ready();
    void catalogFinished(QString requestId, QVariantMap response);
    void editorChanged(QString id, QString section);
    void accountsRequested();
private:
    QVariantMap fail(const QString& operation, const QString& detail);
    void observeInstances();
    void requestArtwork(const QString& id);
    void loadArtwork(const QString& id);
    void invalidateArtwork();
    QString iconUrl(MinecraftInstance* instance);
    QPointer<Assets> m_assets;
    PackCatalog* m_packCatalog = nullptr;
    InstanceEditor* m_instanceEditor = nullptr;
    Task::Ptr m_minecraftTask;
    Task::Ptr m_javaTask;
    bool m_javaPending = false;
    Select m_select;
    Action m_action;
    QSet<MinecraftInstance*> m_observed;
    QHash<QString, QPair<QString, QString>> m_icons;
    QString m_artworkId, m_artworkUrl;
    QString m_artworkPath;
    QTimer* m_artworkTimer = nullptr;
    std::shared_ptr<std::atomic_bool> m_canceled;
    quint64 m_artworkGeneration = 0;
    bool m_stateScheduled = false;
    bool m_active = true;
    bool m_artworkFocused = false;
    bool m_modalActive = false;
    bool m_actionPending = false;
};
}  // namespace Awake::Web
