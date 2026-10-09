// SPDX-License-Identifier: GPL-3.0-only
#include "LocalInstanceImportTask.h"
#include <QtConcurrentRun>
#include "Application.h"
#include "icons/IconList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/INISettingsObject.h"
#include "net/ApiRequest.h"
#include "net/NetJob.h"

namespace Awake::LocalImport {
PackLinkTask::PackLinkTask(MinecraftInstance* instance, Pack pack, QUrl archive)
    : m_instance(instance), m_pack(std::move(pack)), m_archive(std::move(archive)),
      m_work(instance->instanceRoot() + "/.awake-pack-link-XXXXXX") {}
PackLinkTask::~PackLinkTask() { m_extract.waitForFinished(); }
bool PackLinkTask::abort() { return m_download && m_download->abort(); }
void PackLinkTask::executeTask()
{
    if (!m_instance || m_instance->isRunning() || !m_work.isValid()) { emitFailed(tr("Stop the game before linking this pack.")); return; }
    m_snapshot = inspect(m_instance->instanceRoot());
    if (!m_snapshot.error.isEmpty()) { emitFailed(m_snapshot.error); return; }
    if (m_pack.provider == "atlauncher") { commitLink(); return; }
    setStatus(tr("Reading the installed pack release metadata..."));
    auto job = makeShared<NetJob>(tr("Installed pack release"), APPLICATION->network());
    job->addNetAction(Net::ApiRequest::makeFile(m_archive, m_work.path() + "/release.zip"));
    connect(job.get(), &Task::failed, this, &PackLinkTask::emitFailed);
    connect(job.get(), &Task::aborted, this, &PackLinkTask::emitAborted);
    connect(job.get(), &Task::progress, this, &PackLinkTask::setProgress);
    connect(job.get(), &Task::succeeded, this, [this] {
        setAbortable(false);
        connect(&m_extract, &QFutureWatcher<QString>::finished, this, [this] {
            const auto error = m_extract.result();
            if (!error.isEmpty()) { emitFailed(error); return; }
            commitLink();
        });
        m_extract.setFuture(QtConcurrent::run([path = m_work.path(), source = m_snapshot, provider = m_pack.provider] {
            QString error;
            writePackBaseline(path + "/release.zip", path + "/baseline", provider, source.minecraft, source.loader, source.loaderVersion, &error);
            return error;
        }));
    });
    setAbortable(true);
    m_download = job;
    job->start();
}
void PackLinkTask::commitLink()
{
    if (!m_instance || m_instance->isRunning()) { emitFailed(tr("The instance is no longer available for linking.")); return; }
    const auto fresh = inspect(m_instance->instanceRoot());
    if (!fresh.error.isEmpty() || fresh.path != m_snapshot.path || fresh.minecraft != m_snapshot.minecraft ||
        fresh.loader != m_snapshot.loader || fresh.loaderVersion != m_snapshot.loaderVersion) {
        emitFailed(tr("The instance changed while linking. Select its installed release again.")); return;
    }
    if (m_pack.provider != "atlauncher") {
        const auto destination = m_snapshot.path + (m_pack.provider == "curseforge" ? "/flame" : "/mrpack");
        const auto backup = m_work.path() + "/previous";
        const bool exists = QFileInfo::exists(destination);
        if (QFileInfo(destination).isSymLink() || QFileInfo(destination).isJunction() || (exists && !QFileInfo(destination).isDir()) ||
            (exists && !QDir().rename(destination, backup))) {
            emitFailed(tr("Cannot prepare the pack metadata folder.")); return;
        }
        if (!QDir().rename(m_work.path() + "/baseline", destination)) {
            if (exists) QDir().rename(backup, destination);
            emitFailed(tr("Cannot save the linked pack metadata.")); return;
        }
    }
    m_instance->setManagedPack(m_pack.provider == "curseforge" ? "flame" : m_pack.provider,
                              m_pack.id, m_pack.name, m_pack.versionId, m_pack.versionName);
    m_instance->settings()->set("AwakePackLinkRequired", false);
    m_instance->settings()->set("AwakeSkippedPackVersion", "");
    emitSucceeded();
}
ImportTask::~ImportTask() = default;
void ImportTask::executeTask()
{
    setStatus(tr("Copying local instance files..."));
    setAbortable(false);
    connect(&m_copy, &QFutureWatcher<QString>::finished, this, [this] {
        const auto error = m_copy.result();
        if (!error.isEmpty()) { emitFailed(error); return; }
        m_instance = std::make_unique<MinecraftInstance>(m_globalSettings,
            std::make_unique<INISettingsObject>(m_stagingPath + "/instance.cfg"), m_stagingPath);
        {
            SettingsObject::Lock lock(m_instance->settings());
            m_instance->settings()->set("InstanceType", "OneSix");
            // Pin the scanned version instead of inheriting Prism's automatic Minecraft upgrades.
            m_instance->settings()->set("UseLatestMinecraftVersion", false);
            if (m_source.source != "Prism / MultiMC") {
                auto* profile = m_instance->getPackProfile();
                profile->buildingFromScratch();
                profile->setComponentVersion("net.minecraft", m_source.minecraft, true);
                if (!m_source.loader.isEmpty()) profile->setComponentVersion(m_source.loader, m_source.loaderVersion, true);
                profile->saveNow();
                if (!QFileInfo::exists(m_stagingPath + "/mmc-pack.json")) { emitFailed(tr("Cannot save the imported instance versions.")); return; }
                m_instance->settings()->set("totalTimePlayed", m_source.playtime);
                if (!m_source.pack.id.isEmpty()) {
                    const auto& pack = m_source.pack;
                    m_instance->setManagedPack(pack.provider == "curseforge" ? "flame" : pack.provider, pack.id, pack.name, pack.versionId, pack.versionName);
                    m_instance->settings()->set("AwakePackLinkRequired", pack.provider != "atlauncher");
                }
                if (m_source.memory >= 512 && m_source.memory <= 1048576) {
                    m_instance->settings()->set("OverrideMemory", true);
                    m_instance->settings()->set("MinMemAlloc", 512);
                    m_instance->settings()->set("MaxMemAlloc", m_source.memory);
                }
            }
            m_instance->setName(name());
            for (const auto& file : {"icon.png", "instance.png"}) {
                if (QFileInfo::exists(m_source.path + '/' + file)) {
                    m_instIcon = "local_" + QFileInfo(m_stagingPath).fileName();
                    APPLICATION->icons()->installIcon(m_source.path + '/' + file, m_instIcon + ".png");
                    m_instance->setIconKey(m_instIcon);
                    break;
                }
            }
            if (m_source.source != "Prism / MultiMC" && m_instIcon.isEmpty()) m_instance->setIconKey("default");
        }
        downloadFiles(m_instance.get());
    });
    m_copy.setFuture(QtConcurrent::run([source = m_source, destination = m_stagingPath] {
        QString error;
        copyInstance(source, destination, &error);
        return error;
    }));
}
}
