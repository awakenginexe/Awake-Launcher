// SPDX-License-Identifier: GPL-3.0-only
#include "WorkspaceServices.h"
#include "CoreBackend.h"
#include "Contract.h"
#include "InboxService.h"
#include <archive.h>
#include <archive_entry.h>
#include "awake/integration/DurableIO.h"
#include "awake/integration/ProfileAccess.h"
#include "awake/integration/TransactionEngine.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QSemaphore>
#include <QRunnable>
#include <QScopeGuard>
#include <qt_windows.h>
using namespace Awake::Core;
using namespace Awake::Integration;
namespace Awake::Core {
struct CoreBackendTestAccess {
    static OperationStore& store(CoreBackend& backend) { return *backend.m_store; }
    static void block(CoreBackend& backend, QSemaphore& entered, QSemaphore& finish) {
        backend.m_pool.setMaxThreadCount(1);
        backend.m_pool.start(QRunnable::create([&] { entered.release(); finish.acquire(); }));
        entered.acquire();
    }
    static QJsonObject submit(CoreBackend& backend, const QString& method, const QJsonObject& params) {
        QJsonObject response;
        try { backend.submit({"fixture", method, {}, params}, [&](QJsonObject result) { response = result; }); }
        catch (const ServiceError& e) { response = {{"error", QJsonObject{{"code", e.code}, {"message", e.message}, {"retryable", e.retryable}}}}; }
        return response;
    }
};
}
namespace {
QByteArray jar(const QString& id) { return "fixture jar bytes: " + id.toUtf8(); }
QByteArray digest(const QByteArray& value, QCryptographicHash::Algorithm algorithm) { return QCryptographicHash::hash(value, algorithm).toHex(); }
QByteArray response(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
ModrinthService::Fetch network(int dependencies = 1, bool collision = false, bool newestBeta = false)
{
    return [dependencies, collision, newestBeta](const QUrl& url, qint64 limit) -> QByteArray {
        const auto path = url.path();
        if (url.host() == "piston-meta.mojang.com") return response({{"versions", QJsonArray{QJsonObject{{"id", "1.21.1"}}}}});
        if (url.host() == "meta.fabricmc.net") return QJsonDocument(QJsonArray{QJsonObject{{"loader", QJsonObject{{"version", "0.16.9"}}}}}).toJson();
        if (url.host() == "cdn.modrinth.com") return jar(QFileInfo(path).completeBaseName());
        if (path.contains("/version_file/")) return network(dependencies, collision, newestBeta)(QUrl("https://api.modrinth.com/v2/version/p0-v1"), limit);
        if (path.contains("/version/")) {
            const auto id = path.section('/', -1), project = id.section('-', 0, 0), version = id.section('-', 1, 1);
            const auto data = jar(id); QJsonArray deps;
            if (project == "p0") for (int i = 1; i <= dependencies; ++i) deps.append(QJsonObject{{"project_id", "p" + QString::number(i)},
                {"version_id", "p" + QString::number(i) + '-' + version}, {"dependency_type", "required"}});
            return response({{"project_id", project}, {"id", id}, {"name", id}, {"version_number", version}, {"date_published", version == "v2" ? "2026-02-01T00:00:00Z" : "2026-01-01T00:00:00Z"},
                {"version_type", newestBeta && version == "v2" ? "beta" : "release"}, {"game_versions", QJsonArray{"1.21.1"}}, {"loaders", QJsonArray{"fabric"}}, {"dependencies", deps},
                {"files", QJsonArray{QJsonObject{{"filename", collision && project == "p1" ? "p0-v1.jar" : id + ".jar"}, {"url", "https://cdn.modrinth.com/data/" + id + ".jar"},
                    {"primary", true}, {"size", data.size()}, {"hashes", QJsonObject{{"sha512", QString::fromLatin1(digest(data, QCryptographicHash::Sha512))},
                        {"sha1", QString::fromLatin1(digest(data, QCryptographicHash::Sha1))}}}}}}});
        }
        if (path.contains("/project/") && path.endsWith("/version")) {
            const auto project = path.section('/', -2, -2); const auto fetch = network(dependencies, collision, newestBeta);
            const auto first = QJsonDocument::fromJson(fetch(QUrl("https://api.modrinth.com/v2/version/" + project + "-v1"), limit)).object();
            const auto second = QJsonDocument::fromJson(fetch(QUrl("https://api.modrinth.com/v2/version/" + project + "-v2"), limit)).object();
            return QJsonDocument(newestBeta ? QJsonArray{second, first} : QJsonArray{first, second}).toJson();
        }
        if (path.contains("/project/")) {
            const auto project = path.section('/', -1);
            return response({{"id", project}, {"title", project}, {"slug", project}, {"project_type", "mod"}, {"client_side", "required"}, {"server_side", "required"}});
        }
        throw ServiceError{"DOWNLOAD_FAILED", "Unexpected fixture route: " + path};
    };
}
struct Fixture {
    QTemporaryDir temporary;
    QString profile;
    std::unique_ptr<OperationStore> ownedStore;
    OperationStore& store;
    WorkspaceServices services;
    QJsonObject workspace; int key = 0;
    Fixture(int dependencies = 1, QString retained = {}, ModrinthService::Fetch fixture = {}, OperationStore* external = nullptr)
        : profile(retained.isEmpty() ? temporary.path() + "/core" : retained), ownedStore(external ? nullptr : std::make_unique<OperationStore>(profile + "/operations")),
          store(external ? *external : *ownedStore), services(profile, store, {}, fixture ? std::move(fixture) : network(dependencies))
    {
        if (!createPrivateStorageDirectory(profile)) throw ServiceError{"IO_ERROR", "Fixture profile is not private."};
        for (const auto* child : {"catalog", "ownership", "workspaces"}) if (!createPrivateStorageDirectory(profile + '/' + child)) throw ServiceError{"IO_ERROR", "Fixture privacy setup failed."};
        if (!external && !store.open()) throw ServiceError{"IO_ERROR", "Fixture store failed."};
        const QJsonObject params{{"idempotencyKey", "create"}, {"name", "Disposable native fixture"}, {"minecraftVersion", "1.21.1"}, {"loader", QJsonObject{{"id", "fabric"}, {"version", "0.16.9"}}}};
        const auto id = begin("workspaces.create", params); services.create(id, params);
        workspace = services.operation(id)["result"].toObject()["workspace"].toObject();
    }
    QString begin(const QString& method, const QJsonObject& request)
    {
        const auto begun = store.begin(method, request["idempotencyKey"].toString(), request); requireResult(begun);
        const auto id = begun["result"].toObject()["operationId"].toString(); services.saveContext(id, method, request); return id;
    }
    QJsonObject inventory(int offset = 0) { return services.inventory({{"physicalId", workspace["physicalId"]}, {"offset", offset}, {"limit", 200}}); }
    QString prepare(const QJsonArray& changes)
    {
        const QJsonObject params{{"idempotencyKey", "prepare-" + QString::number(++key)}, {"physicalId", workspace["physicalId"]},
            {"instanceRevision", inventory()["instanceRevision"]}, {"changes", changes}};
        const auto id = begin("plans.prepare", params); services.prepare(id, params); return id;
    }
    void apply(const QString& id)
    {
        const auto plan = services.plan({{"operationId", id}}); const QJsonObject params{{"idempotencyKey", "apply-" + QString::number(++key)},
            {"operationId", id}, {"planDigest", plan["planDigest"]}, {"instanceRevision", plan["instanceRevision"]}};
        requireResult(store.begin("transactions.apply", params["idempotencyKey"].toString(), params, id)); services.apply(id, params);
    }
    QJsonObject item(const QString& project)
    { for (const auto& value : inventory()["items"].toArray()) if (value.toObject()["projectId"] == project) return value.toObject(); return {}; }
};
}
class WorkspaceServicesTest : public QObject {
    Q_OBJECT
private slots:









    void packagesApplyOverridesAndPreserveLocalEdits_data()
    { QTest::addColumn<QString>("format"); QTest::newRow("mrpack") << QString("mrpack"); QTest::newRow("secondary") << QString("mrpack-secondary"); QTest::newRow("curseforge") << QString("curseforge"); }
    void packagesApplyOverridesAndPreserveLocalEdits()
    {
        QFETCH(QString,format); const bool secondary=format=="mrpack-secondary"; if(secondary) format="mrpack";
        try {
            ModrinthService::Fetch fixture;
            if (secondary) fixture=[](const QUrl& url,qint64 limit) {
                auto payload=network()(url,limit); auto raw=QJsonDocument::fromJson(payload).object();
                if(raw["id"]=="p0-v1") {
                    const auto bytes=jar("secondary"); auto artifacts=raw["files"].toArray();
                    artifacts.append(QJsonObject{{"filename","secondary.jar"},{"url","https://cdn.modrinth.com/data/secondary.jar"},{"primary",false},{"size",bytes.size()},{"hashes",QJsonObject{{"sha512",QString::fromLatin1(digest(bytes,QCryptographicHash::Sha512))},{"sha1",QString::fromLatin1(digest(bytes,QCryptographicHash::Sha1))}}}});
                    raw["files"]=artifacts; payload=response(raw);
                }
                return payload;
            };
            Fixture f(1,{},fixture); const auto archivePath=f.temporary.filePath("import.zip");
            const QJsonObject loader{{"id","fabric-0.16.9"},{"primary",true}};
            QJsonObject manifest= format=="mrpack" ? QJsonObject{{"formatVersion",1},{"game","minecraft"},{"dependencies",QJsonObject{{"minecraft","1.21.1"},{"fabric-loader","0.16.9"}}},{"files",QJsonArray{}}} :
                QJsonObject{{"manifestType","minecraftModpack"},{"manifestVersion",1},{"minecraft",QJsonObject{{"version","1.21.1"},{"modLoaders",QJsonArray{loader}}}},{"files",QJsonArray{}},{"overrides","overrides"}};
            if (format=="mrpack") {
                const auto data=jar(secondary ? "secondary" : "p0-v1"); const QString filename=secondary ? "secondary.jar" : "p0-v1.jar";
                manifest["files"]=QJsonArray{QJsonObject{{"path","mods/"+filename},{"fileSize",data.size()},{"hashes",QJsonObject{{"sha512",QString::fromLatin1(digest(data,QCryptographicHash::Sha512))},{"sha1",QString::fromLatin1(digest(data,QCryptographicHash::Sha1))}}},{"downloads",QJsonArray{"https://cdn.modrinth.com/data/"+filename}}}};
            }
            auto* writer=archive_write_new(); archive_write_set_format_zip(writer);
            const auto wide=archivePath.toStdWString(); QVERIFY(archive_write_open_filename_w(writer,wide.c_str())==ARCHIVE_OK);
            const QList<QPair<QByteArray,QByteArray>> contents{{format=="mrpack" ? "modrinth.index.json" : "manifest.json",response(manifest)},{"overrides/config/awake.txt","imported config"}};
            for (const auto& file:contents) {
                auto* entry=archive_entry_new(); archive_entry_set_pathname(entry,file.first.constData()); archive_entry_set_filetype(entry,AE_IFREG); archive_entry_set_perm(entry,0644); archive_entry_set_size(entry,file.second.size());
                QVERIFY(archive_write_header(writer,entry)==ARCHIVE_OK); QCOMPARE(archive_write_data(writer,file.second.constData(),file.second.size()),la_ssize_t(file.second.size())); archive_entry_free(entry);
            }
            QCOMPARE(archive_write_close(writer),ARCHIVE_OK); archive_write_free(writer);
            QFile archiveFile(archivePath); QVERIFY(archiveFile.open(QIODevice::ReadOnly)); const auto payload=archiveFile.readAll(); archiveFile.close();
            InboxService inbox(f.profile); const auto transfer=inbox.begin({{"idempotencyKey","upload"},{"purpose","import"},{"size",QString::number(payload.size())},{"sha256",QString::fromLatin1(digest(payload,QCryptographicHash::Sha256))}});
            const auto token=transfer["token"].toString(); inbox.write({{"token",token},{"offset","0"},{"bytesBase64",QString::fromLatin1(payload.toBase64())}}); inbox.commit({{"token",token}});
            const QJsonObject selection{{"token",token},{"format",format}};
            QCOMPARE(f.services.inspectImport(selection)["files"].toArray().size(),1);
            auto params=selection; params["idempotencyKey"]="import"; params["physicalId"]=f.workspace["physicalId"]; params["instanceRevision"]=f.inventory()["instanceRevision"];
            const auto id=f.begin("imports.prepare",params); f.services.prepareImport(id,params);
            const auto target=f.profile+"/workspaces/"+f.workspace["workspaceId"].toString()+"/.minecraft/config/awake.txt";
            QVERIFY(!QFileInfo::exists(target)); const auto review=f.services.plan({{"operationId",id}}); QVERIFY(review["total"].toInt()>=2);
            for (const auto& value : review["files"].toArray()) {
                const auto file=value.toObject(); const auto path=file["path"].toString();
                QCOMPARE(file["kind"].toString(),path==".minecraft/config/awake.txt" ? QString("override") : path.endsWith(".jar") ? QString("mod") : QString("bookkeeping"));
            }
            f.apply(id);
            QFile config(target); QVERIFY(config.open(QIODevice::ReadOnly)); QCOMPARE(config.readAll(),QByteArray("imported config")); config.close();
            QJsonObject exported{{"physicalId",f.workspace["physicalId"]},{"instanceRevision",f.inventory()["instanceRevision"]}};
            const auto listed=f.services.outboxList(exported);
            QCOMPARE(listed["total"].toInt(),1); QCOMPARE(listed["files"].toArray().first().toObject()["path"].toString(),QString("config/awake.txt"));
            exported["path"]="config/awake.txt"; exported["offset"]="0"; exported["length"]=8;
            const auto first=f.services.outboxRead(exported);
            QCOMPARE(QByteArray::fromBase64(first["bytesBase64"].toString().toLatin1()),QByteArray("imported")); QVERIFY(!first["eof"].toBool());
            exported["offset"]="8"; exported["length"]=262144;
            const auto last=f.services.outboxRead(exported);
            QCOMPARE(QByteArray::fromBase64(last["bytesBase64"].toString().toLatin1()),QByteArray(" config")); QVERIFY(last["eof"].toBool());
            for (const auto* refused : {"../instance.cfg","config/../../instance.cfg","C:/private.txt","config/untracked.txt","mods/p0-v1.jar"}) {
                exported["path"]=refused; QVERIFY_THROWS_EXCEPTION(ServiceError,f.services.outboxRead(exported));
            }
            exported["path"]="config/awake.txt"; exported["offset"]="18446744073709551615";
            QVERIFY_THROWS_EXCEPTION(ServiceError,f.services.outboxRead(exported));
            exported["offset"]="0";
            auto stale=exported; stale["instanceRevision"]=QString(64,'0'); QVERIFY_THROWS_EXCEPTION(ServiceError,f.services.outboxRead(stale));
            if (format=="mrpack") { QCOMPARE(f.inventory()["total"].toInt(),2); QCOMPARE(f.item("p0")["metadataState"].toString(),QString("verified")); }
            QVERIFY(config.open(QIODevice::WriteOnly)); config.write("local user changes"); config.close();
            QVERIFY_THROWS_EXCEPTION(ServiceError,f.services.outboxRead(exported));
            exported["instanceRevision"]=f.inventory()["instanceRevision"];
            QVERIFY_THROWS_EXCEPTION(ServiceError,f.services.outboxRead(exported));
            params["idempotencyKey"]="modified-import"; params["instanceRevision"]=f.inventory()["instanceRevision"]; const auto next=f.begin("imports.prepare",params);
            bool refused=false; try { f.services.prepareImport(next,params); } catch(const ServiceError& e) { refused=e.code=="POLICY_CONFLICT"; } QVERIFY(refused);
            QVERIFY(config.open(QIODevice::ReadOnly)); QCOMPARE(config.readAll(),QByteArray("local user changes"));
        } catch (const ServiceError& error) { QFAIL(qPrintable(error.code+": "+error.message)); }
    }

    void launcherInstanceConnectsEditsAndDisconnects()
    {
        try {
            Fixture f(0); Contract contract;
            const auto home = f.temporary.path() + "/launcher"; QVERIFY(createPrivateStorageDirectory(home));
            const auto instance = home + "/pack", mods = instance + "/minecraft/mods"; QVERIFY(QDir().mkpath(mods + "/.index"));
            const auto tracked = jar("p0-v1");
            QVERIFY(DurableIO::write(instance + "/instance.cfg", "InstanceType=OneSix\nname=Existing pack\n"));
            QVERIFY(DurableIO::write(instance + "/mmc-pack.json", R"({"formatVersion":1,"components":[{"uid":"net.minecraft","version":"1.21.1"},{"uid":"net.fabricmc.fabric-loader","version":"0.16.9"}]})"));
            QVERIFY(DurableIO::write(mods + "/p0-v1.jar", tracked)); QVERIFY(DurableIO::write(mods + "/manual.jar", "manual mod"));
            QVERIFY(DurableIO::write(mods + "/.index/some-name.pw.toml", "name = \"P0\"\nfilename = \"p0-v1.jar\"\nside = \"both\"\n\n[download]\nhash-format = \"sha512\"\nhash = \"" + digest(tracked, QCryptographicHash::Sha512) +
                "\"\nmode = \"url\"\nurl = \"https://cdn.modrinth.com/data/p0-v1.jar\"\n\n[update]\n[update.modrinth]\nmod-id = \"p0\"\nversion = \"p0-v1\"\n"));
            const QJsonObject params{{"idempotencyKey", "adopt"}, {"path", instance}};
            QVERIFY(contract.valid("workspaces.adopt.params", params));
            const auto id = f.begin("workspaces.adopt", params); f.services.adopt(id, params);
            f.workspace = f.services.operation(id)["result"].toObject()["workspace"].toObject();
            QVERIFY(contract.valid("workspace", f.workspace));
            QCOMPARE(f.workspace["sharedRoot"].toString(), QFileInfo(instance).canonicalFilePath());
            QCOMPARE(f.workspace["name"].toString(), QString("Existing pack"));
            QCOMPARE(f.item("p0")["metadataState"].toString(), QString("verified"));
            QCOMPARE(f.inventory()["total"].toInt(), 2);
            bool refused = false;
            try { f.services.rename({{"physicalId", f.workspace["physicalId"]}, {"name", "Other"}}); } catch (const ServiceError& error) { refused = error.code == "UNSUPPORTED_CAPABILITY"; }
            QVERIFY(refused);
            f.apply(f.prepare(QJsonArray{QJsonObject{{"action", "replace"}, {"projectId", "p0"}, {"versionId", "p0-v2"}}}));
            QCOMPARE(f.item("p0")["versionId"].toString(), QString("p0-v2"));
            QVERIFY(QFileInfo::exists(mods + "/p0-v2.jar")); QVERIFY(!QFileInfo::exists(mods + "/p0-v1.jar"));
            QVERIFY(!QFileInfo::exists(mods + "/.index/some-name.pw.toml")); QVERIFY(QFileInfo::exists(mods + "/.index/modrinth-p0.pw.toml"));
            QVERIFY(QFileInfo::exists(mods + "/manual.jar"));
            QVERIFY(InstanceAuthority::blocksNativeLoad(instance));
            const auto released = f.services.release({{"physicalId", f.workspace["physicalId"]}});
            QVERIFY(contract.valid("workspaces.release.result", released));
            QVERIFY(!QFileInfo::exists(instance + "/.awake-integration")); QVERIFY(!InstanceAuthority::blocksNativeLoad(instance));
            QVERIFY(QFileInfo::exists(mods + "/p0-v2.jar")); QVERIFY(QFileInfo::exists(mods + "/manual.jar"));
            QCOMPARE(f.services.list({})["total"].toInt(), 1);
        } catch (const ServiceError& error) { QFAIL(qPrintable(error.code + ": " + error.message)); }
    }
    void policyStatesAreIndependent()
    {
        Fixture f;
        f.apply(f.prepare(QJsonArray{QJsonObject{{"action","install"},{"projectId","p0"},{"versionId","p0-v1"}}}));
        const QJsonValue inventoryId=f.item("p0")["inventoryId"];
        const auto set = [&](QJsonValue pin, bool excluded) {
            f.apply(f.prepare(QJsonArray{QJsonObject{{"action","policy"},{"inventoryId",inventoryId},{"pinnedVersion",pin},{"excluded",excluded}}}));
        };
        const auto check = [&](QJsonValue pin, bool excluded, bool locked) {
            const auto row=f.item("p0"); const auto policy=row["policy"].toObject();
            QCOMPARE(policy["pinnedVersion"],pin); QCOMPARE(policy["excluded"],QJsonValue(excluded));
            QCOMPARE(row["nativeLock"],QJsonValue(false)); QCOMPARE(row["updateLocked"],QJsonValue(locked));
        };
        check(QJsonValue(QJsonValue::Null),false,false);
        set("p0-v1",false); check("p0-v1",false,true);
        QVERIFY(f.services.updates({{"physicalId",f.workspace["physicalId"]}})["updates"].toArray().first().toObject()["excluded"].toBool());
        QVERIFY_THROWS_EXCEPTION(ServiceError,f.prepare(QJsonArray{QJsonObject{{"action","replace"},{"projectId","p0"},{"versionId","p0-v2"}}}));
        set(QJsonValue(QJsonValue::Null),true); check(QJsonValue(QJsonValue::Null),true,true);
        QVERIFY_THROWS_EXCEPTION(ServiceError,f.prepare(QJsonArray{QJsonObject{{"action","replace"},{"projectId","p0"},{"versionId","p0-v2"}}}));
        set("p0-v1",true); check("p0-v1",true,true);
        set(QJsonValue(QJsonValue::Null),false); check(QJsonValue(QJsonValue::Null),false,false);
        const auto instance=f.profile+"/workspaces/"+f.workspace["workspaceId"].toString();
        const auto index=instance+"/.minecraft/mods/.index/modrinth-p0.pw.toml";
        QFile native(index); QVERIFY(native.open(QIODevice::ReadOnly)); auto data=native.readAll(); native.close();
        QVERIFY(data.contains("x-prismlauncher-lock-update = false")); data.replace("x-prismlauncher-lock-update = false","x-prismlauncher-lock-update = true");
        QVERIFY(native.open(QIODevice::WriteOnly)); QCOMPARE(native.write(data),qint64(data.size())); native.close();
        const auto row=f.item("p0");
        QCOMPARE(row["nativeLock"],QJsonValue(true)); QCOMPARE(row["updateLocked"],QJsonValue(true));
        QCOMPARE(row["policy"].toObject()["pinnedVersion"],QJsonValue(QJsonValue::Null));
        QCOMPARE(row["policy"].toObject()["excluded"],QJsonValue(false));
        QVERIFY_THROWS_EXCEPTION(ServiceError,f.prepare(QJsonArray{QJsonObject{{"action","policy"},{"inventoryId",inventoryId},{"pinnedVersion",QJsonValue(QJsonValue::Null)},{"excluded",false}}}));
    }
    void deletionRemovesFilesAndCatalogAndRenameIsDurable()
    {
        Fixture f; f.apply(f.prepare(QJsonArray{QJsonObject{{"action","install"},{"projectId","p0"},{"versionId","p0-v1"}}}));
        const auto renamed = f.services.rename({{"physicalId", f.workspace["physicalId"]}, {"name", "Renamed"}})["workspace"].toObject();
        QCOMPARE(renamed["name"].toString(), QString("Renamed"));
        QCOMPARE(f.services.list({{"limit", 10}})["workspaces"].toArray().first().toObject()["name"].toString(), QString("Renamed"));
        const auto root = f.profile + "/workspaces/" + f.workspace["workspaceId"].toString();
        QVERIFY(QFileInfo::exists(root + "/.minecraft/mods"));
        const QJsonObject params{{"idempotencyKey", "delete"}, {"physicalId", f.workspace["physicalId"]}};
        const auto id = f.begin("workspaces.delete", params); f.services.remove(id, params);
        const auto done = f.services.operation(id);
        QCOMPARE(done["state"].toString(), QString("completed")); QCOMPARE(done["kind"].toString(), QString("workspace-delete"));
        QVERIFY(!QFileInfo::exists(root)); QVERIFY(!QFileInfo::exists(f.profile + "/deleted/" + f.workspace["workspaceId"].toString()));
        QCOMPARE(f.services.list({{"limit", 10}})["workspaces"].toArray().size(), 0);
    }
    void policyPinsAreReviewedAndCanBeCleared()
    {
        Fixture f; const auto installed=f.prepare(QJsonArray{QJsonObject{{"action","install"},{"projectId","p0"},{"versionId","p0-v1"}}}); f.apply(installed);
        const QJsonValue inventoryId=f.item("p0")["inventoryId"];
        const auto pin=f.prepare(QJsonArray{QJsonObject{{"action","policy"},{"inventoryId",inventoryId},{"pinnedVersion","p0-v1"},{"excluded",true}}});
        QVERIFY(!f.item("p0")["updateLocked"].toBool()); f.apply(pin); QVERIFY(f.item("p0")["updateLocked"].toBool());
        QVERIFY_THROWS_EXCEPTION(ServiceError,f.prepare(QJsonArray{QJsonObject{{"action","replace"},{"projectId","p0"},{"versionId","p0-v2"}}}));
        const auto clear=f.prepare(QJsonArray{QJsonObject{{"action","policy"},{"inventoryId",inventoryId},{"pinnedVersion",QJsonValue(QJsonValue::Null)},{"excluded",false}}}); f.apply(clear);
        QVERIFY(!f.item("p0")["updateLocked"].toBool());
        const auto update=f.prepare(QJsonArray{QJsonObject{{"action","replace"},{"projectId","p0"},{"versionId","p0-v2"}}}); f.apply(update);
        QCOMPARE(f.item("p0")["versionId"].toString(),QString("p0-v2"));
    }
    void dispatchContentionDoesNotFailOrAuthorizeAnExistingOperation_data()
    {
        QTest::addColumn<QString>("method"); QTest::newRow("apply") << QString("transactions.apply"); QTest::newRow("recovery") << QString("recovery.run");
    }
    void dispatchContentionDoesNotFailOrAuthorizeAnExistingOperation()
    {
        QFETCH(QString, method);
        {
            QTemporaryDir temporary; QSemaphore entered, finish; CoreBackend backend; QString error;
            QVERIFY2(backend.start(temporary.path() + "/core", &error), qPrintable(error));
            try {
                Fixture f(1, temporary.path() + "/core", {}, &CoreBackendTestAccess::store(backend));
                const auto id = f.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"projectId", "p0"}, {"versionId", "p0-v1"}}});
                const auto plan = f.services.plan({{"operationId", id}});
                QJsonObject params{{"operationId", id}, {"idempotencyKey", "first-authorization"}};
                if (QString(method) == "transactions.apply") { params["planDigest"] = plan["planDigest"]; params["instanceRevision"] = plan["instanceRevision"]; }
                else { requireResult(f.store.transition(id, "recovery_required")); params["action"] = "rollback"; }
                CoreBackendTestAccess::block(backend, entered, finish); auto release = qScopeGuard([&] { finish.release(); });
                const auto first = CoreBackendTestAccess::submit(backend, method, params); QVERIFY(first.contains("result"));
                auto other = params; other["idempotencyKey"] = "second-authorization";
                const auto busy = CoreBackendTestAccess::submit(backend, method, other);
                QCOMPARE(busy["error"].toObject()["code"].toString(), QString("BUSY"));
                QCOMPARE(f.services.operation(id)["state"].toString(), QString(method) == "transactions.apply" ? QString("prepared") : QString("recovery_required"));
                QVERIFY(CoreBackendTestAccess::submit(backend, method, params)["result"].toObject()["replayed"].toBool());
                finish.release(); release.dismiss();
                QTRY_VERIFY_WITH_TIMEOUT(f.services.operation(id)["terminal"].toBool(), 15000);
                QCOMPARE(f.services.operation(id)["state"].toString(), QString(method) == "transactions.apply" ? QString("completed") : QString("rolled_back"));
                QVERIFY(!CoreBackendTestAccess::submit(backend, method, other).contains("result"));
            } catch (const ServiceError& e) { QFAIL(qPrintable(e.code + ": " + e.message)); }
        }
    }
    void updateChecksNeverDowngradeANewerBeta()
    {
        try {
            Fixture beta(1, {}, network(1, false, true));
            beta.apply(beta.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"projectId", "p0"}, {"versionId", "p0-v2"}}}));
            const auto updates = beta.services.updates({{"physicalId", beta.workspace["physicalId"]}})["updates"].toArray();
            QVERIFY(!updates.isEmpty()); for (const auto& value : updates) QVERIFY(value.toObject()["candidate"].isNull());
            Fixture release; release.apply(release.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"projectId", "p0"}, {"versionId", "p0-v1"}}}));
            const auto newer = release.services.updates({{"physicalId", release.workspace["physicalId"]}})["updates"].toArray();
            for (const auto& value : newer) QVERIFY(value.toObject()["candidate"].toObject()["versionId"].toString().endsWith("-v2"));
        } catch (const ServiceError& e) { QFAIL(qPrintable(e.code + ": " + e.message)); }
    }
    void localPolicyCannotBlessModifiedProviderBytes()
    {
        try {
            Fixture f; f.apply(f.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"projectId", "p0"}, {"versionId", "p0-v1"}}}));
            const auto root = f.profile + "/workspaces/" + f.workspace["workspaceId"].toString();
            const auto modified = QByteArray("manually replaced mod bytes");
            QVERIFY(DurableIO::write(root + "/.minecraft/mods/p0-v1.jar", modified));
            QFile policy(root + "/.awake-integration/policies.json"); QVERIFY(policy.open(QIODevice::ReadOnly));
            auto contents = QJsonDocument::fromJson(policy.readAll()).object(); policy.close();
            auto projects = contents["projects"].toObject(), entry = projects["p0"].toObject();
            entry["sha256"] = QString::fromLatin1(digest(modified, QCryptographicHash::Sha256)); projects["p0"] = entry; contents["projects"] = projects;
            QVERIFY(DurableIO::write(policy.fileName(), response(contents)));
            QCOMPARE(f.item("p0")["metadataState"].toString(), QString("modified"));
        } catch (const ServiceError& error) { QFAIL(qPrintable(error.code + ": " + error.message)); }
    }
    void startupSettlesUnstartedAndCancelledRequests()
    {
        try {
            Fixture f;
            const auto unstarted = f.store.begin("plans.prepare", "no-context", {})["result"].toObject()["operationId"].toString();
            const auto prepared = f.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"projectId", "p0"}, {"versionId", "p0-v1"}}});
            requireResult(f.store.note(prepared, {}, true));
            f.services.startupRecovery();
            QCOMPARE(f.services.operation(unstarted)["state"].toString(), QString("failed"));
            QCOMPARE(f.services.operation(prepared)["state"].toString(), QString("cancelled"));
            QCOMPARE(f.inventory()["total"].toInt(), 0);
        } catch (const ServiceError& error) { QFAIL(qPrintable(error.code + ": " + error.message)); }
    }
    void collidingProviderArtifactsAreRejectedBeforeReview()
    {
        try {
            Fixture f(1, {}, network(1, true));
            QVERIFY_THROWS_EXCEPTION(ServiceError, f.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"projectId", "p0"}, {"versionId", "p0-v1"}}}));
            QCOMPARE(f.inventory()["total"].toInt(), 0);
        } catch (const ServiceError& error) { QFAIL(qPrintable(error.code + ": " + error.message)); }
    }
    void createInstallDependencyUpdateToggleAndRemove()
    {
        try {
            Fixture f; Contract contract; QVERIFY(contract.valid("workspace", f.workspace));
            const auto install = f.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"projectId", "p0"}, {"versionId", "p0-v1"}}});
            QCOMPARE(f.services.operation(install)["state"].toString(), QString("prepared"));
            QVERIFY(f.services.unsafeToExit()); QCOMPARE(f.inventory()["total"].toInt(), 0);
            const auto plan = f.services.plan({{"operationId", install}}); QVERIFY(contract.valid("plans.get.result", plan));
            QCOMPARE(plan["dependencies"].toArray().size(), 1); f.apply(install);
            QCOMPARE(f.inventory()["total"].toInt(), 2); QCOMPARE(f.item("p0")["metadataState"].toString(), QString("verified"));
            QCOMPARE(f.item("p1")["metadataState"].toString(), QString("verified"));
            f.apply(f.prepare(QJsonArray{QJsonObject{{"action", "replace"}, {"projectId", "p0"}, {"versionId", "p0-v2"}}}));
            QCOMPARE(f.item("p0")["versionId"].toString(), QString("p0-v2"));
            for (const auto* action : {"disable", "enable"}) {
                f.apply(f.prepare(QJsonArray{QJsonObject{{"action", action}, {"inventoryId", f.item("p0")["inventoryId"]}}}));
                QCOMPARE(f.item("p0")["enabled"].toBool(), QString(action) == "enable");
            }
            f.apply(f.prepare(QJsonArray{QJsonObject{{"action", "remove"}, {"inventoryId", f.item("p0")["inventoryId"]}}}));
            QCOMPARE(f.inventory()["total"].toInt(), 1);
            f.apply(f.prepare(QJsonArray{QJsonObject{{"action", "remove"}, {"inventoryId", f.item("p1")["inventoryId"]}}}));
            QCOMPARE(f.inventory()["total"].toInt(), 0); QVERIFY(!f.services.unsafeToExit());
            QVERIFY(contract.valid("operations.get.result", f.services.operation(install)));
        } catch (const ServiceError& error) { QFAIL(qPrintable(error.code + ": " + error.message)); }
    }
    void staleRevisionAndRequiredDependencyRemovalAreBlocked()
    {
        try {
            Fixture f; const auto first = f.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"projectId", "p0"}, {"versionId", "p0-v1"}}}); f.apply(first);
            QVERIFY_THROWS_EXCEPTION(ServiceError, f.prepare(QJsonArray{QJsonObject{{"action", "remove"}, {"inventoryId", f.item("p1")["inventoryId"]}}}));
            const auto update = f.prepare(QJsonArray{QJsonObject{{"action", "replace"}, {"projectId", "p0"}, {"versionId", "p0-v2"}}});
            const auto root = f.profile + "/workspaces/" + f.workspace["workspaceId"].toString();
            QVERIFY(DurableIO::write(root + "/.minecraft/mods/manual.jar", "manual mod"));
            QVERIFY_THROWS_EXCEPTION(ServiceError, f.apply(update));
            QCOMPARE(f.item("p0")["versionId"].toString(), QString("p0-v1"));
            QCOMPARE(f.inventory()["total"].toInt(), 3);
        } catch (const ServiceError& error) { QFAIL(qPrintable(error.code + ": " + error.message)); }
    }
    void threeHundredDependenciesRemainPagedAndVerified()
    {
        try {
            Fixture f(300);
            const auto id = f.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"projectId", "p0"}, {"versionId", "p0-v1"}}});
            const auto plan = f.services.plan({{"operationId", id}, {"limit", 200}});
            QCOMPARE(plan["dependencies"].toArray().size(), 300); QCOMPARE(plan["total"].toInt(), 603); f.apply(id);
            QCOMPARE(f.inventory()["total"].toInt(), 301); QCOMPARE(f.inventory()["items"].toArray().size(), 200); QCOMPARE(f.inventory(200)["items"].toArray().size(), 101);
            for (const auto& value : f.inventory()["items"].toArray()) QCOMPARE(value.toObject()["metadataState"].toString(), QString("verified"));
            for (const auto& value : f.inventory(200)["items"].toArray()) QCOMPARE(value.toObject()["metadataState"].toString(), QString("verified"));
            QJsonArray update;
            for (int i = 0; i <= 300; ++i) update.append(QJsonObject{{"action", "replace"}, {"projectId", "p" + QString::number(i)}, {"versionId", "p" + QString::number(i) + "-v2"}});
            const auto bulk = f.prepare(update); QCOMPARE(f.services.plan({{"operationId", bulk}})["total"].toInt(), 904); f.apply(bulk);
            for (int offset : {0, 200}) for (const auto& value : f.inventory(offset)["items"].toArray()) {
                const auto row = value.toObject(); QCOMPARE(row["metadataState"].toString(), QString("verified")); QVERIFY(row["versionId"].toString().endsWith("-v2"));
            }
        } catch (const ServiceError& error) { QFAIL(qPrintable(error.code + ": " + error.message)); }
    }
};
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == "--crash-fixture") {
        try {
            Fixture f(300, app.arguments()[2]);
            const auto id = f.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"projectId", "p0"}, {"versionId", "p0-v1"}}});
            const auto plan = f.services.plan({{"operationId", id}});
            QTextStream(stdout) << QJsonDocument(QJsonObject{{"operationId", id}, {"workspace", f.workspace}}).toJson(QJsonDocument::Compact) << Qt::endl;
            const QJsonObject params{{"idempotencyKey", "crashing-apply"}, {"operationId", id}, {"planDigest", plan["planDigest"]}, {"instanceRevision", plan["instanceRevision"]}};
            requireResult(f.store.begin("transactions.apply", "crashing-apply", params, id));
            QString error;
            auto lease = PhysicalInstanceLease::acquire(f.profile + "/workspaces/" + f.workspace["workspaceId"].toString(), f.workspace["physicalId"].toString(), f.profile + "/ownership", &error);
            if (!lease) throw ServiceError{"BUSY", error};
            TransactionEngine engine(f.store, lease->root(), ".minecraft/mods");
            engine.apply(id, plan["planDigest"].toString(), plan["instanceRevision"].toString(), lease, [](const QString& phase, int index) {
                if (phase == "after_replace" && index == 150) ExitProcess(86); return true;
            });
            return 87;
        } catch (const ServiceError& error) { QTextStream(stderr) << error.code << ": " << error.message << Qt::endl; return 88; }
    }
    WorkspaceServicesTest test; return QTest::qExec(&test, argc, argv);
}
#include "WorkspaceServices_test.moc"
