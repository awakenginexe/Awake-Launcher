// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeWebBridge.h"
#include "AwakeWebAssets.h"
#include "AwakeWebPolicy.h"
#include "AwakePackCatalog.h"
#include "AwakeInstanceEditor.h"
#include "Application.h"
#include "HardwareInfo.h"
#include "awake/GpuSelection.h"
#include <QDesktopServices>
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
#include <QFileDialog>
#include <QFileInfo>
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "meta/Version.h"
#include "java/JavaChecker.h"
#include "java/JavaUtils.h"
#include "java/RuntimeSelection.h"
#include <QApplication>

namespace Awake::Web {
namespace {
QVariantMap success() { return {{"ok", true}}; }
struct ArtworkResult { QByteArray png; QString path; QString error; };
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
    m_packCatalog = new PackCatalog(assets, this);
    m_artworkTimer = new QTimer(this);
    m_artworkTimer->setObjectName("awakeArtworkTimer");
    m_artworkTimer->setInterval(60'000);
    connect(m_artworkTimer, &QTimer::timeout, this, [this] {
        if (m_active && m_artworkFocused && !m_artworkId.isEmpty()) loadArtwork(m_artworkId);
    });
    connect(m_packCatalog, &PackCatalog::finished, this, &Bridge::catalogFinished);
    m_instanceEditor = new InstanceEditor(assets, this);
    connect(m_instanceEditor, &InstanceEditor::changed, this, &Bridge::editorChanged);
    connect(m_instanceEditor, &InstanceEditor::failed, this, [this](const QString& detail) { fail("instanceCommand", detail); });
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

    QVariantList accountList;
    auto accounts = APPLICATION->accounts();
    for (int i = 0; i < accounts->count(); ++i) {
        auto acc = accounts->at(i);
        QVariantMap accMap;
        accMap.insert("id", acc->internalId());
        accMap.insert("name", acc->displayName());
        accMap.insert("type", acc->accountType() == AccountType::Offline ? QString("offline") : QString("microsoft"));
        accMap.insert("active", acc == accounts->defaultAccount());
        accMap.insert("valid", acc->accountState() != AccountState::Errored);
        accountList.append(accMap);
    }

    auto s = APPLICATION->settings();
    QVariantMap settingsMap{
        {"language", s->get("Language").toString()},
        {"minMem", s->get("MinMemAlloc").toInt()},
        {"maxMem", s->get("MaxMemAlloc").toInt()},
        {"javaPath", s->get("JavaPath").toString()},
        {"gameWidth", s->get("MinecraftWinWidth").toInt()},
        {"gameHeight", s->get("MinecraftWinHeight").toInt()},
        {"maximizeGame", s->get("MaximizeMinecraft").toBool()},
        {"closeOnLaunch", s->get("CloseAfterLaunch").toBool()}
    };

    return {{"instances", list}, {"selectedId", selected},
            {"locale", frontendLocale(APPLICATION->translations()->selectedLanguage())},
            {"reducedMotion", APPLICATION->settings()->get("AwakeReduceMotion").toBool()},
            {"compact", APPLICATION->settings()->get("AwakeCompactLibrary").toBool()},
            {"sortMode", sortMode == "Playtime" ? QString("TotalTimePlayed") : sortMode},
            {"accountName", account ? account->displayName() : QString()},
            {"totalMemoryMb", QVariant::fromValue(HardwareInfo::installedRamMiB())},
            {"accounts", accountList},
            {"launcherSettings", settingsMap},
            {"modalActive", m_modalActive}};
}

QVariantMap Bridge::gpuSettings()
{
    if (!m_active) return fail("gpuSettings", tr("The launcher is not active."));
    return Awake::Gpu::settings();
}

QVariantMap Bridge::setGpuPreference(const QString& mode)
{
    if (!m_active || !Awake::Gpu::validMode(mode)) return fail("setGpuPreference", tr("Invalid GPU preference."));
    if (!Awake::Gpu::settings().value("supported").toBool()) return fail("setGpuPreference", tr("GPU selection is not supported on this platform."));
    APPLICATION->settings()->set("AwakeGpuPreference", mode);
    APPLICATION->settings()->set("AwakeGpuChoiceSeen", true);
    return Awake::Gpu::settings();
}

QVariantMap Bridge::openGpuSettings()
{
#ifdef Q_OS_WIN
    if (m_active && QDesktopServices::openUrl(QUrl("ms-settings:display-advancedgraphics"))) return success();
#endif
    return fail("openGpuSettings", tr("Unable to open Windows Graphics settings."));
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
    } else if (key == "language") {
        const auto language = nativeLocale(value.toString());
        if (!language.isEmpty() && APPLICATION->translations()->selectLanguage(language))
            settings->set("Language", language);
    } else if (key == "minMem") {
        settings->set("MinMemAlloc", value.toInt());
    } else if (key == "maxMem") {
        settings->set("MaxMemAlloc", value.toInt());
    } else if (key == "gameWidth") {
        settings->set("MinecraftWinWidth", value.toInt());
    } else if (key == "gameHeight") {
        settings->set("MinecraftWinHeight", value.toInt());
    } else if (key == "maximizeGame") {
        settings->set("MaximizeMinecraft", value.toBool());
    } else if (key == "closeOnLaunch") {
        settings->set("CloseAfterLaunch", value.toBool());
    } else if (key == "javaProfile") {
        return setJavaProfile({}, value.toString());
    } else {
        const auto nativeKey = key == "reducedMotion" ? "AwakeReduceMotion" : key == "compact" ? "AwakeCompactLibrary" : "InstSortMode";
        settings->set(nativeKey, key == "sortMode" && value.toString() == "TotalTimePlayed" ? QVariant("Playtime") : value);
    }
    scheduleState();
    return success();
}

QVariantMap Bridge::javaSettings(const QString& id)
{
    if (!m_active) return {{"ok", false}, {"error", tr("The instance editor is paused.")}};
    auto* instance = id.isEmpty() ? nullptr : APPLICATION->instances()->getInstanceById(id);
    if (!id.isEmpty() && !instance) return {{"ok", false}, {"error", tr("This instance no longer exists.")}};
    auto* settings = instance ? instance->settings() : APPLICATION->settings();
    const auto manualOverride = instance && settings->get("OverrideJavaLocation").toBool() && !settings->get("AutomaticJava").toBool();
    const auto overrideProfile = instance && settings->get("OverrideJavaProfile").toBool();
    auto profile = settings->get("AwakeJavaProfile").toString();
    if (manualOverride && !overrideProfile) profile = "custom";
    if (!instance && !settings->get("AutomaticJavaSwitch").toBool()) profile = "custom";
    QVariantList majors;
    if (instance && instance->getPackProfile()->getProfile()) {
        for (const auto major : instance->getPackProfile()->getProfile()->getCompatibleJavaMajors()) majors.append(major);
    }
    auto globalProfile = APPLICATION->settings()->get("AutomaticJavaSwitch").toBool() ? APPLICATION->settings()->get("AwakeJavaProfile").toString() : QString("custom");
    const auto inherited = instance && !overrideProfile && !manualOverride;
    auto* pathSettings = inherited && globalProfile == "custom" ? APPLICATION->settings() : settings;
    return {{"ok", true}, {"profile", Java::runtimeProfileAllowed(profile) ? profile : QString("minecraft")},
        {"inherited", inherited}, {"globalProfile", globalProfile},
        {"path", pathSettings->get("JavaPath").toString()}, {"version", pathSettings->get("JavaVersion").toString()},
        {"vendor", pathSettings->get("JavaVendor").toString()}, {"majors", majors}, {"running", instance && instance->isRunning()}};
}

QVariantMap Bridge::setJavaProfile(const QString& id, const QString& profile)
{
    if (!m_active || m_actionPending || m_javaPending) return {{"ok", false}, {"error", tr("Finish the current native action first.")}};
    auto* instance = id.isEmpty() ? nullptr : APPLICATION->instances()->getInstanceById(id);
    if (!id.isEmpty() && !instance) return {{"ok", false}, {"error", tr("This instance no longer exists.")}};
    if (instance && instance->isRunning()) return {{"ok", false}, {"error", tr("Stop the game before changing its settings.")}};
    if (!Java::runtimeProfileAllowed(profile) && !(instance && profile == "inherit"))
        return {{"ok", false}, {"error", tr("Choose a supported Java runtime.")}};
    auto* settings = instance ? instance->settings() : APPLICATION->settings();
    const auto path = settings->get("JavaPath").toString();
    if (profile == "custom" && !QFileInfo(path).isFile())
        return {{"ok", false}, {"error", tr("Choose a Java executable using Browse before selecting Custom Java.")}};
    if (instance) {
        settings->set("OverrideJavaProfile", profile != "inherit");
        if (profile != "inherit") settings->set("AwakeJavaProfile", profile);
        settings->set("OverrideJavaLocation", profile == "custom");
        settings->set("AutomaticJava", profile != "custom");
        if (profile == "custom") settings->set("JavaPath", path);
        emit editorChanged(id, "java");
    } else {
        settings->set("AwakeJavaProfile", profile);
        settings->set("AutomaticJavaSwitch", profile != "custom");
        settings->set("AutomaticJavaDownload", profile != "custom");
    }
    scheduleState();
    return javaSettings(id);
}

QVariantMap Bridge::browseJava(const QString& requestId, const QString& id)
{
    if (!m_active || m_actionPending || m_javaPending || requestId.isEmpty() || requestId.size() > 128)
        return {{"ok", false}, {"error", tr("Finish the current native action first.")}};
    auto* instance = id.isEmpty() ? nullptr : APPLICATION->instances()->getInstanceById(id);
    if ((!id.isEmpty() && !instance) || (instance && instance->isRunning()))
        return {{"ok", false}, {"error", tr("Stop the game before changing its settings.")}};
    m_javaPending = true;
    QTimer::singleShot(0, this, [this, requestId, id] {
        const auto path = QFileDialog::getOpenFileName(QApplication::activeWindow(), tr("Choose your Java executable"), {},
#if defined(Q_OS_WIN)
            tr("Java executable (javaw.exe java.exe)"));
#else
            tr("Java executable (java)"));
#endif
        if (path.isEmpty()) { m_javaPending = false; emit catalogFinished(requestId, {{"ok", true}, {"canceled", true}}); return; }
        const QFileInfo file(path);
        if (!file.isFile() || !QStringList{"javaw.exe", "java.exe", "java"}.contains(file.fileName().toLower()) || JavaUtils::getJavaCheckPath().isEmpty()) {
            m_javaPending = false;
            emit catalogFinished(requestId, {{"ok", false}, {"error", tr("Choose the java or javaw program inside your Java installation's bin folder.")}});
            return;
        }
        auto checker = makeShared<JavaChecker>(file.canonicalFilePath(), "");
        m_javaTask = checker;
        connect(checker.get(), &JavaChecker::checkFinished, this, [this, requestId, id](const JavaChecker::Result& result) {
            m_javaPending = false;
            auto* target = id.isEmpty() ? nullptr : APPLICATION->instances()->getInstanceById(id);
            if ((!id.isEmpty() && !target) || (target && target->isRunning()) || !m_active) {
                emit catalogFinished(requestId, {{"ok", false}, {"error", tr("The Java selection could not be saved. Stop the game and try again.")}}); return;
            }
            if (result.validity != JavaChecker::Result::Validity::Valid) {
                emit catalogFinished(requestId, {{"ok", false}, {"error", tr("This Java installation did not pass its check. Choose another Java installation.")}}); return;
            }
            if (target) {
                const auto majors = target->getPackProfile()->getProfile()->getCompatibleJavaMajors();
                if (!majors.isEmpty() && !majors.contains(result.javaVersion.major())) {
                    emit catalogFinished(requestId, {{"ok", false}, {"error", tr("This Java version is not compatible with the instance. Choose a supported Java version.")}}); return;
                }
            }
            auto* settings = target ? target->settings() : APPLICATION->settings();
            if (target) { settings->set("OverrideJavaLocation", true); settings->set("AutomaticJava", false); }
            settings->set("JavaPath", result.path);
            settings->set("JavaVersion", result.javaVersion.toString());
            settings->set("JavaVendor", result.javaVendor);
            emit catalogFinished(requestId, setJavaProfile(id, "custom"));
        });
        checker->start();
    });
    return {{"ok", true}, {"pending", true}};
}

QVariantMap Bridge::searchPacks(const QString& requestId, const QString& provider, const QString& query, int offset)
{
    if (!m_active || requestId.isEmpty() || requestId.size() > 64 || query.size() > 256 || offset < 0 || offset > 10000 ||
        !QStringList{"modrinth", "curseforge", "atlauncher", "ftb", "ftb-legacy", "ftb-app", "technic"}.contains(provider))
        return {{"ok", false}, {"error", tr("Invalid provider search.")}};
    m_packCatalog->search(requestId, provider, query, offset);
    return success();
}

QVariantMap Bridge::packVersions(const QString& requestId, const QString& provider, const QString& packId)
{
    if (!m_active || requestId.isEmpty() || requestId.size() > 64 || packId.isEmpty() || packId.size() > 256)
        return {{"ok", false}, {"error", tr("Invalid pack selection.")}};
    m_packCatalog->versions(requestId, provider, packId);
    return success();
}

QVariantMap Bridge::minecraftVersions(const QString& requestId)
{
    if (!m_active || requestId.isEmpty() || requestId.size() > 64)
        return {{"ok", false}, {"error", tr("Invalid version request.")}};
    const auto list = APPLICATION->metadataIndex()->get("net.minecraft");
    const auto reply = [this, requestId, list] {
        QVariantList versions;
        for (const auto& version : list->versions()) {
            versions.append(QVariantMap{{"version", version->version()}, {"released", version->time().date().toString(Qt::ISODate)},
                {"type", version->type()}, {"recommended", version->isRecommended()}});
        }
        emit catalogFinished(requestId, {{"ok", true}, {"minecraftVersions", versions}});
    };
    if (list->isLoaded()) QTimer::singleShot(0, this, reply);
    else {
        const auto task = list->getLoadTask();
        if (!task) return {{"ok", false}, {"error", tr("Minecraft metadata is unavailable.")}};
        m_minecraftTask = task;
        connect(task.get(), &Task::succeeded, this, reply);
        connect(task.get(), &Task::failed, this, [this, requestId](const QString& reason) {
            emit catalogFinished(requestId, {{"ok", false}, {"error", reason}});
        });
        connect(task.get(), &Task::aborted, this, [this, requestId] {
            emit catalogFinished(requestId, {{"ok", false}, {"error", tr("Version request canceled.")}});
        });
        if (!task->isRunning()) task->start();
    }
    return success();
}

QVariantMap Bridge::browseArchive(const QString& requestId)
{
    if (!m_active || m_actionPending || requestId.isEmpty() || requestId.size() > 64)
        return {{"ok", false}, {"error", tr("Finish the current native action first.")}};
    m_actionPending = true;
    QTimer::singleShot(0, this, [this, requestId] {
        setModalActive(true);
        const auto file = QFileDialog::getOpenFileName(nullptr, tr("Import archive"), {}, tr("Modpack archives (*.zip *.mrpack)"));
        setModalActive(false);
        m_actionPending = false;
        emit catalogFinished(requestId, {{"ok", true}, {"archiveUrl", file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString()},
            {"fileName", QFileInfo(file).fileName()}});
    });
    return success();
}

QVariantMap Bridge::instanceDetails(const QString& id, const QString& section)
{
    if (!m_active) return {{"ok", false}, {"error", tr("The instance editor is paused.")}};
    return m_instanceEditor->details(id, section);
}

QVariantMap Bridge::instanceCommand(const QString& id, const QString& command, const QVariant& payload)
{
    if (!m_active || m_actionPending || m_modalActive) return fail("instanceCommand", tr("Finish the current native action first."));
    const auto result = m_instanceEditor->command(id, command, payload);
    scheduleState();
    return result;
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
        m_artworkTimer->stop();
        if (m_canceled) m_canceled->store(true);
        ++m_artworkGeneration;
    } else {
        scheduleState();
        if (m_artworkFocused) {
            const auto id = APPLICATION->settings()->get("SelectedInstance").toString();
            if (id == m_artworkId && m_artworkUrl.isEmpty()) loadArtwork(id);
            else requestArtwork(id);
            m_artworkTimer->start();
        }
    }
}

void Bridge::setArtworkFocused(bool focused)
{
    if (m_artworkFocused == focused) return;
    m_artworkFocused = focused;
    if (!focused) {
        m_artworkTimer->stop();
        if (m_canceled) m_canceled->store(true);
        ++m_artworkGeneration;
        return;
    }
    if (m_active) {
        const auto id = APPLICATION->settings()->get("SelectedInstance").toString();
        if (id == m_artworkId && m_artworkUrl.isEmpty()) loadArtwork(id);
        else requestArtwork(id);
        m_artworkTimer->start();
    }
}

void Bridge::setModalActive(bool active)
{
    if (m_modalActive == active) return;
    m_modalActive = active;
    emit modalChanged(active);
    scheduleState();
}

void Bridge::requestArtwork(const QString& id)
{
    if (!m_assets || !m_active || !m_artworkFocused) return;
    if (!id.isEmpty() && id == m_artworkId) return;
    m_artworkId = id;
    m_artworkPath.clear();
    m_assets->removeImage(m_artworkUrl);
    m_artworkUrl.clear();
    if (id.isEmpty()) return;
    loadArtwork(id);
}

void Bridge::loadArtwork(const QString& id)
{
    if (!m_assets || !m_active || !m_artworkFocused || id.isEmpty() || id != m_artworkId) return;
    if (m_canceled) m_canceled->store(true);
    const auto previousPath = m_artworkPath;
    const auto generation = ++m_artworkGeneration;
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
        if (result.png.isEmpty()) return;
        const auto previousUrl = m_artworkUrl;
        m_artworkPath = result.path;
        m_artworkUrl = m_assets->putImage(result.png);
        if (!previousUrl.isEmpty()) m_assets->removeImage(previousUrl);
        emit artworkChanged(id, m_artworkUrl, result.error);
    });
    watcher->setFuture(QtConcurrent::run([gameRoot, canceled, previousPath] {
        ArtworkResult result;
        const auto artwork = Awake::loadArtwork(gameRoot, previousPath, canceled);
        if (canceled->load()) return result;
        if (artwork.image.isNull()) {
            result.error = QObject::tr("The bundled artwork fallback could not be loaded.");
            return result;
        }
        result.path = artwork.path.startsWith(":/") ? QString() : artwork.path;
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
    m_artworkPath.clear();
}
}  // namespace Awake::Web
