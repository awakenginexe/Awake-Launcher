// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeWebBridge.h"
#include "AwakeWebAssets.h"
#include "AwakeWebPolicy.h"
#include "Application.h"
#include "InstanceList.h"
#include "awake/InstanceArtwork.h"
#include "icons/IconList.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/auth/AccountList.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"
#include "translations/TranslationsModel.h"
#include <QBuffer>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrentRun>

namespace Awake::Web {
namespace {
QVariantMap success() { return {{"ok", true}}; }
struct ArtworkResult { QByteArray png; QString error; };
QString componentVersion(const ComponentPtr& component)
{
    if (!component) return {};
    const auto resolved = component->getVersion();
    return resolved.isEmpty() ? component->m_version : resolved;
}
}

Bridge::Bridge(Assets* assets, Select select, Action action, QObject* parent)
    : QObject(parent), m_assets(assets), m_select(std::move(select)), m_action(std::move(action))
{
    const auto changed = [this] { observeInstances(); scheduleState(); };
    auto* instances = APPLICATION->instances();
    connect(instances, &QAbstractItemModel::modelReset, this, changed);
    connect(instances, &QAbstractItemModel::rowsInserted, this, changed);
    connect(instances, &QAbstractItemModel::rowsRemoved, this, changed);
    connect(instances, &QAbstractItemModel::dataChanged, this, [this] { scheduleState(); });
    connect(instances, &QAbstractItemModel::layoutChanged, this, [this] { scheduleState(); });
    connect(APPLICATION->accounts(), &AccountList::defaultAccountChanged, this, [this] { scheduleState(); });
    connect(APPLICATION->accounts(), &AccountList::listChanged, this, [this] { scheduleState(); });
    connect(APPLICATION->settings(), &SettingsObject::SettingChanged, this, [this](const Setting& setting, QVariant value) {
        if (setting.id() == "SelectedInstance" && value.toString() != m_artworkId)
            invalidateArtwork();
        if (QStringList{"SelectedInstance", "AwakeReduceMotion", "AwakeCompactLibrary", "AwakePinnedInstances", "InstSortMode", "Language"}.contains(setting.id()))
            scheduleState();
    });
    connect(APPLICATION->icons(), &IconList::iconUpdated, this, [this](const QString& key) {
        if (!m_active || !m_assets) return;
        for (auto it = m_icons.begin(); it != m_icons.end();) {
            if (it.value().first == key) {
                m_assets->removeImage(it.value().second);
                it = m_icons.erase(it);
            } else {
                ++it;
            }
        }
        scheduleState();
    });
    observeInstances();
}

Bridge::~Bridge() { if (m_canceled) m_canceled->store(true); }

void Bridge::observeInstances()
{
    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); ++i) {
        auto* instance = instances->at(i);
        if (m_observed.contains(instance))
            continue;
        m_observed.insert(instance);
        auto* profile = instance->getPackProfile();
        if (profile) {
            // The Prism component model is lazily loaded. Load its local list once; snapshot reads only the model.
            if (profile->rowCount() == 0 && !profile->getCurrentTask()) {
                if (auto result = profile->reload(Net::Mode::Offline); !result)
                    qWarning() << "Awake component list could not be loaded:" << instance->id() << result.error();
            }
            connect(profile, &QAbstractItemModel::modelReset, this, [this] { scheduleState(); });
            connect(profile, &QAbstractItemModel::dataChanged, this, [this] { scheduleState(); });
            connect(profile, &QAbstractItemModel::rowsInserted, this, [this] { scheduleState(); });
            connect(profile, &QAbstractItemModel::rowsRemoved, this, [this] { scheduleState(); });
        }
        connect(instance, &BaseInstance::runningStatusChanged, this, [this] { scheduleState(); });
        connect(instance, &QObject::destroyed, this, [this, instance] { m_observed.remove(instance); scheduleState(); });
    }
}

QString Bridge::iconUrl(MinecraftInstance* instance)
{
    if (!m_assets) return {};
    const auto id = instance->id();
    const auto key = instance->iconKey();
    const auto cached = m_icons.value(id);
    if (cached.first == key && !cached.second.isEmpty())
        return cached.second;
    m_assets->removeImage(cached.second);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    APPLICATION->icons()->getIcon(key).pixmap(64, 64).toImage().save(&buffer, "PNG");
    const auto url = m_assets->putImage(png);
    m_icons.insert(id, {key, url});
    return url;
}

QVariantMap Bridge::snapshot()
{
    QVariantList list;
    auto* instances = APPLICATION->instances();
    const auto pins = APPLICATION->settings()->get("AwakePinnedInstances").toStringList();
    QSet<QString> liveIds;
    for (int i = 0; i < instances->count(); ++i) {
        auto* instance = instances->at(i);
        InstanceData data;
        data.id = instance->id();
        liveIds.insert(data.id);
        data.name = instance->name();
        data.group = instances->getInstanceGroup(data.id);
        data.iconUrl = iconUrl(instance);
        data.pinned = pins.contains(data.id);
        data.canLaunch = instance->canLaunch();
        data.running = instance->isRunning();
        data.broken = instance->hasVersionBroken();
        data.lastLaunch = instance->lastLaunch();
        data.totalTimePlayed = instance->totalTimePlayed();
        if (auto* profile = instance->getPackProfile()) {
            data.minecraftVersion = componentVersion(profile->getComponent("net.minecraft"));
            for (const auto& loader : {QPair<QString, QString>{"net.neoforged", "NeoForge"}, {"net.minecraftforge", "Forge"},
                                      {"org.quiltmc.quilt-loader", "Quilt"}, {"net.fabricmc.fabric-loader", "Fabric"}}) {
                auto component = profile->getComponent(loader.first);
                if (component && component->isEnabled()) {
                    data.loader = loader.second;
                    data.loaderVersion = componentVersion(component);
                    break;
                }
            }
        }
        list.append(instanceDto(data));
    }
    for (auto it = m_icons.begin(); it != m_icons.end();) {
        if (!liveIds.contains(it.key())) {
            if (m_assets) m_assets->removeImage(it.value().second);
            it = m_icons.erase(it);
        } else ++it;
    }
    auto selected = APPLICATION->settings()->get("SelectedInstance").toString();
    if (!liveIds.contains(selected)) selected.clear();
    auto account = APPLICATION->accounts()->defaultAccount();
    const auto sortMode = APPLICATION->settings()->get("InstSortMode").toString();
    return {{"instances", list}, {"selectedId", selected},
            {"locale", frontendLocale(APPLICATION->translations()->selectedLanguage())},
            {"reducedMotion", APPLICATION->settings()->get("AwakeReduceMotion").toBool()},
            {"compact", APPLICATION->settings()->get("AwakeCompactLibrary").toBool()},
            {"sortMode", sortMode == "Playtime" ? QString("TotalTimePlayed") : sortMode},
            {"accountName", account ? account->displayName() : QString()}};
}

void Bridge::scheduleState()
{
    if (m_stateScheduled || !m_active) return;
    m_stateScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_stateScheduled = false;
        if (!m_active) return;
        const auto state = snapshot();
        emit stateChanged(state);
        requestArtwork(state.value("selectedId").toString());
    });
}

QVariantMap Bridge::fail(const QString& operation, const QString& detail)
{
    emit operationFailed(operation, detail);
    return {{"ok", false}, {"error", detail}};
}

QVariantMap Bridge::selectInstance(const QString& id)
{
    if (!m_active || m_actionPending) return fail("selectInstance", tr("Finish the current native action first."));
    if (!APPLICATION->instances()->getInstanceById(id)) return fail("selectInstance", tr("This instance no longer exists."));
    if (!m_select(id)) return fail("selectInstance", tr("The instance could not be selected."));
    invalidateArtwork();
    scheduleState();
    return success();
}

QVariantMap Bridge::launchInstance(const QString& id)
{
    if (!m_active || m_actionPending) return fail("launchInstance", tr("Finish the current native action first."));
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance) return fail("launchInstance", tr("This instance no longer exists."));
    if (instance->isRunning() || !instance->canLaunch()) return fail("launchInstance", tr("This instance cannot be launched right now."));
    if (!m_select(id)) return fail("launchInstance", tr("The instance could not be selected."));
    m_actionPending = true;
    QTimer::singleShot(0, this, [this, id] {
        if (!m_active) { m_actionPending = false; return; }
        auto result = m_action("launch", id);
        m_actionPending = false;
        if (!result.value("ok").toBool()) fail("launchInstance", result.value("error").toString());
        scheduleState();
    });
    return success();
}

QVariantMap Bridge::invokeAction(const QString& action, const QString& id)
{
    if (!m_active || m_actionPending) return fail(action, tr("Finish the current native action first."));
    if (!actionAllowed(action)) return fail(action, tr("This action is not available."));
    const bool needsInstance = QStringList{"edit", "folder", "manage", "launchOptions"}.contains(action);
    if (needsInstance && !APPLICATION->instances()->getInstanceById(id)) return fail(action, tr("This instance no longer exists."));
    // Return to QWebChannel before invoking native modal dialogs and menus.
    m_actionPending = true;
    QTimer::singleShot(0, this, [this, action, id] {
        if (!m_active) { m_actionPending = false; return; }
        auto result = m_action(action, id);
        m_actionPending = false;
        if (!result.value("ok").toBool()) fail(action, result.value("error").toString());
        scheduleState();
    });
    return success();
}

QVariantMap Bridge::setPreference(const QString& key, const QVariant& value)
{
    if (!m_active || m_actionPending) return fail("setPreference", tr("Finish the current native action first."));
    if (!preferenceAllowed(key, value)) return fail("setPreference", tr("Invalid frontend preference."));
    auto settings = APPLICATION->settings();
    if (key == "pin") {
        const auto map = value.toMap();
        const auto id = map.value("id").toString();
        if (!APPLICATION->instances()->getInstanceById(id)) return fail("setPreference", tr("This instance no longer exists."));
        auto ids = settings->get("AwakePinnedInstances").toStringList();
        ids.removeAll(id);
        if (map.value("pinned").toBool()) ids.append(id);
        settings->set("AwakePinnedInstances", ids);
    } else {
        const auto nativeKey = key == "reducedMotion" ? "AwakeReduceMotion" : key == "compact" ? "AwakeCompactLibrary" : "InstSortMode";
        settings->set(nativeKey, key == "sortMode" && value.toString() == "TotalTimePlayed" ? QVariant("Playtime") : value);
    }
    scheduleState();
    return success();
}

void Bridge::frontendReady()
{
    emit ready();
    scheduleState();
}

void Bridge::setActive(bool active)
{
    m_active = active;
    if (!active) {
        if (m_canceled) m_canceled->store(true);
        ++m_artworkGeneration;
        m_artworkId.clear();
    } else scheduleState();
}

void Bridge::requestArtwork(const QString& id)
{
    if (!m_assets) return;
    if (!id.isEmpty() && id == m_artworkId) return;
    if (m_canceled) m_canceled->store(true);
    const auto generation = ++m_artworkGeneration;
    m_artworkId = id;
    m_assets->removeImage(m_artworkUrl);
    m_artworkUrl.clear();
    if (id.isEmpty()) return;
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance) return;
    const auto gameRoot = instance->gameRoot();
    auto canceled = std::make_shared<std::atomic_bool>(false);
    m_canceled = canceled;
    auto* watcher = new QFutureWatcher<ArtworkResult>(this);
    connect(watcher, &QFutureWatcher<ArtworkResult>::finished, this, [this, watcher, generation, id] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!m_active || !m_assets || generation != m_artworkGeneration || id != m_artworkId || !APPLICATION->instances()->getInstanceById(id)) return;
        m_artworkUrl = m_assets->putImage(result.png);
        emit artworkChanged(id, m_artworkUrl, result.error);
    });
    watcher->setFuture(QtConcurrent::run([gameRoot, canceled] {
        ArtworkResult result;
        const bool hasScreenshots = !Awake::screenshotFiles(gameRoot).isEmpty();
        const auto artwork = Awake::loadRandomScreenshot(gameRoot, {}, canceled);
        if (canceled->load()) return result;
        if (artwork.image.isNull()) {
            if (hasScreenshots) result.error = QObject::tr("The instance screenshots could not be decoded safely.");
            return result;
        }
        QBuffer buffer(&result.png);
        buffer.open(QIODevice::WriteOnly);
        if (!artwork.image.save(&buffer, "PNG")) result.error = QObject::tr("The screenshot could not be prepared for display.");
        return result;
    }));
}

void Bridge::invalidateArtwork()
{
    if (m_canceled) m_canceled->store(true);
    ++m_artworkGeneration;
    m_artworkId.clear();
}
}  // namespace Awake::Web
