// SPDX-License-Identifier: GPL-3.0-only
#include "AwakePackCatalog.h"
#include "AwakeWebAssets.h"

#include "Application.h"
#include "BuildConfig.h"
#include "InstanceImportTask.h"
#include "icons/IconList.h"
#include "modplatform/atlauncher/ATLPackIndex.h"
#include "modplatform/atlauncher/ATLPackInstallTask.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/ftb/FTBPackInstallTask.h"
#include "modplatform/import_ftb/PackInstallTask.h"
#include "modplatform/legacy_ftb/PackFetchTask.h"
#include "modplatform/legacy_ftb/PackInstallTask.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/technic/SingleZipPackInstallTask.h"
#include "modplatform/technic/SolderPackInstallTask.h"
#include "modplatform/technic/SolderPackManifest.h"
#include "net/ApiRequest.h"
#include "ui/pages/modplatform/atlauncher/AtlUserInteractionSupportImpl.h"
#include "ui/pages/modplatform/import_ftb/ListModel.h"
#include "ui/pages/modplatform/technic/TechnicData.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include <functional>

namespace Awake::Web {
namespace {
constexpr int pageSize = 25;
QString encoded(const QString& text) { return QString::fromLatin1(QUrl::toPercentEncoding(text)); }
QString loaders(ModPlatform::ModLoaderTypes types) { return ModrinthAPI::getModLoaderStrings(types).join(", "); }
bool networkUrl(const QString& value)
{
    const QUrl url(value);
    return url.isValid() && !url.host().isEmpty() && (url.scheme() == "https" || url.scheme() == "http");
}
QVariantMap versionRow(QString id, QString name, QString minecraft = {}, QString loader = {})
{
    return {{"id", id}, {"name", name}, {"minecraft", minecraft}, {"loader", loader}};
}
}

struct PackCatalog::State {
    struct Entry {
        QVariantMap row;
        QString imageUrl;
        ModPlatform::IndexedPack::Ptr resource;
        ATLauncher::IndexedPack atl{};
        FTB::Modpack ftb{};
        LegacyFTB::Modpack legacy{};
        FTBImportAPP::Modpack local{};
        Technic::Modpack technic;
        QHash<QString, ModPlatform::IndexedVersion> files;
        QHash<QString, QVariantMap> releases;
        QStringList releaseOrder;
        bool versionsLoaded = false;
        void addRelease(const QString& id, const QVariantMap& release)
        {
            if (!releases.contains(id))
                releaseOrder.append(id);
            releases.insert(id, release);
        }
    };
    using EntryPtr = std::shared_ptr<Entry>;
    struct Request {
        QString id;
        bool done = false;
        QPointer<QTimer> timer;
        QList<Task::Ptr> jobs;
    };
    using RequestPtr = std::shared_ptr<Request>;
    PackCatalog* owner;
    QPointer<Assets> assets;
    QHash<QString, QHash<QString, EntryPtr>> packs;
    QHash<QString, QList<EntryPtr>> catalogs;
    QHash<QString, QString> icons;
    QHash<QString, QByteArray> iconBytes;
    QStringList iconOrder;
    QHash<QString, RequestPtr> requests;
    QHash<QUuid, Task::Ptr> jobs;

    State(PackCatalog* owner, Assets* assets) : owner(owner), assets(assets) {}
    ~State()
    {
        for (auto& task : jobs) {
            QObject::disconnect(task.get(), nullptr, owner, nullptr);
            task->abort();
        }
        if (assets) {
            for (const auto& image : icons)
                assets->removeImage(image);
        }
    }
    void complete(const RequestPtr& request, QVariantMap response)
    {
        if (request->done)
            return;
        request->done = true;
        if (request->timer) {
            request->timer->stop();
            request->timer->deleteLater();
        }
        requests.remove(request->id);
        emit owner->finished(request->id, response);
    }
    void fail(const RequestPtr& request, const QString& reason)
    {
        complete(request, {{"ok", false}, {"error", reason}});
    }
    RequestPtr begin(const QString& id)
    {
        if (requests.contains(id))
            fail(requests.value(id), QObject::tr("The request was replaced."));
        auto request = std::make_shared<Request>();
        request->id = id;
        requests.insert(id, request);
        auto timer = new QTimer(owner);
        timer->setSingleShot(true);
        request->timer = timer;
        QObject::connect(timer, &QTimer::timeout, owner, [this, request] {
            fail(request, QObject::tr("The provider request timed out. Please try again."));
            const auto active = request->jobs;
            for (const auto& task : active)
                task->abort();
        });
        timer->start(60000);
        return request;
    }
    void retain(const RequestPtr& request, const Task::Ptr& task)
    {
        jobs.insert(task->getUid(), task);
        request->jobs.append(task);
        const auto uid = task->getUid();
        QObject::connect(task.get(), &Task::finished, owner, [this, request, uid] {
            QTimer::singleShot(0, owner, [this, request, uid] {
                jobs.remove(uid);
                request->jobs.removeIf([uid](const Task::Ptr& item) { return item->getUid() == uid; });
            });
        });
    }
    void bytes(const RequestPtr& request, const QUrl& url, std::function<void(QByteArray)> success,
               std::function<void(QString)> failure = {}, int timeout = 20000)
    {
        if (request->done)
            return;
        auto job = makeShared<NetJob>("Awake provider request", APPLICATION->network());
        job->setAskRetry(false);
        auto [action, response] = Net::ApiRequest::makeByteArray(url);
        job->addNetAction(action);
        auto settled = std::make_shared<bool>(false);
        auto timer = new QTimer(owner);
        timer->setSingleShot(true);
        auto error = [this, request, failure, settled, timer](QString reason) {
            if (*settled)
                return;
            *settled = true;
            timer->deleteLater();
            if (request->done)
                return;
            if (failure)
                failure(reason);
            else
                fail(request, reason);
        };
        QObject::connect(job.get(), &Task::succeeded, owner, [request, settled, timer, response, success] {
            if (*settled)
                return;
            *settled = true;
            timer->deleteLater();
            if (!request->done)
                success(*response);
        });
        QObject::connect(job.get(), &Task::failed, owner, error);
        QObject::connect(job.get(), &Task::aborted, owner, [error] { error(QObject::tr("Provider request cancelled.")); });
        auto weak = job.toWeakRef();
        QObject::connect(timer, &QTimer::timeout, owner, [error, weak] {
            error(QObject::tr("The provider did not respond in time."));
            if (auto activeJob = weak.lock())
                activeJob->abort();
        });
        retain(request, job);
        timer->start(timeout);
        job->start();
    }
    void json(const RequestPtr& request, const QUrl& url, std::function<void(QJsonDocument)> success,
              std::function<void(QString)> failure = {})
    {
        bytes(request, url, [this, request, success](const QByteArray& bytes) {
            QJsonParseError error;
            auto doc = QJsonDocument::fromJson(bytes, &error);
            if (error.error != QJsonParseError::NoError || doc.isNull()) {
                fail(request, QObject::tr("The provider returned invalid JSON: %1").arg(error.errorString()));
                return;
            }
            success(doc);
        }, failure);
    }
    EntryPtr entry(const QString& provider, const QString& id, const QString& name, const QString& author = {},
                   const QString& description = {}, const QVariant& downloads = {}, const QString& minecraft = {},
                   const QString& loader = {})
    {
        auto existing = packs[provider].value(id);
        auto result = existing ? existing : std::make_shared<Entry>();
        result->row = {{"id", id}, {"name", name}, {"author", author}, {"description", description},
                       {"downloads", downloads}, {"minecraft", minecraft}, {"loader", loader}, {"icon", ""}};
        packs[provider].insert(id, result);
        return result;
    }
    QString image(const QByteArray& data, const QString& key)
    {
        if (!assets || data.size() > 4 * 1024 * 1024)
            return {};
        if (icons.contains(key))
            return icons.value(key);
        QBuffer source;
        source.setData(data);
        source.open(QIODevice::ReadOnly);
        QImageReader reader(&source);
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 4096 || size.height() > 4096)
            return {};
        reader.setScaledSize(size.scaled(128, 128, Qt::KeepAspectRatio));
        const auto decoded = reader.read();
        if (decoded.isNull())
            return {};
        QByteArray png;
        QBuffer destination(&png);
        destination.open(QIODevice::WriteOnly);
        if (!decoded.save(&destination, "PNG"))
            return {};
        const auto url = assets->putImage(png);
        icons.insert(key, url);
        iconBytes.insert(key, png);
        iconOrder.append(key);
        while (iconOrder.size() > 256) {
            const auto oldest = iconOrder.takeFirst();
            assets->removeImage(icons.take(oldest));
            iconBytes.remove(oldest);
        }
        return url;
    }
    void list(const RequestPtr& request, QList<EntryPtr> entries, bool hasMore)
    {
        auto remaining = std::make_shared<int>(1);
        auto done = [this, request, entries, hasMore, remaining] {
            if (--*remaining != 0 || request->done)
                return;
            QVariantList rows;
            for (const auto& entry : entries)
                rows.append(entry->row);
            complete(request, {{"ok", true}, {"packs", rows}, {"hasMore", hasMore}});
        };
        for (const auto& entry : entries) {
            if (icons.contains(entry->imageUrl)) {
                entry->row["icon"] = icons.value(entry->imageUrl);
                continue;
            }
            if (!networkUrl(entry->imageUrl))
                continue;
            entry->row["icon"] = "";
            ++*remaining;
            bytes(request, QUrl(entry->imageUrl), [this, entry, done](const QByteArray& data) {
                entry->row["icon"] = image(data, entry->imageUrl);
                done();
            }, [done](QString) { done(); }, 5000);
        }
        done();
    }
    void filtered(const RequestPtr& request, const QString& provider, const QString& query, int offset)
    {
        QList<EntryPtr> result;
        for (const auto& entry : catalogs.value(provider)) {
            if (query.isEmpty() || entry->row.value("name").toString().contains(query, Qt::CaseInsensitive) ||
                entry->row.value("description").toString().contains(query, Qt::CaseInsensitive) ||
                entry->row.value("author").toString().contains(query, Qt::CaseInsensitive))
                result.append(entry);
        }
        list(request, result.mid(offset, pageSize), offset + pageSize < result.size());
    }
    void resourceSearch(const RequestPtr& request, const QString& provider, const QString& query, int offset)
    {
        const ResourceAPI& api = provider == "modrinth" ? static_cast<const ResourceAPI&>(ModrinthAPI::get()) : FlameAPI::get();
        ResourceAPI::SearchArgs args;
        args.type = ModPlatform::ResourceType::Modpack;
        args.offset = offset;
        args.search = encoded(query);
        const auto url = api.getSearchURL(args);
        if (!url) {
            fail(request, QObject::tr("This provider cannot perform the requested search."));
            return;
        }
        json(request, QUrl(*url), [this, request, provider, offset, &api](QJsonDocument doc) {
            const auto root = doc.object();
            const auto key = provider == "modrinth" ? "hits" : "data";
            if (!root.value(key).isArray()) {
                fail(request, QObject::tr("The provider returned an unexpected search response."));
                return;
            }
            const auto rows = api.documentToArray(doc);
            QList<EntryPtr> result;
            for (const auto& value : rows) {
                auto obj = value.toObject();
                auto pack = std::make_shared<ModPlatform::IndexedPack>();
                auto parsed = api.loadIndexedPack(*pack, obj);
                if (!parsed || pack->addonId.toString().isEmpty()) {
                    fail(request, QObject::tr("The provider returned invalid pack metadata."));
                    return;
                }
                QStringList authors;
                for (const auto& author : pack->authors)
                    authors.append(author.name);
                auto item = entry(provider, pack->addonId.toString(), pack->name, authors.join(", "), pack->description,
                                  obj.value(provider == "modrinth" ? "downloads" : "downloadCount").toVariant());
                if (provider == "modrinth") {
                    const auto gameVersions = obj.value("versions").toArray();
                    if (!gameVersions.isEmpty())
                        item->row["minecraft"] = gameVersions.last().toString();
                    QStringList knownLoaders;
                    for (const auto& category : obj.value("categories").toArray()) {
                        const auto name = category.toString();
                        if (name == "fabric" || name == "forge" || name == "neoforge" || name == "quilt")
                            knownLoaders.append(name);
                    }
                    item->row["loader"] = knownLoaders.join(", ");
                } else {
                    const auto files = obj.value("latestFiles").toArray();
                    if (!files.isEmpty()) {
                        auto fileObject = files.first().toObject();
                        if (auto file = api.loadIndexedPackVersion(fileObject, ModPlatform::ResourceType::Modpack); file) {
                            item->row["minecraft"] = file->mcVersion.join(", ");
                            item->row["loader"] = loaders(file->loaders);
                        }
                    }
                }
                if (!item->resource)
                    item->resource = pack;
                item->imageUrl = pack->logoUrl;
                result.append(item);
            }
            const int total = provider == "modrinth" ? root.value("total_hits").toInt() :
                root.value("pagination").toObject().value("totalCount").toInt();
            list(request, result, offset + rows.size() < total);
        });
    }
    void atlSearch(const RequestPtr& request, const QString& query, int offset)
    {
        if (catalogs.contains("atlauncher")) {
            filtered(request, "atlauncher", query, offset);
            return;
        }
        json(request, QUrl(BuildConfig.ATL_DOWNLOAD_SERVER_URL + "launcher/json/packsnew.json"),
             [this, request, query, offset](QJsonDocument doc) {
            if (!doc.isArray()) {
                fail(request, QObject::tr("ATLauncher returned an unexpected catalog."));
                return;
            }
            QList<EntryPtr> result;
            for (const auto& value : doc.array()) {
                auto obj = value.toObject();
                ATLauncher::IndexedPack pack{};
                const auto parsed = ATLauncher::loadIndexedPack(pack, obj);
                if (!parsed) {
                    fail(request, QObject::tr("ATLauncher returned invalid pack metadata: %1").arg(parsed.error()));
                    return;
                }
                if (pack.system || pack.type != ATLauncher::PackType::Public || pack.versions.isEmpty())
                    continue;
                auto item = entry("atlauncher", QString::number(pack.id), pack.name, {}, pack.description, {},
                                  pack.versions.first().minecraft);
                item->atl = pack;
                item->imageUrl = BuildConfig.ATL_DOWNLOAD_SERVER_URL + "launcher/images/" + pack.safeName;
                for (const auto& version : pack.versions)
                    item->addRelease(version.version, versionRow(version.version, version.version, version.minecraft));
                item->versionsLoaded = true;
                result.append(item);
            }
            catalogs.insert("atlauncher", result);
            filtered(request, "atlauncher", query, offset);
        });
    }
    void legacySearch(const RequestPtr& request, const QString& query, int offset)
    {
        if (catalogs.contains("ftb-legacy")) {
            filtered(request, "ftb-legacy", query, offset);
            return;
        }
        auto result = std::make_shared<QList<EntryPtr>>();
        auto remaining = std::make_shared<int>(2);
        for (const auto type : {LegacyFTB::PackType::Public, LegacyFTB::PackType::ThirdParty}) {
            const auto file = type == LegacyFTB::PackType::Public ? "modpacks.xml" : "thirdparty.xml";
            bytes(request, QUrl(BuildConfig.LEGACY_FTB_CDN_BASE_URL + "static/" + file),
                  [this, request, query, offset, type, remaining, result](QByteArray data) {
                LegacyFTB::ModpackList parsed;
                if (!LegacyFTB::PackFetchTask::parseAndAddPacks(data, type, parsed)) {
                    fail(request, QObject::tr("Legacy FTB returned an invalid catalog."));
                    return;
                }
                for (const auto& pack : parsed) {
                    if (pack.broken || pack.dir.isEmpty())
                        continue;
                    const auto id = (type == LegacyFTB::PackType::Public ? "public:" : "thirdparty:") + pack.dir;
                    auto item = entry("ftb-legacy", id, pack.name, pack.author, pack.description, {}, pack.mcVersion);
                    item->legacy = pack;
                    item->imageUrl = BuildConfig.LEGACY_FTB_CDN_BASE_URL + "static/" + pack.logo;
                    auto versions = pack.oldVersions;
                    versions.prepend(pack.currentVersion);
                    versions.removeDuplicates();
                    for (const auto& version : versions) {
                        if (!version.isEmpty())
                            item->addRelease(version, versionRow(version, version, version == pack.currentVersion ? pack.mcVersion : QString()));
                    }
                    item->versionsLoaded = true;
                    result->append(item);
                }
                if (--*remaining == 0) {
                    catalogs.insert("ftb-legacy", *result);
                    filtered(request, "ftb-legacy", query, offset);
                }
            });
        }
    }
    void localSearch(const RequestPtr& request, const QString& query, int offset)
    {
        FTBImportAPP::ListModel model(owner);
        model.update();
        QList<EntryPtr> result;
        for (int row = 0; row < model.rowCount({}); ++row) {
            const auto pack = model.data(model.index(row, 0), Qt::UserRole).value<FTBImportAPP::Modpack>();
            const auto id = QString::fromLatin1(QCryptographicHash::hash(pack.path.toUtf8(), QCryptographicHash::Sha256).toHex());
            auto item = entry("ftb-app", id, pack.name, {}, {}, {}, pack.mcVersion,
                              pack.loaderType ? loaders(*pack.loaderType) : QString());
            item->local = pack;
            const auto version = QString::number(pack.versionId);
            item->releases = {{version, versionRow(version, pack.version, pack.mcVersion, item->row.value("loader").toString())}};
            item->releaseOrder = {version};
            item->versionsLoaded = true;
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            if (pack.icon.pixmap(128, 128).save(&buffer, "PNG")) {
                item->row["icon"] = image(png, "local:" + id);
            }
            result.append(item);
        }
        catalogs.insert("ftb-app", result);
        filtered(request, "ftb-app", query, offset);
    }
    void ftbSearch(const RequestPtr& request, const QString& query, int offset)
    {
        if (catalogs.contains("ftb")) {
            filtered(request, "ftb", query, offset);
            return;
        }
        auto fetchAll = [this, request, query, offset](QString) {
            json(request, QUrl(BuildConfig.FTB_API_BASE_URL + "/modpack/all"),
                 [this, request, query, offset](QJsonDocument doc) { ftbDetails(request, doc, query, offset, true); });
        };
        const auto path = query.isEmpty() ? "/modpack/popular" : "/modpack/search/" + encoded(query);
        json(request, QUrl(BuildConfig.FTB_API_BASE_URL + path),
             [this, request, query, offset](QJsonDocument doc) { ftbDetails(request, doc, query, offset, false); }, fetchAll);
    }
    void ftbDetails(const RequestPtr& request, const QJsonDocument& catalogDocument, const QString& query, int offset, bool full)
    {
        auto rows = catalogDocument.object().value("packs");
        if (!rows.isArray()) {
            fail(request, QObject::tr("FTB returned an unexpected catalog response."));
            return;
        }
        auto ids = rows.toArray();
        if (ids.isEmpty()) {
            if (full)
                catalogs.insert("ftb", {});
            list(request, {}, false);
            return;
        }
        auto job = makeShared<NetJob>("Awake FTB catalog", APPLICATION->network(), 6);
        job->setAskRetry(false);
        QList<QByteArray*> responses;
        for (const auto& value : ids) {
            const int id = value.isObject() ? value.toObject().value("id").toInt() : value.toInt();
            if (id <= 0) {
                fail(request, QObject::tr("FTB returned an invalid pack ID."));
                return;
            }
            auto [action, response] = Net::Request::makeByteArray(QUrl(BuildConfig.FTB_API_BASE_URL + "/modpack/" + QString::number(id)));
            responses.append(response);
            job->addNetAction(action);
        }
        QObject::connect(job.get(), &Task::succeeded, owner, [this, request, responses, query, offset, full] {
            if (request->done)
                return;
            QList<EntryPtr> result;
            for (auto response : responses) {
                QJsonParseError error;
                const auto packDocument = QJsonDocument::fromJson(*response, &error);
                FTB::Modpack pack{};
                const auto parsed = FTB::loadModpack(pack, packDocument.object());
                if (error.error != QJsonParseError::NoError || !parsed) {
                    fail(request, QObject::tr("FTB returned invalid pack metadata."));
                    return;
                }
                pack.name = packDocument.object().value("name").toString();
                if (pack.versions.isEmpty())
                    continue;
                QStringList authors;
                for (const auto& author : pack.authors)
                    authors.append(author.name);
                auto item = entry("ftb", QString::number(pack.id), pack.name, authors.join(", "), pack.synopsis, pack.installs);
                item->ftb = pack;
                QHash<int, QPair<QString, QString>> targets;
                for (const auto& versionValue : packDocument.object().value("versions").toArray()) {
                    const auto versionObject = versionValue.toObject();
                    QString minecraft;
                    QStringList modloaders;
                    for (const auto& targetValue : versionObject.value("targets").toArray()) {
                        const auto target = targetValue.toObject();
                        if (target.value("name").toString() == "minecraft")
                            minecraft = target.value("version").toString();
                        else if (target.value("type").toString() == "modloader")
                            modloaders.append(target.value("name").toString());
                    }
                    targets.insert(versionObject.value("id").toInt(), {minecraft, modloaders.join(", ")});
                }
                const auto latestTarget = targets.value(pack.versions.last().id);
                item->row["minecraft"] = latestTarget.first;
                item->row["loader"] = latestTarget.second;
                for (const auto& art : pack.art) {
                    if (art.type == "square") {
                        item->imageUrl = art.url;
                        break;
                    }
                }
                for (auto index = pack.versions.size(); index-- > 0;) {
                    const auto& version = pack.versions.at(index);
                    const auto target = targets.value(version.id);
                    item->addRelease(QString::number(version.id), versionRow(QString::number(version.id), version.name, target.first, target.second));
                }
                item->versionsLoaded = true;
                result.append(item);
            }
            if (full) {
                std::sort(result.begin(), result.end(), [](const EntryPtr& a, const EntryPtr& b) {
                    return a->ftb.installs > b->ftb.installs;
                });
                catalogs.insert("ftb", result);
                filtered(request, "ftb", query, offset);
            } else {
                list(request, result.mid(offset, pageSize), offset + pageSize < result.size());
            }
        });
        QObject::connect(job.get(), &Task::failed, owner, [this, request](const QString& reason) { fail(request, reason); });
        QObject::connect(job.get(), &Task::aborted, owner, [this, request] { fail(request, QObject::tr("FTB request cancelled.")); });
        retain(request, job);
        job->start();
    }
    void technicSearch(const RequestPtr& request, const QString& query, int offset)
    {
        const bool direct = query.startsWith('#');
        QUrl url(BuildConfig.TECHNIC_API_BASE_URL + (direct ? "modpack/" + encoded(query.mid(1)) : query.isEmpty() ? "trending" : "search"));
        QUrlQuery parameters;
        parameters.addQueryItem("build", BuildConfig.TECHNIC_API_BUILD);
        if (!query.isEmpty() && !direct)
            parameters.addQueryItem("q", query);
        const auto client = APPLICATION->settings()->get("TechnicClientID").toString();
        if (!client.isEmpty())
            parameters.addQueryItem("cid", client);
        url.setQuery(parameters);
        json(request, url, [this, request, direct, offset](QJsonDocument doc) {
            const auto root = doc.object();
            if (root.contains("error")) {
                fail(request, QObject::tr("Technic reported: %1").arg(root.value("error").toString()));
                return;
            }
            QJsonArray rows;
            if (direct)
                rows.append(root);
            else if (root.value("modpacks").isArray())
                rows = root.value("modpacks").toArray();
            else {
                fail(request, QObject::tr("Technic returned an unexpected catalog response."));
                return;
            }
            QList<EntryPtr> result;
            for (const auto& value : rows) {
                const auto obj = value.toObject();
                const auto id = obj.value(direct ? "name" : "slug").toString();
                const auto name = obj.value(direct ? "displayName" : "name").toString();
                if (id.isEmpty() || name.isEmpty()) {
                    fail(request, QObject::tr("Technic returned invalid pack metadata."));
                    return;
                }
                if (id == "vanilla")
                    continue;
                auto item = entry("technic", id, name, obj.value("user").toString(), obj.value("description").toString(),
                                  obj.value("downloads").toVariant(), obj.value("minecraft").toString());
                item->technic.slug = id;
                item->technic.name = name;
                item->imageUrl = direct ? obj.value("icon").toObject().value("url").toString() : obj.value("iconUrl").toString();
                result.append(item);
            }
            list(request, result.mid(offset, pageSize), offset + pageSize < result.size());
        });
    }
    void releaseList(const RequestPtr& request, const EntryPtr& item)
    {
        QVariantList result;
        for (const auto& id : item->releaseOrder)
            result.append(item->releases.value(id));
        complete(request, {{"ok", true}, {"versions", result}});
    }
    void resourceVersions(const RequestPtr& request, const QString& provider, const EntryPtr& item)
    {
        const ResourceAPI& api = provider == "modrinth" ? static_cast<const ResourceAPI&>(ModrinthAPI::get()) : FlameAPI::get();
        ResourceAPI::VersionSearchArgs args{};
        args.pack = item->resource;
        args.resourceType = ModPlatform::ResourceType::Modpack;
        auto url = api.getVersionsURL(args);
        if (!url) {
            fail(request, QObject::tr("This provider cannot list versions for the selected pack."));
            return;
        }
        json(request, QUrl(*url), [this, request, item, &api](QJsonDocument doc) {
            if (!doc.isArray() && !doc.object().value("data").isArray()) {
                fail(request, QObject::tr("The provider returned an unexpected version response."));
                return;
            }
            const auto rows = doc.isArray() ? doc.array() : doc.object().value("data").toArray();
            QList<ModPlatform::IndexedVersion> files;
            for (const auto& value : rows) {
                auto obj = value.toObject();
                auto parsed = api.loadIndexedPackVersion(obj, ModPlatform::ResourceType::Modpack);
                if (!parsed) {
                    fail(request, QObject::tr("The provider returned invalid version metadata: %1").arg(parsed.error()));
                    return;
                }
                auto file = parsed.value();
                if (file.fileId.toString().isEmpty() || !networkUrl(file.downloadUrl))
                    continue;
                files.append(file);
            }
            std::stable_sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.date > b.date; });
            item->files.clear();
            item->releases.clear();
            item->releaseOrder.clear();
            for (const auto& file : files) {
                item->files.insert(file.fileId.toString(), file);
                auto row = versionRow(file.fileId.toString(), file.version, file.mcVersion.join(", "), loaders(file.loaders));
                row.insert("versionNumber", file.versionNumber);
                item->addRelease(file.fileId.toString(), row);
            }
            item->versionsLoaded = true;
            releaseList(request, item);
        });
    }
    void technicVersions(const RequestPtr& request, const EntryPtr& item)
    {
        const auto url = BuildConfig.TECHNIC_API_BASE_URL + "modpack/" + encoded(item->technic.slug) + "?build=" + encoded(BuildConfig.TECHNIC_API_BUILD);
        json(request, QUrl(url), [this, request, item](QJsonDocument doc) {
            const auto obj = doc.object();
            auto& pack = item->technic;
            pack.url = obj.value("url").toString();
            pack.isSolder = pack.url.isEmpty();
            if (pack.isSolder)
                pack.url = obj.value("solder").toString();
            pack.minecraftVersion = obj.value("minecraft").toString();
            pack.currentVersion = obj.value("version").toString();
            if (!networkUrl(pack.url) || pack.currentVersion.isEmpty()) {
                fail(request, QObject::tr("Technic did not provide an installable pack version."));
                return;
            }
            if (!pack.isSolder) {
                item->addRelease(pack.currentVersion, versionRow(pack.currentVersion, pack.currentVersion, pack.minecraftVersion));
                item->versionsLoaded = true;
                releaseList(request, item);
                return;
            }
            while (pack.url.endsWith('/'))
                pack.url.chop(1);
            json(request, QUrl(pack.url + "/modpack/" + encoded(pack.slug)), [this, request, item](QJsonDocument solderDocument) {
                TechnicSolder::Pack solder;
                const auto parsed = TechnicSolder::loadPack(solder, solderDocument.object());
                if (!parsed) {
                    fail(request, QObject::tr("The Solder server returned invalid versions: %1").arg(parsed.error()));
                    return;
                }
                if (solder.builds.contains(solder.recommended))
                    item->addRelease(solder.recommended, versionRow(solder.recommended, solder.recommended));
                for (auto index = solder.builds.size(); index-- > 0;) {
                    const auto& version = solder.builds.at(index);
                    if (!version.isEmpty())
                        item->addRelease(version, versionRow(version, version));
                }
                item->versionsLoaded = true;
                releaseList(request, item);
            });
        });
    }
};

PackCatalog::PackCatalog(Assets* assets, QObject* parent) : QObject(parent), d(std::make_unique<State>(this, assets)) {}
PackCatalog::~PackCatalog() = default;

void PackCatalog::search(QString requestId, QString provider, QString query, int offset)
{
    auto request = d->begin(requestId);
    query = query.trimmed();
    if (offset < 0 || offset > 10000 || query.size() > 512) {
        d->fail(request, tr("Invalid provider search parameters."));
        return;
    }
    QTimer::singleShot(0, this, [this, request, provider, query, offset] {
        if (request->done)
            return;
        if (provider == "modrinth" || provider == "curseforge")
            d->resourceSearch(request, provider, query, offset);
        else if (provider == "atlauncher")
            d->atlSearch(request, query, offset);
        else if (provider == "ftb")
            d->ftbSearch(request, query, offset);
        else if (provider == "ftb-legacy")
            d->legacySearch(request, query, offset);
        else if (provider == "ftb-app")
            d->localSearch(request, query, offset);
        else if (provider == "technic")
            d->technicSearch(request, query, offset);
        else
            d->fail(request, tr("Unknown modpack provider."));
    });
}

void PackCatalog::versions(QString requestId, QString provider, QString packId)
{
    auto request = d->begin(requestId);
    QTimer::singleShot(0, this, [this, request, provider, packId] {
        if (request->done)
            return;
        const auto item = d->packs.value(provider).value(packId);
        if (!item) {
            d->fail(request, tr("Search for this pack before requesting its versions."));
            return;
        }
        if (item->versionsLoaded)
            d->releaseList(request, item);
        else if (provider == "modrinth" || provider == "curseforge")
            d->resourceVersions(request, provider, item);
        else if (provider == "technic")
            d->technicVersions(request, item);
        else
            d->fail(request, tr("No version metadata is available for this pack."));
    });
}

InstanceTask* PackCatalog::createTask(QString provider, QString packId, QString versionId, QWidget* parent, QString* error)
{
    auto reject = [&](const QString& reason) -> InstanceTask* {
        if (error)
            *error = reason;
        return nullptr;
    };
    const auto item = d->packs.value(provider).value(packId);
    if (!item || !item->versionsLoaded || !item->releases.contains(versionId))
        return reject(tr("Select an available pack and version before installing."));
    auto decorate = [&](InstanceTask* task) {
        QString icon = "default";
        const auto png = d->iconBytes.value(provider == "ftb-app" ? "local:" + packId : item->imageUrl);
        if (!png.isEmpty()) {
            QTemporaryFile file(QDir::tempPath() + "/awake-pack-XXXXXX.png");
            if (file.open() && file.write(png) == png.size() && file.flush()) {
                file.close();
                icon = "awake_pack_" + provider + "_" + QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex());
                APPLICATION->icons()->installIcon(file.fileName(), icon + ".png");
            }
        }
        task->setIcon(icon);
        return task;
    };
    if (provider == "modrinth" || provider == "curseforge") {
        const auto version = item->files.value(versionId);
        if (!networkUrl(version.downloadUrl))
            return reject(tr("This version does not have an available download."));
        auto* task = new InstanceImportTask(QUrl(version.downloadUrl), true, parent, {{"pack_id", packId}, {"pack_version_id", versionId}});
        task->setOriginalName(item->row.value("name").toString(), version.versionNumber.isEmpty() ? version.version : version.versionNumber);
        return decorate(task);
    }
    if (provider == "atlauncher")
        return decorate(new ATLauncher::PackInstallTask(new AtlUserInteractionSupportImpl(parent), item->atl.name, versionId));
    if (provider == "ftb")
        return decorate(new FTB::PackInstallTask(item->ftb, item->releases.value(versionId).value("name").toString(), parent));
    if (provider == "ftb-legacy")
        return decorate(new LegacyFTB::PackInstallTask(APPLICATION->network(), item->legacy, versionId));
    if (provider == "ftb-app")
        return decorate(new FTBImportAPP::PackInstallTask(item->local));
    if (provider == "technic") {
        if (item->technic.isSolder)
            return decorate(new Technic::SolderPackInstallTask(APPLICATION->network(), item->technic.url, item->technic.slug,
                                                    versionId, item->technic.minecraftVersion));
        return decorate(new Technic::SingleZipPackInstallTask(item->technic.url, item->technic.minecraftVersion));
    }
    return reject(tr("Unknown modpack provider."));
}

void PackCatalog::installedVersions(QString requestId, QString provider, QString packId)
{
    auto request = d->begin(requestId);
    QTimer::singleShot(0, this, [this, request, provider, packId] {
        if (request->done) return;
        if (!QStringList{"curseforge", "modrinth"}.contains(provider) || packId.isEmpty()) {
            d->fail(request, tr("This pack does not have a supported update provider."));
            return;
        }
        auto item = std::make_shared<State::Entry>();
        item->resource = std::make_shared<ModPlatform::IndexedPack>();
        item->resource->addonId = packId;
        d->packs[provider].insert(packId, item);
        d->resourceVersions(request, provider, item);
    });
}

InstanceTask* PackCatalog::createUpdateTask(QString provider, QString packId, QString versionId, QString instanceId, QWidget* parent, QString* error)
{
    const auto item = d->packs.value(provider).value(packId);
    if (!QStringList{"curseforge", "modrinth"}.contains(provider) || !item || !item->versionsLoaded || !item->files.contains(versionId) ||
        !networkUrl(item->files.value(versionId).downloadUrl)) {
        if (error) *error = tr("Check for updates and select an available pack version first.");
        return nullptr;
    }
    const auto version = item->files.value(versionId);
    auto* task = new InstanceImportTask(QUrl(version.downloadUrl), true, parent,
                                      {{"pack_id", packId}, {"pack_version_id", versionId}, {"original_instance_id", instanceId}});
    task->setOriginalName({}, version.versionNumber.isEmpty() ? version.version : version.versionNumber);
    return task;
}
}  // namespace Awake::Web
