// SPDX-License-Identifier: GPL-3.0-only
// Regression coverage for the five Forge 1.20.1 blockers found with a real 407-JAR pack:
// Connector-loaded Fabric files, combined mod+datapack projects, provider version pins that loader
// metadata accepts, provider requirements the loader metadata does not declare, and add-ons pinning
// different library versions. Fixture metadata mirrors the shapes observed on Modrinth; no real mod
// bytes are embedded.
#include "WorkspaceServices.h"
#include "Contract.h"
#include "InboxService.h"
#include "ModMetadata.h"
#include "awake/integration/ProfileAccess.h"
#include <archive.h>
#include <archive_entry.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
using namespace Awake::Core;
using namespace Awake::Integration;
namespace {
QByteArray hex(const QByteArray& data, QCryptographicHash::Algorithm algorithm) { return QCryptographicHash::hash(data, algorithm).toHex(); }
QByteArray zip(const QList<QPair<QByteArray, QByteArray>>& entries)
{
    QTemporaryDir directory; const auto path = directory.filePath("fixture.zip");
    auto* writer = archive_write_new(); archive_write_set_format_zip(writer);
    const auto wide = path.toStdWString(); archive_write_open_filename_w(writer, wide.c_str());
    for (const auto& file : entries) {
        auto* entry = archive_entry_new(); archive_entry_set_pathname(entry, file.first.constData()); archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644); archive_entry_set_size(entry, file.second.size());
        archive_write_header(writer, entry); archive_write_data(writer, file.second.constData(), size_t(file.second.size())); archive_entry_free(entry);
    }
    archive_write_close(writer); archive_write_free(writer);
    QFile file(path); file.open(QIODevice::ReadOnly); return file.readAll();
}
// A Forge JAR whose mods.toml declares `deps` as {modId, mandatory, range}.
QByteArray forgeJar(const QString& modId, const QString& version, const QList<std::tuple<QString, bool, QString>>& deps = {}, bool jarVersion = false)
{
    QString toml = QString("modLoader=\"javafml\"\nloaderVersion=\"[47,)\"\nlicense=\"MIT\"\n[[mods]]\nmodId=\"%1\"\nversion=\"%2\"\n").arg(modId, jarVersion ? "${file.jarVersion}" : version);
    toml += QString("[[dependencies.%1]]\nmodId=\"forge\"\nmandatory=true\nversionRange=\"[47,)\"\nordering=\"NONE\"\nside=\"BOTH\"\n").arg(modId);
    for (const auto& [id, mandatory, range] : deps)
        toml += QString("[[dependencies.%1]]\nmodId=\"%2\"\nmandatory=%3\nversionRange=\"%4\"\nordering=\"NONE\"\nside=\"BOTH\"\n").arg(modId, id, mandatory ? "true" : "false", range);
    QList<QPair<QByteArray, QByteArray>> entries{{"META-INF/mods.toml", toml.toUtf8()}, {"pack.mcmeta", "{}"}};
    if (jarVersion) entries.prepend({"META-INF/MANIFEST.MF", ("Manifest-Version: 1.0\r\nImplementation-Version: " + version + "\r\n").toUtf8()});
    return zip(entries);
}
QByteArray fabricJar(const QString& id, const QString& version, const QJsonObject& depends = {})
{ return zip({{"fabric.mod.json", QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"id", id}, {"version", version}, {"depends", depends}}).toJson()}}); }

struct Version { QString project, id, number; QStringList loaders; QByteArray jar; QJsonArray deps; QString date; };
struct Catalogue {
    QMap<QString, QJsonObject> projects; QMap<QString, Version> versions;
    void project(const QString& id, const QString& title, const QStringList& loaders = {"forge"}, const QString& type = "mod")
    { projects[id] = {{"id", id}, {"slug", id.toLower()}, {"title", title}, {"project_type", type}, {"loaders", QJsonArray::fromStringList(loaders)}, {"client_side", "required"}, {"server_side", "required"}}; }
    void version(const Version& value) { versions[value.id] = value; }
    static QJsonObject dep(const QString& project, const QString& version = {}, const QString& type = "required")
    { return {{"project_id", project}, {"version_id", version.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(version)}, {"dependency_type", type}}; }
    QJsonObject raw(const Version& v) const
    {
        const auto name = v.id + ".jar";
        return {{"project_id", v.project}, {"id", v.id}, {"name", v.id}, {"version_number", v.number}, {"date_published", v.date.isEmpty() ? "2026-01-01T00:00:00Z" : v.date},
            {"version_type", "release"}, {"game_versions", QJsonArray{"1.20.1"}}, {"loaders", QJsonArray::fromStringList(v.loaders)}, {"dependencies", v.deps},
            {"files", QJsonArray{QJsonObject{{"filename", name}, {"url", "https://cdn.modrinth.com/data/" + name}, {"primary", true}, {"size", v.jar.size()},
                {"hashes", QJsonObject{{"sha512", QString::fromLatin1(hex(v.jar, QCryptographicHash::Sha512))}, {"sha1", QString::fromLatin1(hex(v.jar, QCryptographicHash::Sha1))}}}}}}};
    }
    QString sha512(const QString& id) const { return QString::fromLatin1(hex(versions[id].jar, QCryptographicHash::Sha512)); }
    ModrinthService::Fetch fetch() const
    {
        return [this](const QUrl& url, qint64) -> QByteArray {
            const auto path = url.path(); const auto json = [](const QJsonValue& v) { return v.isArray() ? QJsonDocument(v.toArray()).toJson() : QJsonDocument(v.toObject()).toJson(); };
            if (url.host() == "piston-meta.mojang.com") return json(QJsonObject{{"versions", QJsonArray{QJsonObject{{"id", "1.20.1"}}}}});
            if (url.host() == "meta.prismlauncher.org") return json(QJsonObject{{"formatVersion", 1}, {"uid", "net.minecraftforge"},
                {"versions", QJsonArray{QJsonObject{{"version", "47.4.0"}, {"requires", QJsonArray{QJsonObject{{"uid", "net.minecraft"}, {"equals", "1.20.1"}}}}}}}});
            if (url.host() == "cdn.modrinth.com") return versions[QFileInfo(path).completeBaseName()].jar;
            if (path.contains("/version_file/")) {
                const auto hash = path.section('/', -1);
                for (const auto& v : versions) if (sha512(v.id) == hash) return json(raw(v));
                throw ServiceError{"DOWNLOAD_FAILED", "Unknown fixture hash."};
            }
            if (path.startsWith("/v2/version/")) {
                const auto id = path.section('/', -1);
                if (!versions.contains(id)) throw ServiceError{"DOWNLOAD_FAILED", "Unknown fixture version " + id};
                return json(raw(versions[id]));
            }
            if (path.endsWith("/version")) {
                const auto project = path.section('/', -2, -2); const auto query = QUrlQuery(url).queryItemValue("loaders", QUrl::FullyDecoded);
                QJsonArray list;
                for (const auto& v : versions) if (v.project == project) {
                    bool match = query.isEmpty();
                    for (const auto& loader : v.loaders) match = match || query.contains('"' + loader + '"');
                    if (match) list.append(raw(v));
                }
                return json(list);
            }
            if (path.startsWith("/v2/project/")) {
                const auto id = path.section('/', -1);
                if (!projects.contains(id)) throw ServiceError{"DOWNLOAD_FAILED", "Unknown fixture project " + id};
                return json(projects[id]);
            }
            throw ServiceError{"DOWNLOAD_FAILED", "Unexpected fixture route " + path};
        };
    }
};

// Shapes from Modrinth: EasyMagic Forge pins Puzzles Lib v8.0.15 while its mods.toml requires puzzleslib [8.0.0,).
// SkeletonAIFix Forge requires Forge Config API Port (Fabric-only for 1.20.1) without declaring it in mods.toml.
Catalogue blockers()
{
    Catalogue c;
    c.project("QAGBst4M", "Puzzles Lib", {"fabric", "forge", "neoforge"});
    c.version({"QAGBst4M", "V4g0yKzo", "v8.0.15-1.20.1-Forge", {"forge"}, forgeJar("puzzleslib", "8.0.15"), {}, "2025-01-01T00:00:00Z"});
    c.version({"QAGBst4M", "lib8133", "v8.1.33-1.20.1-Forge", {"forge"}, forgeJar("puzzleslib", "8.1.33", {}, true), {}, "2026-01-01T00:00:00Z"});
    c.project("9hx3AbJM", "Easy Magic", {"fabric", "forge"});
    c.version({"9hx3AbJM", "W2xPEG9I", "v8.0.1-1.20.1-Forge", {"forge"}, forgeJar("easymagic", "8.0.1", {{"puzzleslib", true, "[8.0.0,)"}}), {Catalogue::dep("QAGBst4M", "V4g0yKzo")}});
    c.project("strictreq", "Strict Requester");
    c.version({"strictreq", "strict1", "1.0", {"forge"}, forgeJar("strict", "1.0", {{"puzzleslib", true, "[8.1.29,)"}}), {Catalogue::dep("QAGBst4M", "lib8133")}});
    c.project("ohNO6lps", "Forge Config API Port", {"fabric", "forge", "neoforge"});
    c.version({"ohNO6lps", "fcapfab", "v8.0.3-1.20.1-Fabric", {"fabric"}, fabricJar("forgeconfigapiport", "8.0.3"), {}});
    c.project("jn24bUJo", "Skeleton AI Fix", {"fabric", "forge"});
    c.version({"jn24bUJo", "VMGmo5Si", "v20.1.1-1.20.1-Forge", {"forge"}, forgeJar("skeletonaifix", "20.1.1", {{"puzzleslib", true, "[8.1.29,)"}}),
        {Catalogue::dep("QAGBst4M"), Catalogue::dep("ohNO6lps")}});
    c.project("needsfcap", "Declares Config Port");
    c.version({"needsfcap", "needs1", "1.0", {"forge"}, forgeJar("needsfcap", "1.0", {{"forgeconfigapiport", true, "[8,)"}}), {Catalogue::dep("ohNO6lps")}});
    // Refined Storage add-ons pinning different RS builds whose mods.toml ranges accept 1.12.4.
    c.project("KDvYkUg3", "Refined Storage", {"forge", "neoforge"});
    c.version({"KDvYkUg3", "DiI2vAev", "1.12.3", {"forge"}, forgeJar("refinedstorage", "1.12.3"), {}, "2025-06-01T00:00:00Z"});
    c.version({"KDvYkUg3", "ZITLFjjf", "1.12.4", {"forge", "neoforge"}, forgeJar("refinedstorage", "1.12.4"), {}, "2025-09-01T00:00:00Z"});
    c.project("Sjzdj2cy", "RSInfinityBooster");
    c.version({"Sjzdj2cy", "UkT7CCGr", "1.20.1-1.0+41", {"forge"}, forgeJar("rsinfinitybooster", "1.0", {{"refinedstorage", true, "[1.12.0,)"}}), {Catalogue::dep("KDvYkUg3", "DiI2vAev")}});
    c.project("Tlo2tahX", "Extra Disks");
    c.version({"Tlo2tahX", "disks1", "1.20.1-3.0.4", {"forge"}, forgeJar("extradisks", "3.0.4", {{"refinedstorage", true, "[1.12.3,1.13)"}}), {Catalogue::dep("KDvYkUg3", "ZITLFjjf")}});
    // Combined mod+datapack project (Incendium-like) and a datapack-only project.
    c.project("ZVzW5oNS", "Incendium", {"datapack", "fabric", "forge", "neoforge", "quilt"});
    c.version({"ZVzW5oNS", "uUqarp2H", "5.3.5", {"fabric", "forge", "neoforge", "quilt"}, forgeJar("incendium", "5.3.5"), {}});
    c.version({"ZVzW5oNS", "b9pN4Twz", "5.3.5-datapack", {"datapack"}, zip({{"pack.mcmeta", "{}"}}), {}});
    c.project("dpOnly01", "Datapack Only", {"datapack"}, "datapack");
    c.version({"dpOnly01", "dpv1", "1.0", {"datapack"}, zip({{"pack.mcmeta", "{}"}}), {}});
    // Sinytra Connector, Forgified Fabric API and a Fabric-only mod requiring Fabric API.
    c.project("u58R1TMW", "Sinytra Connector", {"forge", "neoforge"});
    c.version({"u58R1TMW", "1MQDrKN7", "1.0.0-beta.49+1.20.1", {"forge"}, forgeJar("connector", "1.0.0-beta.49"), {Catalogue::dep("Aqlf1Shp")}});
    c.project("Aqlf1Shp", "Forgified Fabric API", {"forge", "neoforge"});
    c.version({"Aqlf1Shp", "ffapi1", "0.92.2+1.11.12+1.20.1", {"forge"}, forgeJar("fabric_api", "0.92.2"), {}});
    c.project("P7dR8mSH", "Fabric API", {"fabric", "quilt"});
    c.version({"P7dR8mSH", "XheZ9iGK", "0.92.2+1.20.1", {"fabric"}, fabricJar("fabric-api", "0.92.2"), {}});
    c.project("fabOnly1", "Fabric Only Mod", {"fabric"});
    c.version({"fabOnly1", "fab1", "2.1.0", {"fabric"}, fabricJar("fabriconly", "2.1.0", {{"fabric-api", ">=0.90.0"}}), {Catalogue::dep("P7dR8mSH", "XheZ9iGK")}});
    return c;
}

struct Forge {
    QTemporaryDir temporary; QString profile; OperationStore store; WorkspaceServices services; QJsonObject workspace; int key = 0;
    explicit Forge(const Catalogue& catalogue)
        : profile(temporary.path() + "/core"), store(profile + "/operations"), services(profile, store, {}, catalogue.fetch())
    {
        if (!createPrivateStorageDirectory(profile)) throw ServiceError{"IO_ERROR", "Fixture profile is not private."};
        for (const auto* child : {"catalog", "ownership", "workspaces"}) createPrivateStorageDirectory(profile + '/' + child);
        if (!store.open()) throw ServiceError{"IO_ERROR", "Fixture store failed."};
        const QJsonObject params{{"idempotencyKey", "create"}, {"name", "Forge fixture"}, {"minecraftVersion", "1.20.1"}, {"loader", QJsonObject{{"id", "forge"}, {"version", "47.4.0"}}}};
        const auto id = begin("workspaces.create", params); services.create(id, params);
        workspace = services.operation(id)["result"].toObject()["workspace"].toObject();
    }
    QString begin(const QString& method, const QJsonObject& request)
    {
        const auto begun = store.begin(method, request["idempotencyKey"].toString(), request); requireResult(begun);
        const auto id = begun["result"].toObject()["operationId"].toString(); services.saveContext(id, method, request); return id;
    }
    QString revision() { return services.inventory({{"physicalId", workspace["physicalId"]}, {"limit", 200}})["instanceRevision"].toString(); }
    QJsonArray items() { return services.inventory({{"physicalId", workspace["physicalId"]}, {"limit", 200}})["items"].toArray(); }
    QJsonObject params(const QJsonArray& changes, const QJsonArray& resolutions = {})
    {
        QJsonObject value{{"idempotencyKey", "prepare-" + QString::number(++key)}, {"physicalId", workspace["physicalId"]}, {"instanceRevision", revision()}, {"changes", changes}};
        if (!resolutions.isEmpty()) value["dependencyResolutions"] = resolutions;
        return value;
    }
    QString prepare(const QJsonArray& changes, const QJsonArray& resolutions = {})
    { const auto request = params(changes, resolutions); const auto id = begin("plans.prepare", request); services.prepare(id, request); return id; }
    ServiceError refused(const QJsonArray& changes, const QJsonArray& resolutions = {})
    {
        try { prepare(changes, resolutions); } catch (const ServiceError& error) { return error; }
        return {"NONE", "prepare unexpectedly succeeded"};
    }
    void apply(const QString& id)
    {
        const auto plan = services.plan({{"operationId", id}});
        const QJsonObject request{{"idempotencyKey", "apply-" + QString::number(++key)}, {"operationId", id}, {"planDigest", plan["planDigest"]}, {"instanceRevision", plan["instanceRevision"]}};
        requireResult(store.begin("transactions.apply", request["idempotencyKey"].toString(), request, id)); services.apply(id, request);
    }
    QString inventoryId(const QString& project)
    { for (const auto& value : items()) if (value.toObject()["projectId"] == project) return value.toObject()["inventoryId"].toString(); return {}; }
    QString importPack(const QJsonArray& files, const QJsonArray& resolutions = {}, QString* operation = nullptr)
    {
        const auto manifest = QJsonObject{{"formatVersion", 1}, {"game", "minecraft"}, {"versionId", "1"}, {"name", "fixture"},
            {"dependencies", QJsonObject{{"minecraft", "1.20.1"}, {"forge", "47.4.0"}}}, {"files", files}};
        const auto payload = zip({{"modrinth.index.json", QJsonDocument(manifest).toJson(QJsonDocument::Compact)}});
        InboxService inbox(profile);
        const auto transfer = inbox.begin({{"idempotencyKey", "upload-" + QString::number(++key)}, {"purpose", "import"}, {"size", QString::number(payload.size())},
            {"sha256", QString::fromLatin1(hex(payload, QCryptographicHash::Sha256))}});
        const auto token = transfer["token"].toString();
        inbox.write({{"token", token}, {"offset", "0"}, {"bytesBase64", QString::fromLatin1(payload.toBase64())}}); inbox.commit({{"token", token}});
        QJsonObject request{{"idempotencyKey", "import-" + QString::number(++key)}, {"physicalId", workspace["physicalId"]}, {"instanceRevision", revision()}, {"token", token}, {"format", "mrpack"}};
        if (!resolutions.isEmpty()) request["dependencyResolutions"] = resolutions;
        const auto id = begin("imports.prepare", request); if (operation) *operation = id;
        services.prepareImport(id, request); return id;
    }
};
QJsonObject packFile(const Catalogue& c, const QString& version)
{
    const auto& v = c.versions[version];
    return {{"path", "mods/" + v.id + ".jar"}, {"fileSize", v.jar.size()}, {"downloads", QJsonArray{"https://cdn.modrinth.com/data/" + v.id + ".jar"}},
        {"hashes", QJsonObject{{"sha512", c.sha512(version)}, {"sha1", QString::fromLatin1(hex(v.jar, QCryptographicHash::Sha1))}}}};
}
QJsonObject install(const QString& project, const QString& version) { return {{"action", "install"}, {"projectId", project}, {"versionId", version}}; }
QJsonObject resolution(const QString& project, const QJsonValue& version) { return {{"projectId", project}, {"versionId", version}}; }
QJsonObject only(const ServiceError& error, const QString& kind)
{
    for (const auto& value : error.details["conflicts"].toArray()) if (value.toObject()["kind"] == kind) return value.toObject();
    return {};
}
}

class DependencyResolutionTest : public QObject {
    Q_OBJECT
private slots:
    void rangesFollowLoaderSemantics()
    {
        QCOMPARE(compareVersions("1.0", "1.0.0"), 0); QVERIFY(compareVersions("1.0-beta", "1.0") < 0);
        QVERIFY(compareVersions("1.0-rc1", "1.0-beta2") > 0); QVERIFY(compareVersions("8.1.33", "8.0.15") > 0); QVERIFY(compareVersions("1.10", "1.9") > 0);
        QCOMPARE(satisfies("forge", "[8.0.0,)", "8.1.33"), RangeResult::Satisfied);
        QCOMPARE(satisfies("forge", "[8.1.29,)", "8.0.15"), RangeResult::Violated);
        QCOMPARE(satisfies("forge", "[1.20.1]", "1.20.1"), RangeResult::Satisfied);
        QCOMPARE(satisfies("forge", "(,2.0)", "2.0"), RangeResult::Violated);
        QCOMPARE(satisfies("forge", "[1.0,2.0),[3.0,)", "3.1"), RangeResult::Satisfied);
        QCOMPARE(satisfies("forge", "[1.12.3,1.13)", "1.12.4"), RangeResult::Satisfied);
        QCOMPARE(satisfies("forge", "1.0", "0.1"), RangeResult::Satisfied); // Maven soft recommendation
        QCOMPARE(satisfies("forge", "", "anything"), RangeResult::Satisfied);
        QCOMPARE(satisfies("forge", "[1.0", "1.0"), RangeResult::Unknown);
        QCOMPARE(satisfies("forge", "[1.0,)", ""), RangeResult::Unknown);
        QCOMPARE(satisfies("fabric", ">=0.14.0", "0.15.11"), RangeResult::Satisfied);
        QCOMPARE(satisfies("fabric", "~1.2", "1.3.0"), RangeResult::Violated);
        QCOMPARE(satisfies("fabric", "^1.2.3", "1.9.0+build.4"), RangeResult::Satisfied);
        QCOMPARE(satisfies("fabric", "1.2.x", "1.2.9"), RangeResult::Satisfied);
        QCOMPARE(satisfies("fabric", "1.2.x", "1.3"), RangeResult::Violated);
        QCOMPARE(satisfies("fabric", ">=1.0 <2.0", "2.0"), RangeResult::Violated);
        QCOMPARE(satisfies("fabric", "<0.5 || >=1.0", "1.1"), RangeResult::Satisfied);
        QCOMPARE(satisfies("fabric", "$weird", "1.0"), RangeResult::Violated);
    }
    void jarMetadataIsReadBounded()
    {
        QTemporaryDir directory; const auto write = [&](const QString& name, const QByteArray& bytes) { QFile file(directory.filePath(name)); file.open(QIODevice::WriteOnly); file.write(bytes); return file.fileName(); };
        const auto forge = inspectJar(write("a.jar", forgeJar("easymagic", "8.0.1", {{"puzzleslib", true, "[8.0.0,)"}, {"jei", false, "[15,)"}})), "forge");
        QVERIFY(forge.readable); QCOMPARE(forge.format, QString("forge")); QCOMPARE(forge.mods.first().first, QString("easymagic"));
        QCOMPARE(forge.dependencies.size(), 3); QCOMPARE(forge.dependencies[1].modId, QString("puzzleslib")); QVERIFY(forge.dependencies[1].mandatory); QVERIFY(!forge.dependencies[2].mandatory);
        const auto manifest = inspectJar(write("b.jar", forgeJar("puzzleslib", "8.1.33", {}, true)), "forge");
        QCOMPARE(manifest.mods.first().second, QString("8.1.33"));
        const auto fabric = inspectJar(write("c.jar", fabricJar("fabriconly", "2.1.0", {{"fabric-api", QJsonArray{">=0.90", "<0.1"}}})), "forge");
        QCOMPARE(fabric.format, QString("fabric")); QCOMPARE(fabric.dependencies.first().range, QString(">=0.90 || <0.1"));
        QVERIFY(!inspectJar(write("d.jar", "not a zip"), "forge").readable);
        QVERIFY(!inspectJar(write("e.jar", zip({{"META-INF/mods.toml", "this is = = not toml"}})), "forge").readable);
    }
    void combinedModDatapackProjectsInstallAsMods()
    {
        const auto catalogue = blockers(); Forge f(catalogue);
        try { f.apply(f.prepare({install("ZVzW5oNS", "uUqarp2H")})); } catch (const ServiceError& e) { QFAIL(qPrintable(e.code + ": " + e.message)); }
        bool installed = false; for (const auto& value : f.items()) installed = installed || (value.toObject()["projectId"] == "ZVzW5oNS" && value.toObject()["metadataState"] == "verified");
        QVERIFY(installed);
        // A datapack-only version of the same project, and a datapack-only project, stay refused.
        QCOMPARE(f.refused({install("ZVzW5oNS", "b9pN4Twz")}).code, QString("POLICY_CONFLICT")); // already installed: install action conflicts
        Forge g(catalogue);
        QCOMPARE(g.refused({install("ZVzW5oNS", "b9pN4Twz")}).code, QString("DEPENDENCY_CONFLICT"));
        QCOMPARE(g.refused({install("dpOnly01", "dpv1")}).code, QString("INTEGRITY_FAILED"));
    }
    void versionPinsAreReviewedWithLoaderEvidence()
    {
        const auto catalogue = blockers(); Forge f(catalogue);
        const QJsonArray changes{install("QAGBst4M", "lib8133"), install("9hx3AbJM", "W2xPEG9I")};
        const auto error = f.refused(changes);
        QCOMPARE(error.code, QString("DEPENDENCY_CONFLICT"));
        QVERIFY2(error.message.contains("Puzzles Lib") && error.message.contains("Easy Magic") && !error.message.contains("QAGBst4M"), qPrintable(error.message));
        QVERIFY(Contract().valid("conflictDetails", error.details));
        const auto conflict = only(error, "version_pin");
        QCOMPARE(conflict["title"].toString(), QString("Puzzles Lib")); QCOMPARE(conflict["selected"].toObject()["versionNumber"].toString(), QString("v8.1.33-1.20.1-Forge"));
        const auto request = conflict["requests"].toArray().first().toObject();
        QCOMPARE(request["title"].toString(), QString("Easy Magic")); QCOMPARE(request["requestedVersionNumber"].toString(), QString("v8.0.15-1.20.1-Forge"));
        const auto evidence = request["evidence"].toObject();
        QCOMPARE(evidence["result"].toString(), QString("satisfied")); QCOMPARE(evidence["declaredRange"].toString(), QString("[8.0.0,)"));
        QCOMPARE(evidence["selectedModVersion"].toString(), QString("8.1.33")); QCOMPARE(evidence["modId"].toString(), QString("puzzleslib"));
        QCOMPARE(conflict["resolutions"].toArray(), QJsonArray{"use_version"});
        QCOMPARE(conflict["candidates"].toArray().size(), 1); // the package selected Puzzles Lib explicitly
        // A resolution naming another version than the selected file is refused, not silently applied.
        QCOMPARE(f.refused(changes, {resolution("QAGBst4M", "V4g0yKzo")}).code, QString("INVALID_REQUEST"));
        QString id;
        try { id = f.prepare(changes, {resolution("QAGBst4M", "lib8133")}); } catch (const ServiceError& e) { QFAIL(qPrintable(e.code + ": " + e.message)); }
        const auto plan = f.services.plan({{"operationId", id}});
        QCOMPARE(plan["dependencyResolutions"].toArray().first().toObject()["requestedBy"].toArray().first().toObject()["evidence"].toString(), QString("satisfied"));
        bool reviewed = false; for (const auto& w : plan["warnings"].toArray()) reviewed = reviewed || w.toString().startsWith("Reviewed: Easy Magic keeps Puzzles Lib");
        QVERIFY(reviewed);
        f.apply(id);
        // The accepted resolution is persisted for that Easy Magic version: later plans need no repeat review.
        try { f.apply(f.prepare({install("ZVzW5oNS", "uUqarp2H")})); } catch (const ServiceError& e) { QFAIL(qPrintable("persisted resolution ignored: " + e.code + ": " + e.message)); }
        // Removing the library is still a dependency violation.
        QVERIFY(!only(f.refused({QJsonObject{{"action", "remove"}, {"inventoryId", f.inventoryId("QAGBst4M")}}}), "missing_dependency").isEmpty());
    }
    void installingIntoNewerLibraryNeverDowngradesSilently()
    {
        const auto catalogue = blockers(); Forge f(catalogue);
        f.apply(f.prepare({install("QAGBst4M", "lib8133")}));
        const auto error = f.refused({install("9hx3AbJM", "W2xPEG9I")});
        QCOMPARE(error.code, QString("DEPENDENCY_CONFLICT"));
        const auto conflict = only(error, "version_pin");
        QCOMPARE(conflict["candidates"].toArray().size(), 2); // keep 8.1.33, or knowingly switch to the pinned 8.0.15
        const auto id = f.prepare({install("9hx3AbJM", "W2xPEG9I")}, {resolution("QAGBst4M", "lib8133")});
        for (const auto& value : f.services.plan({{"operationId", id}})["files"].toArray()) QVERIFY(!value.toObject()["path"].toString().contains("V4g0yKzo"));
    }
    void contradictedPinsCannotBeOverridden()
    {
        const auto catalogue = blockers(); Forge f(catalogue);
        const QJsonArray changes{install("QAGBst4M", "V4g0yKzo"), install("strictreq", "strict1")};
        const auto error = f.refused(changes);
        const auto conflict = only(error, "version_pin");
        QCOMPARE(conflict["requests"].toArray().first().toObject()["evidence"].toObject()["result"].toString(), QString("violated"));
        QVERIFY(conflict["resolutions"].toArray().isEmpty());
        const auto again = f.refused(changes, {resolution("QAGBst4M", "V4g0yKzo")});
        QCOMPARE(again.code, QString("DEPENDENCY_CONFLICT")); QVERIFY(!only(again, "version_pin").isEmpty());
    }
    void undeclaredProviderRequirementsNeedExplicitReview()
    {
        const auto catalogue = blockers(); Forge f(catalogue);
        const QJsonArray changes{install("QAGBst4M", "lib8133"), install("jn24bUJo", "VMGmo5Si")};
        const auto error = f.refused(changes);
        const auto conflict = only(error, "missing_dependency");
        QCOMPARE(conflict["title"].toString(), QString("Forge Config API Port")); QVERIFY(conflict["selected"].isNull());
        const auto evidence = conflict["requests"].toArray().first().toObject()["evidence"].toObject();
        QCOMPARE(evidence["result"].toString(), QString("not_declared")); QCOMPARE(evidence["modId"].toString(), QString("forgeconfigapiport"));
        QCOMPARE(conflict["resolutions"].toArray(), QJsonArray{"proceed_without"});
        try { f.apply(f.prepare(changes, {resolution("ohNO6lps", QJsonValue::Null)})); } catch (const ServiceError& e) { QFAIL(qPrintable(e.code + ": " + e.message)); }
        // A mod whose own metadata declares the dependency mandatory cannot proceed without it.
        Forge g(catalogue);
        const auto strict = only(g.refused({install("needsfcap", "needs1")}), "missing_dependency");
        QCOMPARE(strict["requests"].toArray().first().toObject()["evidence"].toObject()["result"].toString(), QString("violated"));
        QVERIFY(strict["resolutions"].toArray().isEmpty());
        QCOMPARE(g.refused({install("needsfcap", "needs1")}, {resolution("ohNO6lps", QJsonValue::Null)}).code, QString("DEPENDENCY_CONFLICT"));
    }
    void addOnsPinningDifferentLibraryBuildsAreReviewedTogether()
    {
        const auto catalogue = blockers(); Forge f(catalogue);
        const QJsonArray changes{install("KDvYkUg3", "ZITLFjjf"), install("Sjzdj2cy", "UkT7CCGr"), install("Tlo2tahX", "disks1")};
        const auto error = f.refused(changes);
        const auto conflict = only(error, "version_pin");
        QCOMPARE(conflict["title"].toString(), QString("Refined Storage"));
        QCOMPARE(conflict["requests"].toArray().size(), 1); // Extra Disks pins the selected 1.12.4 already
        QCOMPARE(conflict["requests"].toArray().first().toObject()["title"].toString(), QString("RSInfinityBooster"));
        try { f.apply(f.prepare(changes, {resolution("KDvYkUg3", "ZITLFjjf")})); } catch (const ServiceError& e) { QFAIL(qPrintable(e.code + ": " + e.message)); }
        // With Refined Storage absent, two add-ons pinning different builds produce one conflict listing both requesters.
        Forge g(catalogue);
        const auto absent = only(g.refused({install("Sjzdj2cy", "UkT7CCGr"), install("Tlo2tahX", "disks1")}), "version_pin");
        QVERIFY(!absent.isEmpty()); QCOMPARE(absent["candidates"].toArray().size(), 2);
    }
    void connectorBridgeAdmitsOnlyExplicitFabricFiles()
    {
        const auto catalogue = blockers();
        {
            Forge f(catalogue);
            try { f.apply(f.importPack({packFile(catalogue, "1MQDrKN7"), packFile(catalogue, "ffapi1"), packFile(catalogue, "fab1")})); }
            catch (const ServiceError& e) { QFAIL(qPrintable(e.code + ": " + e.message)); }
            QJsonObject row; for (const auto& value : f.items()) if (value.toObject()["projectId"] == "fabOnly1") row = value.toObject();
            QCOMPARE(row["loaderBridge"].toString(), QString("sinytra-connector")); QCOMPARE(row["metadataState"].toString(), QString("verified"));
            bool fabricApi = false; for (const auto& value : f.items()) fabricApi = fabricApi || value.toObject()["projectId"] == "P7dR8mSH";
            QVERIFY(!fabricApi); // Fabric API is provided by Forgified Fabric API under Connector
            // Connector cannot be removed while a bridged Fabric mod stays enabled.
            QVERIFY(!only(f.refused({QJsonObject{{"action", "remove"}, {"inventoryId", f.inventoryId("u58R1TMW")}}}), "loader_bridge").isEmpty());
        }
        {
            Forge f(catalogue);
            ServiceError error{"NONE", ""};
            try { f.importPack({packFile(catalogue, "fab1")}); } catch (const ServiceError& e) { error = e; }
            QCOMPARE(error.code, QString("DEPENDENCY_CONFLICT")); QVERIFY(!only(error, "loader_bridge").isEmpty());
            // Without a package hash, plans.prepare never bridges a Fabric file into Forge.
            QCOMPARE(f.refused({install("fabOnly1", "fab1")}).code, QString("DEPENDENCY_CONFLICT"));
        }
    }
    void importsReportConflictsBeforeDownloadingThePack()
    {
        const auto catalogue = blockers(); Forge f(catalogue); QString id;
        ServiceError error{"NONE", ""};
        try { f.importPack({packFile(catalogue, "lib8133"), packFile(catalogue, "W2xPEG9I"), packFile(catalogue, "VMGmo5Si")}, {}, &id); } catch (const ServiceError& e) { error = e; }
        QCOMPARE(error.code, QString("DEPENDENCY_CONFLICT")); QCOMPARE(error.details["conflicts"].toArray().size(), 2);
        QVERIFY(!QFileInfo::exists(f.services.operationDirectory(id) + "/staging"));
        try { f.apply(f.importPack({packFile(catalogue, "lib8133"), packFile(catalogue, "W2xPEG9I"), packFile(catalogue, "VMGmo5Si")},
            {resolution("QAGBst4M", "lib8133"), resolution("ohNO6lps", QJsonValue::Null)})); }
        catch (const ServiceError& e) { QFAIL(qPrintable(e.code + ": " + e.message)); }
        QCOMPARE(f.items().size(), 3);
    }
};
QTEST_GUILESS_MAIN(DependencyResolutionTest)
#include "DependencyResolution_test.moc"
