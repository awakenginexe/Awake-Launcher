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
class ModCatalog;
class InstanceEditor;
class Skins;
class Bridge final : public QObject {
    Q_OBJECT
public:
    using Select = std::function<bool(const QString&)>;
    using Action = std::function<QVariantMap(const QString&, const QString&)>;
    using GpuDetector = std::function<QVariantMap()>;
    Bridge(Assets* assets, Select select, Action action, QObject* parent = nullptr, GpuDetector gpuDetector = {});
    ~Bridge() override;
    void scheduleState();
    void setActive(bool active);
    void setArtworkFocused(bool focused);
    void setModalActive(bool active);
    bool isModalActive() const { return m_modalActive; }

    Q_INVOKABLE QVariantMap snapshot();
    Q_INVOKABLE QVariantMap skinState(const QString& accountId);
    Q_INVOKABLE QVariantMap skinCommand(const QString& requestId, const QString& accountId, const QString& command, const QVariantMap& payload);
    Q_INVOKABLE QVariantMap selectInstance(const QString& id);
    Q_INVOKABLE QVariantMap launchInstance(const QString& id);
    Q_INVOKABLE QVariantMap invokeAction(const QString& action, const QString& id);
    Q_INVOKABLE QVariantMap setPreference(const QString& key, const QVariant& value);
    Q_INVOKABLE QVariantMap searchPacks(const QString& requestId, const QString& provider, const QString& query, int offset);
    Q_INVOKABLE QVariantMap packVersions(const QString& requestId, const QString& provider, const QString& packId);
    Q_INVOKABLE QVariantMap instancePackVersions(const QString& requestId, const QString& id);
    Q_INVOKABLE QVariantMap updateInstancePack(const QString& id, const QString& versionId);
    Q_INVOKABLE QVariantMap modSearch(const QString& requestId, const QString& instanceId, const QString& provider, const QString& query, const QString& sort, int offset);
    Q_INVOKABLE QVariantMap modVersions(const QString& requestId, const QString& instanceId, const QString& provider, const QString& projectId);
    Q_INVOKABLE QVariantMap modPrepare(const QString& requestId, const QString& instanceId, const QVariantList& selections);
    Q_INVOKABLE QVariantMap modInstall(const QString& requestId, const QString& instanceId, const QString& reviewId);
    Q_INVOKABLE QVariantMap modCancel(const QString& requestId);
    Q_INVOKABLE QVariantMap minecraftVersions(const QString& requestId);
    Q_INVOKABLE QVariantMap browseArchive(const QString& requestId);
    Q_INVOKABLE QVariantMap instanceDetails(const QString& id, const QString& section);
    Q_INVOKABLE QVariantMap instanceCommand(const QString& id, const QString& command, const QVariant& payload);
    Q_INVOKABLE QVariantMap javaSettings(const QString& id);
    Q_INVOKABLE QVariantMap gpuSettings(const QString& requestId = {});
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
    void modProgress(QString requestId, QVariantMap state);
    void editorChanged(QString id, QString section);
    void accountsRequested();
private:
    QVariantMap fail(const QString& operation, const QString& detail);
    void discoverGpuHardware();
    QVariantMap cachedGpuSettings() const;
    void observeInstances();
    void requestArtwork(const QString& id);
    void loadArtwork(const QString& id);
    void invalidateArtwork();
    QString iconUrl(MinecraftInstance* instance);
    QPointer<Assets> m_assets;
    PackCatalog* m_packCatalog = nullptr;
    ModCatalog* m_modCatalog = nullptr;
    InstanceEditor* m_instanceEditor = nullptr;
    Skins* m_skins = nullptr;
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
    QTimer* m_playtimeTimer = nullptr;
    std::shared_ptr<std::atomic_bool> m_canceled;
    quint64 m_artworkGeneration = 0;
    bool m_stateScheduled = false;
    bool m_active = true;
    bool m_artworkFocused = false;
    bool m_modalActive = false;
    bool m_actionPending = false;
    GpuDetector m_gpuDetector;
    QVariantMap m_gpuHardware;
    QSet<QString> m_gpuRequests;
    bool m_gpuPending = false;
};
}  // namespace Awake::Web
