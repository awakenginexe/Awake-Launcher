// SPDX-License-Identifier: GPL-3.0-only
#include "WorkspaceServices.h"
#include "InboxService.h"
#include "awake/integration/ProfileAccess.h"
#include <archive.h>
#include <archive_entry.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
using namespace Awake::Core;
using namespace Awake::Integration;
namespace {
QByteArray json(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
QString hash(const QByteArray& value, QCryptographicHash::Algorithm algorithm) { return QString::fromLatin1(QCryptographicHash::hash(value, algorithm).toHex()); }
QByteArray bytes(const QString& id) { return "fixture-jar-" + id.toUtf8(); }
QJsonObject cfFile(int project, int file) {
    const auto data = bytes(QString::number(file));
    QJsonArray dependencies;
    if (project == 12) dependencies.append(QJsonObject{{"modId", 99}, {"relationType", 3}});
    return {{"id", file}, {"modId", project}, {"gameId", 432}, {"isAvailable", true},
        {"displayName", "CurseForge " + QString::number(file)}, {"fileName", "cf-" + QString::number(file) + ".jar"},
        {"releaseType", 1}, {"fileDate", file == 44 ? "2026-02-01T00:00:00Z" : "2026-01-01T00:00:00Z"},
        {"fileLength", data.size()}, {"downloadUrl", "https://edge.forgecdn.net/" + QString::number(file) + ".jar"},
        {"gameVersions", QJsonArray{"1.20.1", "Forge"}},
        {"hashes", QJsonArray{QJsonObject{{"algo", 1}, {"value", hash(data, QCryptographicHash::Sha1)}}}}, {"dependencies", dependencies}};
}
QJsonObject modrinthVersion() {
    const auto data = bytes("modrinth-12");
    return {{"project_id", "12"}, {"id", "mr12v1"}, {"name", "Modrinth 12"}, {"version_number", "1"},
        {"date_published", "2026-01-01T00:00:00Z"}, {"version_type", "release"}, {"game_versions", QJsonArray{"1.20.1"}},
        {"loaders", QJsonArray{"forge"}}, {"dependencies", QJsonArray{}},
        {"files", QJsonArray{QJsonObject{{"filename", "mr-12.jar"}, {"url", "https://cdn.modrinth.com/data/modrinth-12.jar"},
            {"primary", true}, {"size", data.size()}, {"hashes", QJsonObject{{"sha512", hash(data, QCryptographicHash::Sha512)}, {"sha1", hash(data, QCryptographicHash::Sha1)}}}}}}};
}
ModrinthService::Fetch network() {
    return [](const QUrl& url, qint64) -> QByteArray {
        const auto path = url.path();
        if (url.host() == "piston-meta.mojang.com") return json({{"versions", QJsonArray{QJsonObject{{"id", "1.20.1"}}}}});
        if (url.host() == "meta.prismlauncher.org") return json({{"formatVersion", 1}, {"uid", "net.minecraftforge"},
            {"versions", QJsonArray{QJsonObject{{"version", "47.3.0"}, {"requires", QJsonArray{QJsonObject{{"uid", "net.minecraft"}, {"equals", "1.20.1"}}}}}}}});
        if (url.host() == "edge.forgecdn.net") return bytes(QFileInfo(path).completeBaseName());
        if (url.host() == "cdn.modrinth.com") return bytes(QFileInfo(path).completeBaseName());
        if (url.host() == "api.modrinth.com") {
            if (path.contains("/version/")) return json(modrinthVersion());
            if (path.endsWith("/version")) return QJsonDocument(QJsonArray{modrinthVersion()}).toJson();
            if (path.endsWith("/project/12")) return json({{"id", "12"}, {"title", "Modrinth 12"}, {"slug", "modrinth-12"},
                {"project_type", "mod"}, {"client_side", "required"}, {"server_side", "required"}});
        }
        if (url.host() == "api.curseforge.com") {
            const auto parts = path.split('/', Qt::SkipEmptyParts);
            if (parts.size() >= 3 && parts[0] == "v1" && parts[1] == "mods") {
                bool ok = false; const int project = parts[2].toInt(&ok);
                if (ok && (project == 12 || project == 99)) {
                    if (parts.size() == 3) return json({{"data", QJsonObject{{"id", project}, {"gameId", 432}, {"classId", 6},
                        {"name", "CurseForge " + QString::number(project)}, {"slug", "curseforge-" + QString::number(project)},
                        {"summary", "fixture"}, {"allowModDistribution", true}}}});
                    if (parts.size() == 5 && parts[3] == "files") {
                        const int file = parts[4].toInt();
                        if ((project == 12 && (file == 42 || file == 44)) || (project == 99 && file == 43)) return json({{"data", cfFile(project, file)}});
                    }
                    if (parts.size() == 4 && parts[3] == "files") {
                        const int index = QUrlQuery(url).queryItemValue("index").toInt();
                        QJsonArray files; if (!index) { if (project == 12) { files.append(cfFile(12, 44)); files.append(cfFile(12, 42)); } else files.append(cfFile(99, 43)); }
                        return json({{"data", files}, {"pagination", QJsonObject{{"index", index}, {"pageSize", 50}, {"resultCount", files.size()}, {"totalCount", project == 12 ? 2 : 1}}}});
                    }
                }
            }
        }
        throw ServiceError{"DOWNLOAD_FAILED", "Unexpected fixture URL: " + url.toString()};
    };
}
struct Fixture {
    QTemporaryDir temporary; QString profile; OperationStore store; WorkspaceServices services; QJsonObject workspace; int sequence = 0;
    Fixture() : profile(temporary.path() + "/core"), store(profile + "/operations"), services(profile, store, {}, network()) {
        if (!temporary.isValid() || !createPrivateStorageDirectory(profile)) throw ServiceError{"IO_ERROR", "Fixture profile setup failed."};
        for (const auto* child : {"catalog", "ownership", "workspaces"}) if (!createPrivateStorageDirectory(profile + '/' + child)) throw ServiceError{"IO_ERROR", "Fixture directory setup failed."};
        if (!store.open()) throw ServiceError{"IO_ERROR", "Fixture operation store failed."};
        const QJsonObject params{{"idempotencyKey", "create"}, {"name", "CurseForge fixture"}, {"minecraftVersion", "1.20.1"},
            {"loader", QJsonObject{{"id", "forge"}, {"version", "47.3.0"}}}};
        const auto operation = begin("workspaces.create", params); services.create(operation, params);
        workspace = services.operation(operation)["result"].toObject()["workspace"].toObject();
    }
    QString begin(const QString& method, const QJsonObject& request) {
        const auto result = store.begin(method, request["idempotencyKey"].toString(), request); requireResult(result);
        const auto id = result["result"].toObject()["operationId"].toString(); services.saveContext(id, method, request); return id;
    }
    QString next(const QString& prefix) { return prefix + QString::number(++sequence); }
    QJsonObject inventory() { return services.inventory({{"physicalId", workspace["physicalId"]}, {"limit", 200}}); }
    QJsonObject item(const QString& provider, const QString& project) {
        for (const auto& value : inventory()["items"].toArray()) { const auto row = value.toObject();
            if (row["provider"].toString("modrinth") == provider && row["projectId"] == project) return row; }
        return {};
    }
    QString prepare(const QJsonArray& changes) {
        const QJsonObject request{{"idempotencyKey", next("prepare")}, {"physicalId", workspace["physicalId"]},
            {"instanceRevision", inventory()["instanceRevision"]}, {"changes", changes}};
        const auto id = begin("plans.prepare", request); services.prepare(id, request); return id;
    }
    void apply(const QString& id) {
        const auto plan = services.plan({{"operationId", id}});
        const QJsonObject request{{"idempotencyKey", next("apply")}, {"operationId", id}, {"planDigest", plan["planDigest"]}, {"instanceRevision", plan["instanceRevision"]}};
        requireResult(store.begin("transactions.apply", request["idempotencyKey"].toString(), request, id)); services.apply(id, request);
    }
    QString root() const { return profile + "/workspaces/" + workspace["workspaceId"].toString(); }
};
}
class ManagerProvidersTest : public QObject {
    Q_OBJECT
private slots:
    void installsDependenciesKeepsProvidersDistinctUpdatesAndRemoves() {
        try {
            Fixture f;
            const auto install = f.prepare(QJsonArray{QJsonObject{{"action", "install"}, {"provider", "curseforge"}, {"projectId", "12"}, {"versionId", "42"}},
                QJsonObject{{"action", "install"}, {"provider", "modrinth"}, {"projectId", "12"}, {"versionId", "mr12v1"}}});
            QCOMPARE(f.inventory()["total"].toInt(), 0);
            QCOMPARE(f.services.plan({{"operationId", install}})["dependencies"].toArray().size(), 1);
            f.apply(install); QCOMPARE(f.inventory()["total"].toInt(), 3);
            QCOMPARE(f.item("curseforge", "12")["metadataState"].toString(), QString("verified"));
            QCOMPARE(f.item("curseforge", "99")["metadataState"].toString(), QString("verified"));
            QCOMPARE(f.item("modrinth", "12")["metadataState"].toString(), QString("verified"));
            QVERIFY(QFileInfo::exists(f.root() + "/.minecraft/mods/.index/curseforge-12.pw.toml"));
            QVERIFY(QFileInfo::exists(f.root() + "/.minecraft/mods/.index/modrinth-12.pw.toml"));
            QFile policies(f.root() + "/.awake-integration/policies.json"); QVERIFY(policies.open(QIODevice::ReadOnly));
            const auto projects = QJsonDocument::fromJson(policies.readAll()).object()["projects"].toObject();
            policies.close();
            QVERIFY(projects.contains("curseforge:12")); QVERIFY(projects.contains("12")); QVERIFY(projects.contains("curseforge:99"));
            bool found = false; for (const auto& value : f.services.updates({{"physicalId", f.workspace["physicalId"]}})["updates"].toArray()) {
                const auto row = value.toObject(); if (row["provider"] == "curseforge" && row["projectId"] == "12") {
                    QCOMPARE(row["candidate"].toObject()["versionId"].toString(), QString("44")); found = true;
                }
            } QVERIFY(found);
            f.apply(f.prepare(QJsonArray{QJsonObject{{"action", "replace"}, {"provider", "curseforge"}, {"projectId", "12"}, {"versionId", "44"}}}));
            QCOMPARE(f.item("curseforge", "12")["versionId"].toString(), QString("44"));
            QCOMPARE(f.item("curseforge", "12")["metadataState"].toString(), QString("verified"));
            f.apply(f.prepare(QJsonArray{QJsonObject{{"action", "remove"}, {"inventoryId", f.item("curseforge", "12")["inventoryId"]}}}));
            f.apply(f.prepare(QJsonArray{QJsonObject{{"action", "remove"}, {"inventoryId", f.item("curseforge", "99")["inventoryId"]}}}));
            QCOMPARE(f.inventory()["total"].toInt(), 1);
            QCOMPARE(f.item("modrinth", "12")["metadataState"].toString(), QString("verified"));
        } catch (const ServiceError& e) { QFAIL(qPrintable(e.code + ": " + e.message)); }
    }
    void importsCurseForgeFilesAndOverridesAfterReview() {
        try {
            Fixture f; const auto path = f.temporary.filePath("pack.zip");
            const QJsonObject manifest{{"manifestType", "minecraftModpack"}, {"manifestVersion", 1},
                {"minecraft", QJsonObject{{"version", "1.20.1"}, {"modLoaders", QJsonArray{QJsonObject{{"id", "forge-47.3.0"}, {"primary", true}}}}}},
                {"files", QJsonArray{QJsonObject{{"projectID", 12}, {"fileID", 42}, {"required", true}}}}, {"overrides", "overrides"}};
            auto* writer = archive_write_new(); QVERIFY(writer); QCOMPARE(archive_write_set_format_zip(writer), ARCHIVE_OK);
            const auto wide = path.toStdWString(); QCOMPARE(archive_write_open_filename_w(writer, wide.c_str()), ARCHIVE_OK);
            const QList<QPair<QByteArray, QByteArray>> entries{{"manifest.json", json(manifest)}, {"overrides/config/awake.txt", "imported config"}};
            for (const auto& file : entries) {
                auto* entry = archive_entry_new(); archive_entry_set_pathname(entry, file.first.constData()); archive_entry_set_filetype(entry, AE_IFREG);
                archive_entry_set_perm(entry, 0644); archive_entry_set_size(entry, file.second.size());
                QCOMPARE(archive_write_header(writer, entry), ARCHIVE_OK);
                QCOMPARE(archive_write_data(writer, file.second.constData(), file.second.size()), la_ssize_t(file.second.size())); archive_entry_free(entry);
            }
            QCOMPARE(archive_write_close(writer), ARCHIVE_OK); archive_write_free(writer);
            QFile archive(path); QVERIFY(archive.open(QIODevice::ReadOnly)); const auto payload = archive.readAll(); archive.close();
            InboxService inbox(f.profile); const auto transfer = inbox.begin({{"idempotencyKey", "upload"}, {"purpose", "import"},
                {"size", QString::number(payload.size())}, {"sha256", hash(payload, QCryptographicHash::Sha256)}});
            const auto token = transfer["token"].toString(); inbox.write({{"token", token}, {"offset", "0"}, {"bytesBase64", QString::fromLatin1(payload.toBase64())}}); inbox.commit({{"token", token}});
            QJsonObject request{{"token", token}, {"format", "curseforge"}};
            const auto inspection = f.services.inspectImport(request); QCOMPARE(inspection["projects"].toArray().size(), 1); QCOMPARE(inspection["files"].toArray().size(), 1);
            request["idempotencyKey"] = "import"; request["physicalId"] = f.workspace["physicalId"]; request["instanceRevision"] = f.inventory()["instanceRevision"];
            const auto id = f.begin("imports.prepare", request); f.services.prepareImport(id, request);
            const auto configPath = f.root() + "/.minecraft/config/awake.txt";
            QVERIFY(!QFileInfo::exists(configPath)); QCOMPARE(f.inventory()["total"].toInt(), 0);
            QVERIFY(f.services.plan({{"operationId", id}})["total"].toInt() >= 3); f.apply(id);
            QFile config(configPath); QVERIFY(config.open(QIODevice::ReadOnly)); QCOMPARE(config.readAll(), QByteArray("imported config"));
            QCOMPARE(f.inventory()["total"].toInt(), 2);
            QCOMPARE(f.item("curseforge", "12")["metadataState"].toString(), QString("verified"));
            QCOMPARE(f.item("curseforge", "99")["metadataState"].toString(), QString("verified"));
        } catch (const ServiceError& e) { QFAIL(qPrintable(e.code + ": " + e.message)); }
    }
};
QTEST_GUILESS_MAIN(ManagerProvidersTest)
#include "ManagerProviders_test.moc"
