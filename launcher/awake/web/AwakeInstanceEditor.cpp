// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeInstanceEditor.h"
#include "AwakeWebAssets.h"
#include "Application.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/Component.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "launch/LaunchTask.h"
#include "launch/LogModel.h"
#include "settings/SettingsObject.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace Awake::Web {
namespace {
QVariantMap failure(const QString& message) { return {{"ok", false}, {"error", message}}; }
const QStringList sections{"overview", "log", "versions", "mods", "resourcepacks", "shaderpacks", "notes", "worlds", "screenshots", "settings", "otherlogs"};
ResourceFolderModel* resources(MinecraftInstance* instance, const QString& section)
{
    if (section == "mods") return instance->loaderModList();
    if (section == "resourcepacks") return instance->resourcePackList();
    if (section == "shaderpacks") return instance->shaderPackList();
    return nullptr;
}
QString directory(MinecraftInstance* instance, const QString& section)
{
    if (auto* model = resources(instance, section)) return model->dir().absolutePath();
    if (section == "worlds") return QDir(instance->gameRoot()).filePath("saves");
    if (section == "screenshots") return QDir(instance->gameRoot()).filePath("screenshots");
    if (section == "otherlogs" || section == "log") return QDir(instance->gameRoot()).filePath("logs");
    if (section == "overview") return instance->gameRoot();
    return {};
}
QString readLog(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (file.size() > 1024 * 1024) file.seek(file.size() - 1024 * 1024);
    return QString::fromUtf8(file.read(1024 * 1024));
}
QString currentLog(MinecraftInstance* instance)
{
    if (auto* task = instance->getLaunchTask()) return task->getLogModel()->toPlainText().right(1024 * 1024);
    const auto file = QDir(instance->gameRoot()).filePath("logs/latest.log");
    return QFileInfo(file).isSymLink() ? QString{} : readLog(file);
}
bool safeFileId(const QString& id)
{
    return !id.isEmpty() && id.size() <= 255 && id != "." && id != ".." &&
        !id.contains('/') && !id.contains('\\') && !id.contains(':') && !id.contains(QChar::Null);
}
}

InstanceEditor::InstanceEditor(Assets* assets, QObject* parent) : QObject(parent), m_assets(assets) {}

void InstanceEditor::observe(QAbstractItemModel* model, const QString& id, const QString& section)
{
    if (!model || m_observed.contains(model)) return;
    m_observed.insert(model);
    const auto notify = [this, id, section] { emit changed(id, section); };
    connect(model, &QAbstractItemModel::modelReset, this, notify);
    connect(model, &QAbstractItemModel::rowsInserted, this, notify);
    connect(model, &QAbstractItemModel::rowsRemoved, this, notify);
    connect(model, &QAbstractItemModel::dataChanged, this, notify);
    connect(model, &QObject::destroyed, this, [this, model] { m_observed.remove(model); });
}

QVariantMap InstanceEditor::details(const QString& instanceId, const QString& section)
{
    auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
    if (!instance) return failure(tr("This instance no longer exists."));
    if (!sections.contains(section)) return failure(tr("This editor page is not available."));
    QVariantList rows;
    QVariantMap result{{"ok", true}, {"name", instance->name()}, {"section", section}, {"running", instance->isRunning()}, {"canLaunch", instance->canLaunch()}};
    if (section == "notes") result.insert("text", instance->notes());
    else if (section == "log") result.insert("text", currentLog(instance));
    else if (section == "settings") {
        auto* settings = instance->settings();
        result.insert("settings", QVariantMap{{"minMemory", settings->get("MinMemAlloc")}, {"maxMemory", settings->get("MaxMemAlloc")},
            {"width", settings->get("MinecraftWinWidth")}, {"height", settings->get("MinecraftWinHeight")},
            {"fullscreen", settings->get("LaunchMaximized")}, {"overrideMemory", settings->get("OverrideMemory")}, {"overrideWindow", settings->get("OverrideWindow")}});
    } else if (section == "versions" || section == "overview") {
        auto* profile = instance->getPackProfile();
        observe(profile, instanceId, "versions");
        for (int i = 0; i < profile->rowCount(); ++i) {
            const auto component = profile->getComponent(static_cast<size_t>(i));
            rows.append(QVariantMap{{"id", component->m_uid}, {"name", component->getName()}, {"detail", component->getVersion()}});
        }
    } else if (auto* model = resources(instance, section)) {
        observe(model, instanceId, section);
        model->startWatching();
        for (int i = 0; i < qMin(model->rowCount(), 2000); ++i) {
            const auto& resource = model->at(i);
            rows.append(QVariantMap{{"id", resource.internalId()}, {"name", resource.name()},
                {"detail", QStringList{resource.version(), resource.getOriginalFileName(), resource.sizeStr()}.filter(QRegularExpression(".+")).join(" · ")},
                {"enabled", resource.enabled()}, {"status", resource.enabled() ? tr("Enabled") : tr("Disabled")}});
        }
    } else {
        const auto path = directory(instance, section);
        const QDir dir(path);
        const auto flags = section == "worlds" ? QDir::Dirs : QDir::Files;
        for (const auto& file : dir.entryInfoList(flags | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Readable, QDir::Name)) {
            if (rows.size() >= 500) break;
            if (section == "screenshots" && !QStringList{"png", "jpg", "jpeg", "webp"}.contains(file.suffix().toLower())) continue;
            if (section == "otherlogs" && !QStringList{"log", "txt", "gz"}.contains(file.suffix().toLower())) continue;
            rows.append(QVariantMap{{"id", file.fileName()}, {"name", file.fileName()}, {"detail", file.lastModified().toString("yyyy-MM-dd HH:mm")}});
        }
    }
    result.insert("rows", rows);
    return result;
}

QVariantMap InstanceEditor::command(const QString& instanceId, const QString& name, const QVariant& payload)
{
    auto* instance = APPLICATION->instances()->getInstanceById(instanceId);
    if (!instance) return failure(tr("This instance no longer exists."));
    const auto options = payload.toMap();
    const auto section = options.value("section").toString();
    if (name == "saveNotes") {
        if (payload.metaType().id() != QMetaType::QString || payload.toString().size() > 1024 * 1024)
            return failure(tr("Notes must be text shorter than one megabyte."));
        instance->setNotes(payload.toString());
    } else if (name == "saveSettings") {
        if (instance->isRunning()) return failure(tr("Stop the game before changing its settings."));
        const QStringList numberKeys{"minMemory", "maxMemory", "width", "height"};
        const QStringList boolKeys{"overrideMemory", "overrideWindow", "fullscreen"};
        if (payload.metaType().id() != QMetaType::QVariantMap || options.size() != 7) return failure(tr("Invalid game settings."));
        for (const auto& key : numberKeys) {
            bool valid = false;
            const auto number = options.value(key).toInt(&valid);
            if (!valid || number < (key.endsWith("Memory") ? 128 : 320) || number > (key.endsWith("Memory") ? 1048576 : 16384))
                return failure(tr("Memory or window size is outside the supported range."));
        }
        for (const auto& key : boolKeys) if (options.value(key).metaType().id() != QMetaType::Bool) return failure(tr("Invalid game setting switches."));
        if (options.value("minMemory").toInt() > options.value("maxMemory").toInt()) return failure(tr("Minimum memory cannot exceed maximum memory."));
        auto* settings = instance->settings();
        settings->set("OverrideMemory", options.value("overrideMemory"));
        settings->set("OverrideWindow", options.value("overrideWindow"));
        if (options.value("overrideMemory").toBool()) {
            settings->set("MinMemAlloc", options.value("minMemory"));
            settings->set("MaxMemAlloc", options.value("maxMemory"));
        }
        if (options.value("overrideWindow").toBool()) {
            settings->set("MinecraftWinWidth", options.value("width"));
            settings->set("MinecraftWinHeight", options.value("height"));
            settings->set("LaunchMaximized", options.value("fullscreen"));
        }
    } else if (name == "clearLog" || name == "copyLog") {
        if (name == "copyLog") QApplication::clipboard()->setText(currentLog(instance));
        else if (auto* task = instance->getLaunchTask()) task->getLogModel()->clear();
        else return failure(tr("There is no active console to clear. Saved log files are kept."));
    } else if (name == "openFolder") {
        if (!sections.contains(section)) return failure(tr("Unknown content folder."));
        const auto path = directory(instance, section);
        if (path.isEmpty()) return failure(tr("This page does not have a content folder."));
        QDir().mkpath(path);
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) return failure(tr("The folder could not be opened."));
    } else if (name == "refreshFiles") {
        auto* model = resources(instance, section);
        if (!model) return failure(tr("This page does not contain editable files."));
        model->startWatching();
        model->update();
    } else if (name == "toggleMod" || name == "removeFile") {
        if (instance->isRunning()) return failure(tr("Stop the game before changing its files."));
        const auto contentSection = name == "toggleMod" ? QString("mods") : section;
        auto* model = resources(instance, contentSection);
        const auto id = options.value("id").toString();
        if (!model || !safeFileId(id)) return failure(tr("Invalid content selection."));
        int index = -1;
        for (int i = 0; i < model->rowCount(); ++i) if (model->at(i).internalId() == id) { index = i; break; }
        if (index < 0) return failure(tr("This file is no longer in the instance."));
        if (name == "toggleMod") {
            if (options.value("enabled").metaType().id() != QMetaType::Bool) return failure(tr("Choose whether the mod should be enabled."));
            if (!model->setResourceEnabled({model->index(index, 0)}, options.value("enabled").toBool() ? EnableAction::ENABLE : EnableAction::DISABLE))
                return failure(tr("The mod could not be changed."));
        } else if (!model->deleteResources({model->index(index, 0)})) return failure(tr("The selected file could not be removed."));
    } else if (name == "addFiles") {
        if (instance->isRunning()) return failure(tr("Stop the game before adding files."));
        if (!resources(instance, section) || m_choosingFiles) return failure(tr("Finish the current file selection first."));
        m_choosingFiles = true;
        QTimer::singleShot(0, this, [this, instanceId, section] {
            const auto files = QFileDialog::getOpenFileNames(QApplication::activeWindow(), tr("Add files to the instance"), {},
                section == "mods" ? tr("Mods (*.jar *.zip)") : tr("Packs (*.zip)"));
            auto* target = APPLICATION->instances()->getInstanceById(instanceId);
            bool failed = false;
            if (target && !target->isRunning()) {
                auto* model = resources(target, section);
                for (const auto& file : files) {
                    const auto info = QFileInfo(file);
                    if (QFileInfo(model->dir().filePath(info.fileName())).exists() ||
                        QFileInfo(model->dir().filePath(info.fileName() + ".disabled")).exists() || !model->installResource(file)) failed = true;
                }
                model->update();
            } else if (!files.isEmpty()) failed = true;
            m_choosingFiles = false;
            emit changed(instanceId, section);
            if (failed) emit this->failed(tr("Some files could not be added. Files already in this instance were kept. Check the file names and try again."));
        });
        return {{"ok", true}, {"pending", true}};
    } else if (name == "openAdvanced") {
        static const QHash<QString, QString> pages{{"overview", "version"}, {"versions", "version"}, {"mods", "mods"},
            {"resourcepacks", "resourcepacks"}, {"shaderpacks", "shaderpacks"}, {"worlds", "worlds"}, {"screenshots", "screenshots"},
            {"settings", "settings"}, {"java", "settings"}, {"notes", "notes"}, {"log", "console"}, {"otherlogs", "logs"}, {"servers", "servers"}};
        if (!pages.contains(section)) return failure(tr("Unknown advanced page."));
        const auto page = pages.value(section);
        QTimer::singleShot(0, this, [instanceId, page] {
            if (auto* target = APPLICATION->instances()->getInstanceById(instanceId)) APPLICATION->showInstanceWindow(target, page);
        });
    } else return failure(tr("This editor action is not available."));
    emit changed(instanceId, section);
    return {{"ok", true}};
}
}
