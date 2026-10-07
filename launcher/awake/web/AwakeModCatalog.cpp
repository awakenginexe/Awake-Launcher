// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeModCatalog.h"
#include "AwakeWebAssets.h"
#include "Application.h"
#include "InstanceList.h"
#include "ResourceDownloadTask.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/tasks/GetModDependenciesTask.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "net/ApiRequest.h"
#include "settings/SettingsObject.h"
#include "tasks/ConcurrentTask.h"
#include <QBuffer>
#include <QImageReader>
#include <QPointer>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <algorithm>

namespace Awake::Web {
namespace {
using Dependency = GetModDependenciesTask::PackDependency;
using DependencyPtr = std::shared_ptr<Dependency>;
const ResourceAPI* api(const QString& provider)
{
    if (provider == "modrinth") return &ModrinthAPI::get();
    if (provider == "curseforge" && (APPLICATION->capabilities() & Application::SupportsFlame)) return &FlameAPI::get();
    return nullptr;
}
QString providerName(ModPlatform::ResourceProvider provider)
{
    return provider == ModPlatform::ResourceProvider::FLAME ? "curseforge" : "modrinth";
}
QString plain(const QString& text)
{
    QTextDocument document;
    document.setHtml(text);
    return document.toPlainText().left(16000);
}
QString loaderName(ModPlatform::ModLoaderTypes loaders) { return ModrinthAPI::getModLoaderStrings(loaders).join(", "); }
QString identity(MinecraftInstance* instance)
{
    auto* profile = instance->getPackProfile();
    return profile->getComponentVersion("net.minecraft") + ":" + loaderName(profile->getSupportedModLoaders().value_or(ModPlatform::ModLoaderTypes(0)));
}
QVariantMap modRow(const ModPlatform::IndexedPack::Ptr& pack, QString icon = {})
{
    QStringList authors;
    for (const auto& author : pack->authors) authors.append(author.name);
    auto website = QUrl(pack->websiteUrl);
    return {{"id", pack->addonId.toString()}, {"provider", providerName(pack->provider)}, {"name", pack->name},
            {"author", authors.join(", ")}, {"description", plain(pack->description)}, {"icon", icon},
            {"website", website.scheme() == "https" && !website.host().isEmpty() ? website.toString() : QString()}};
}
QVariantMap versionRow(const ModPlatform::IndexedVersion& version)
{
    return {{"id", version.fileId.toString()}, {"name", version.version}, {"minecraft", version.mcVersion.join(", ")},
            {"loader", loaderName(version.loaders)}, {"type", version.versionType.toString().toLower()},
            {"date", version.date}, {"filename", version.fileName}};
}
QVariantMap selectedRow(const DependencyPtr& item)
{
    return {{"provider", providerName(item->pack->provider)}, {"projectId", item->pack->addonId.toString()},
            {"versionId", item->version.fileId.toString()}, {"name", item->pack->name}};
}
}

bool ModCatalog::safeVersion(const ModPlatform::IndexedVersion& version, const QString& minecraft, ModPlatform::ModLoaderTypes loaders)
{
    const auto& filename = version.fileName;
    const QUrl url(version.downloadUrl);
    return !version.fileId.toString().isEmpty() && version.mcVersion.contains(minecraft) && loaders &&
        (!version.loaders || version.loaders.testAnyFlags(loaders)) && !filename.isEmpty() && filename.size() <= 255 &&
        filename != "." && filename != ".." && !filename.contains('/') && !filename.contains('\\') &&
        !filename.contains(':') && !filename.contains(QChar::Null) && !filename.endsWith('.') && !filename.endsWith(' ') &&
        std::none_of(filename.cbegin(), filename.cend(), [](QChar c) { return c.unicode() < 32 || QString("<>\"|?*").contains(c); }) &&
        (filename.endsWith(".jar", Qt::CaseInsensitive) || filename.endsWith(".zip", Qt::CaseInsensitive)) &&
        url.isValid() && url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty();
}

struct ModCatalog::State {
    struct Request {
        QString id;
        QPointer<MinecraftInstance> instance;
        QString profile;
        QList<Task::Ptr> tasks;
        QPointer<QTimer> timer;
        QMetaObject::Connection runningConnection;
        bool done = false;
        bool installing = false;
        bool canceled = false;
        QVariantList installed, failed;
    };
    using RequestPtr = std::shared_ptr<Request>;
    struct Entry { ModPlatform::IndexedPack::Ptr pack; QHash<QString, ModPlatform::IndexedVersion> versions; QString icon; };
    struct Review {
        QPointer<MinecraftInstance> instance;
        QString profile;
        QList<DependencyPtr> items;
        QSet<QString> dependencies;
        QHash<QString, QString> dependentOn;
    };
    ModCatalog* owner;
    QPointer<Assets> assets;
    QHash<QString, RequestPtr> requests;
    QHash<QString, Entry> entries;
    QStringList entryOrder;
    QHash<QString, Review> reviews;
    QHash<QUuid, Task::Ptr> jobs;
    QString busyRequest, busyInstance;
    explicit State(ModCatalog* owner, Assets* assets) : owner(owner), assets(assets) {}
    ~State()
    {
        for (const auto& request : requests) { QObject::disconnect(request->runningConnection); request->tasks.clear(); }
        for (const auto& job : jobs) { job->disconnect(); job->abort(); }
        if (assets) for (const auto& entry : entries) if (!entry.icon.isEmpty()) assets->removeImage(entry.icon);
    }
    QString key(const QString& instance, const QString& provider, const QString& project) const
    { return instance + '\n' + provider + '\n' + project; }
    void complete(const RequestPtr& request, QVariantMap response)
    {
        if (request->done) return;
        request->done = true;
        QObject::disconnect(request->runningConnection);
        if (request->timer) { request->timer->stop(); request->timer->deleteLater(); }
        requests.remove(request->id);
        if (busyRequest == request->id) { busyRequest.clear(); busyInstance.clear(); }
        emit owner->finished(request->id, response);
    }
    void fail(const RequestPtr& request, QString reason)
    { complete(request, {{"ok", false}, {"error", reason}, {"canceled", request->canceled}}); }
    bool available(const RequestPtr& request)
    {
        if (request->done) return false;
        if (!request->instance || request->instance->isDeleting() || request->instance->isRunning() ||
            identity(request->instance) != request->profile) {
            fail(request, QObject::tr("The instance changed or is running. Stop the game and try again."));
            return false;
        }
        return true;
    }
    RequestPtr begin(const QString& id, const QString& instanceId)
    {
        if (requests.contains(id)) return {};
        auto request = std::make_shared<Request>();
        request->id = id;
        request->instance = dynamic_cast<MinecraftInstance*>(APPLICATION->instances()->getInstanceById(instanceId));
        requests.insert(id, request);
        if (!request->instance || !request->instance->getPackProfile()) { fail(request, QObject::tr("This instance no longer exists.")); return request; }
        request->profile = identity(request->instance);
        if (!available(request)) return request;
        if (!request->instance->getPackProfile()->getSupportedModLoaders().value_or(ModPlatform::ModLoaderTypes(0))) {
            fail(request, QObject::tr("Install a mod loader in the instance before downloading mods.")); return request;
        }
        auto timer = new QTimer(owner);
        timer->setSingleShot(true);
        request->timer = timer;
        QObject::connect(timer, &QTimer::timeout, owner, [this, request] {
            fail(request, QObject::tr("The provider request timed out. Please try again."));
            const auto tasks = request->tasks;
            for (const auto& task : tasks) task->abort();
        });
        timer->start(60000);
        return request;
    }
    void run(const RequestPtr& request, const Task::Ptr& task, bool expectCallback = true)
    {
        if (request->done) return;
        if (!task) { fail(request, QObject::tr("The provider cannot perform this request.")); return; }
        if (auto* net = dynamic_cast<NetJob*>(task.get())) net->setAskRetry(false);
        request->tasks.append(task);
        const auto uid = task->getUid();
        jobs.insert(uid, task);
        QObject::connect(task.get(), &Task::finished, owner, [this, request, uid, expectCallback] {
            QTimer::singleShot(0, owner, [this, request, uid, expectCallback] {
                jobs.remove(uid);
                request->tasks.removeIf([uid](const auto& task) { return task->getUid() == uid; });
                if (expectCallback && !request->done && request->tasks.isEmpty())
                    fail(request, QObject::tr("The provider returned an invalid or incomplete response."));
            });
        });
        task->start();
    }
    template<typename T> ResourceAPI::Callback<T> callbacks(const RequestPtr& request)
    {
        ResourceAPI::Callback<T> result;
        result.onFail = [this, request](const QString& reason, int) { fail(request, reason); };
        result.onAbort = [this, request] { request->canceled = true; fail(request, QObject::tr("Mod request canceled.")); };
        return result;
    }
    void put(const QString& key, Entry entry)
    {
        if (!entries.contains(key)) entryOrder.append(key);
        else if (entry.icon.isEmpty()) entry.icon = entries.value(key).icon;
        entries.insert(key, entry);
        while (entryOrder.size() > 256) {
            const auto removed = entries.take(entryOrder.takeFirst());
            if (assets && !removed.icon.isEmpty()) assets->removeImage(removed.icon);
        }
    }
    QVariantMap compatibility(const RequestPtr& request)
    {
        auto* profile = request->instance->getPackProfile();
        return {{"minecraft", profile->getComponentVersion("net.minecraft")},
                {"loader", loaderName(profile->getSupportedModLoaders().value_or(ModPlatform::ModLoaderTypes(0)))}};
    }
    void list(const RequestPtr& request, const QString& instanceId, const QString& provider,
              const QList<ModPlatform::IndexedPack::Ptr>& packs, QVariantList sorts)
    {
        auto remaining = std::make_shared<int>(1);
        auto done = [this, request, instanceId, provider, packs, sorts, remaining] {
            if (--*remaining || !available(request)) return;
            QVariantList rows;
            for (const auto& pack : packs) rows.append(modRow(pack, entries.value(key(instanceId, provider, pack->addonId.toString())).icon));
            auto response = compatibility(request);
            response.insert("ok", true); response.insert("mods", rows); response.insert("sorts", sorts);
            response.insert("hasMore", packs.size() >= 25);
            complete(request, response);
        };
        for (const auto& pack : packs) {
            const auto cacheKey = key(instanceId, provider, pack->addonId.toString());
            auto cached = entries.value(cacheKey);
            cached.pack = pack;
            put(cacheKey, cached);
            auto& entry = entries[cacheKey];
            const QUrl url(pack->logoUrl);
            if (!entry.icon.isEmpty() || !assets || url.scheme() != "https" || url.host().isEmpty()) continue;
            auto job = makeShared<NetJob>("Mod icon", APPLICATION->network());
            auto [action, response] = Net::ApiRequest::makeByteArray(url);
            job->addNetAction(action);
            ++*remaining;
            QObject::connect(job.get(), &Task::succeeded, owner, [this, response, cacheKey] {
                if (!assets || !entries.contains(cacheKey) || response->size() > 4 * 1024 * 1024) return;
                QBuffer source(response); source.open(QIODevice::ReadOnly);
                QImageReader reader(&source);
                const auto size = reader.size();
                if (!size.isValid() || size.width() > 4096 || size.height() > 4096) return;
                reader.setScaledSize(size.scaled(96, 96, Qt::KeepAspectRatio));
                const auto image = reader.read();
                QByteArray png; QBuffer destination(&png); destination.open(QIODevice::WriteOnly);
                if (!image.isNull() && image.save(&destination, "PNG")) {
                    auto& entry = entries[cacheKey];
                    if (!entry.icon.isEmpty()) assets->removeImage(entry.icon);
                    entry.icon = assets->putImage(png);
                }
            });
            QObject::connect(job.get(), &Task::finished, owner, done);
            run(request, job, false);
        }
        done();
    }
};

ModCatalog::ModCatalog(Assets* assets, QObject* parent) : QObject(parent), d(std::make_unique<State>(this, assets))
{
    connect(APPLICATION->instances(), &InstanceList::removalStarted, this, [this](const QString& id) {
        const auto requests = d->requests.values();
        for (const auto& request : requests) if (request->instance && request->instance->id() == id) cancel(request->id);
    });
}
ModCatalog::~ModCatalog() = default;
bool ModCatalog::busy(const QString& instanceId) const { return !d->busyRequest.isEmpty() && (instanceId.isEmpty() || instanceId == d->busyInstance); }
bool ModCatalog::hasRequest(const QString& requestId) const { return d->requests.contains(requestId); }

void ModCatalog::search(QString requestId, QString instanceId, QString provider, QString query, QString sort, int offset)
{
    auto request = d->begin(requestId, instanceId);
    if (!request || request->done) return;
    const auto* resource = api(provider);
    if (!resource || query.size() > 256 || offset < 0 || offset > 10000) { d->fail(request, tr("Invalid mod search or unavailable provider.")); return; }
    auto* profile = request->instance->getPackProfile();
    ResourceAPI::SearchArgs args;
    args.type = ModPlatform::ResourceType::Mod; args.offset = offset;
    args.search = QString::fromLatin1(QUrl::toPercentEncoding(query.trimmed()));
    args.versions = std::vector<Version>{Version(profile->getComponentVersion("net.minecraft"))};
    args.loaders = profile->getSupportedModLoaders();
    if (provider == "curseforge" ? !FlameAPI::validateModLoaders(*args.loaders) : !ModrinthAPI::validateModLoaders(*args.loaders)) {
        d->fail(request, tr("This provider does not support the instance's mod loader.")); return;
    }
    QVariantList sorts;
    const auto methods = resource->getSortingMethods();
    if (sort.isEmpty() || sort == "relevance") sort = methods.first().name;
    for (const auto& method : methods) {
        sorts.append(QVariantMap{{"id", method.name}, {"name", method.readableName}});
        if (method.name == sort) args.sorting = method;
    }
    if (!args.sorting) { d->fail(request, tr("Unknown provider sort order.")); return; }
    auto callbacks = d->callbacks<QList<ModPlatform::IndexedPack::Ptr>>(request);
    callbacks.onSucceed = [this, request, instanceId, provider, sorts](auto& packs) {
        if (!d->available(request)) return;
        d->list(request, instanceId, provider, packs, sorts);
    };
    d->run(request, resource->searchProjects(args, callbacks));
}

void ModCatalog::versions(QString requestId, QString instanceId, QString provider, QString projectId)
{
    auto request = d->begin(requestId, instanceId);
    if (!request || request->done) return;
    const auto* resource = api(provider);
    const auto cacheKey = d->key(instanceId, provider, projectId);
    const auto entry = d->entries.value(cacheKey);
    if (!resource || !entry.pack) { d->fail(request, tr("Search for this mod before requesting its versions.")); return; }
    auto* profile = request->instance->getPackProfile();
    ResourceAPI::VersionSearchArgs args;
    args.pack = entry.pack; args.resourceType = ModPlatform::ResourceType::Mod;
    args.mcVersions = std::vector<Version>{Version(profile->getComponentVersion("net.minecraft"))};
    args.loaders = profile->getSupportedModLoaders();
    auto callbacks = d->callbacks<QVector<ModPlatform::IndexedVersion>>(request);
    callbacks.onSucceed = [this, request, cacheKey, pack = entry.pack](auto& versions) {
        if (!d->available(request)) return;
        auto* profile = request->instance->getPackProfile();
        State::Entry entry{pack, {}, {}};
        QVariantList rows;
        for (const auto& version : versions) {
            if (!safeVersion(version, profile->getComponentVersion("net.minecraft"), profile->getSupportedModLoaders().value_or(ModPlatform::ModLoaderTypes(0)))) continue;
            entry.versions.insert(version.fileId.toString(), version); rows.append(versionRow(version));
        }
        d->put(cacheKey, entry);
        auto response = d->compatibility(request);
        response.insert("ok", true); response.insert("project", modRow(pack, d->entries.value(cacheKey).icon)); response.insert("versions", rows);
        if (rows.isEmpty()) response.insert("notice", tr("No compatible downloadable versions. The author may restrict third-party launcher downloads."));
        d->complete(request, response);
    };
    d->run(request, resource->getProjectVersions(args, callbacks));
}

void ModCatalog::prepare(QString requestId, QString instanceId, QVariantList selections)
{
    auto request = d->begin(requestId, instanceId);
    if (!request || request->done) return;
    if (busy() || selections.isEmpty() || selections.size() > 100) { d->fail(request, tr("Select mods and finish the current installation first.")); return; }
    d->busyRequest = requestId; d->busyInstance = instanceId;
    QList<DependencyPtr> selected;
    QSet<QString> keys, filenames;
    auto* profile = request->instance->getPackProfile();
    const auto mc = profile->getComponentVersion("net.minecraft");
    const auto loaders = profile->getSupportedModLoaders().value_or(ModPlatform::ModLoaderTypes(0));
    for (const auto& selection : selections) {
        const auto row = selection.toMap(); const auto provider = row.value("provider").toString();
        const auto projectId = row.value("projectId").toString(); const auto versionId = row.value("versionId").toString();
        const auto key = d->key(instanceId, provider, projectId); const auto entry = d->entries.value(key);
        if (!api(provider) || !entry.pack || !entry.versions.contains(versionId) || keys.contains(key) ||
            !safeVersion(entry.versions.value(versionId), mc, loaders) || filenames.contains(entry.versions.value(versionId).fileName.toLower())) {
            d->fail(request, tr("The selection is unavailable, duplicated or incompatible. Search and select its version again.")); return;
        }
        keys.insert(key); filenames.insert(entry.versions.value(versionId).fileName.toLower());
        selected.append(std::make_shared<Dependency>(entry.pack, entry.versions.value(versionId)));
    }
    const bool dependenciesDisabled = APPLICATION->settings()->get("ModDependenciesDisabled").toBool();
    if (!dependenciesDisabled) {
        for (const auto& item : selected) {
            for (const auto& dependency : item->version.dependencies) {
                if (dependency.type != ModPlatform::DependencyType::REQUIRED || dependency.version.isEmpty()) continue;
                for (const auto& candidate : selected) {
                    if (candidate->pack->provider == item->pack->provider && candidate->pack->addonId == dependency.addonId &&
                        candidate->version.fileId.toString() != dependency.version) {
                        d->fail(request, tr("A selected version conflicts with a required dependency. Choose the required version.")); return;
                    }
                }
            }
        }
    }

    auto buildReview = [this, request, selected, mc, loaders](const QList<DependencyPtr>& dependencies,
                                                              const QHash<QString, GetModDependenciesTask::PackDependencyExtraInfo>& extra,
                                                              QStringList warnings) {
        if (!d->available(request)) return;
        State::Review review{request->instance, request->profile, selected, {}, {}};
        QSet<QString> filenames;
        QHash<QString, QString> projects;
        for (const auto& item : selected) filenames.insert(item->version.fileName.toLower());
        for (const auto& item : selected) projects.insert(providerName(item->pack->provider) + ':' + item->pack->addonId.toString(), item->version.fileId.toString());
        for (const auto& dependency : dependencies) {
            if (!dependency->pack || !safeVersion(dependency->version, mc, loaders) || filenames.contains(dependency->version.fileName.toLower())) {
                d->fail(request, tr("A required dependency is unavailable or incompatible. Nothing was installed.")); return;
            }
            const auto project = providerName(dependency->pack->provider) + ':' + dependency->pack->addonId.toString();
            if (projects.contains(project) && projects.value(project) != dependency->version.fileId.toString()) {
                d->fail(request, tr("Required dependencies conflict with a selected version. Nothing was installed.")); return;
            }
            projects.insert(project, dependency->version.fileId.toString());
            filenames.insert(dependency->version.fileName.toLower());
            review.items.append(dependency);
            review.dependencies.insert(providerName(dependency->pack->provider) + ':' + dependency->pack->addonId.toString());
            review.dependentOn.insert(project, extra.value(dependency->pack->addonId.toString()).requiredByIds.value(0));
        }
        QVariantList rows;
        for (const auto& item : review.items) {
            auto row = selectedRow(item); auto info = extra.value(item->pack->addonId.toString());
            row.insert("filename", item->version.fileName); row.insert("version", item->version.version);
            row.insert("type", item->version.versionType.toString().toLower()); row.insert("requiredBy", info.requiredByNames);
            row.insert("dependency", review.dependencies.contains(providerName(item->pack->provider) + ':' + item->pack->addonId.toString()));
            row.insert("maybeInstalled", info.maybeInstalled); rows.append(row);
        }
        const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        d->reviews.clear(); d->reviews.insert(token, review);
        d->complete(request, {{"ok", true}, {"reviewId", token}, {"items", rows}, {"warnings", warnings}});
    };

    if (dependenciesDisabled) {
        buildReview({}, {}, {tr("Required dependencies were not checked because dependency resolution is disabled.")});
        return;
    }
    for (auto* model : request->instance->resourceLists()) if (model && model->empty()) model->startWatching();
    auto task = makeShared<GetModDependenciesTask>(request->instance, request->instance->loaderModList(), selected, false);
    const auto weak = task.toWeakRef();
    connect(task.get(), &Task::succeeded, this, [this, request, weak, buildReview] {
        if (!d->available(request)) return;
        auto task = weak.lock(); if (!task) return;
        if (!task->unresolvedDependencies().isEmpty()) {
            d->fail(request, tr("Required dependencies could not be resolved: %1").arg(task->unresolvedDependencies().join("; "))); return;
        }
        buildReview(task->getDependecies(), task->getExtraInfo(), task->warnings());
    });
    connect(task.get(), &Task::failed, this, [this, request](QString reason) { d->fail(request, reason); });
    connect(task.get(), &Task::aborted, this, [this, request] { request->canceled = true; d->fail(request, tr("Dependency review canceled.")); });
    d->run(request, task);
}

void ModCatalog::install(QString requestId, QString instanceId, QString reviewId)
{
    auto request = d->begin(requestId, instanceId);
    if (!request || request->done) return;
    const auto review = d->reviews.value(reviewId);
    if (busy() || !review.instance || review.instance != request->instance || review.profile != request->profile || review.items.isEmpty()) {
        d->fail(request, tr("Review these mods again before installing.")); return;
    }
    d->reviews.remove(reviewId);
    d->busyRequest = requestId; d->busyInstance = instanceId; request->installing = true;
    request->timer->stop();
    request->runningConnection = connect(request->instance, &BaseInstance::runningStatusChanged, this, [this, request](bool running) {
        if (running && !request->done) cancel(request->id);
    });
    auto task = makeShared<ConcurrentTask>(tr("Download Mods"), std::clamp(APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt(), 1, 16));
    for (const auto& item : review.items) {
        auto row = selectedRow(item);
        auto child = makeShared<ResourceDownloadTask>(item->pack, item->version, request->instance->loaderModList(),
            !APPLICATION->settings()->get("ModMetadataDisabled").toBool(),
            review.dependencies.contains(providerName(item->pack->provider) + ':' + item->pack->addonId.toString()) ? "dependency" : "standalone",
            review.dependentOn.value(providerName(item->pack->provider) + ':' + item->pack->addonId.toString()));
        child->setAskRetry(false);
        connect(child.get(), &Task::succeeded, this, [request, row] { request->installed.append(row); });
        connect(child.get(), &Task::failed, this, [request, row](QString error) mutable { row.insert("error", error); request->failed.append(row); });
        connect(child.get(), &Task::aborted, this, [request, row]() mutable { row.insert("error", tr("Download canceled.")); request->failed.append(row); });
        task->addTask(child);
    }
    connect(task.get(), &Task::progress, this, [this, request](qint64 current, qint64 total) {
        emit progress(request->id, {{"current", current}, {"total", total}, {"status", tr("Downloading mods...")}});
    });
    const auto weak = task.toWeakRef();
    connect(task.get(), &Task::finished, this, [this, request, weak, items = review.items] {
        auto task = weak.lock(); if (!task || request->done) return;
        for (const auto& item : items) {
            const auto row = selectedRow(item);
            const auto matches = [&row](const QVariant& result) {
                const auto map = result.toMap();
                return map.value("provider") == row.value("provider") && map.value("versionId") == row.value("versionId");
            };
            if (std::none_of(request->installed.cbegin(), request->installed.cend(), matches) &&
                std::none_of(request->failed.cbegin(), request->failed.cend(), matches)) {
                auto failed = row;
                failed.insert("error", tr("Download was not completed.")); request->failed.append(failed);
            }
        }
        if (request->instance && !request->instance->isDeleting()) { request->instance->loaderModList()->update(); emit changed(request->instance->id(), "mods"); }
        QVariantMap response{{"ok", task->wasSuccessful()}, {"installed", request->installed}, {"failed", request->failed},
            {"warnings", task->warnings()}, {"canceled", request->canceled}};
        if (!task->wasSuccessful()) response.insert("error", request->canceled ? tr("Download canceled. Completed mods were kept.") : task->failReason());
        d->complete(request, response);
    });
    d->run(request, task, false);
}

bool ModCatalog::cancel(const QString& requestId)
{
    const auto request = d->requests.value(requestId);
    if (!request || request->done) return false;
    request->canceled = true;
    const auto tasks = request->tasks;
    for (const auto& task : tasks) task->abort();
    if (!request->installing) d->fail(request, tr("Mod request canceled."));
    return true;
}
}
