// SPDX-License-Identifier: GPL-3.0-only
#include "CurseForgeService.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <cassert>
using namespace Awake::Core;

static QJsonObject file()
{
    return {{"id", 42}, {"modId", 12}, {"gameId", 432}, {"isAvailable", true},
        {"displayName", "Example"}, {"fileName", "example.jar"}, {"releaseType", 1},
        {"fileDate", "2024-01-01T00:00:00Z"}, {"fileLength", 3},
        {"downloadUrl", "https://edge.forgecdn.net/files/example.jar"},
        {"gameVersions", QJsonArray{"1.20.1", "Forge"}},
        {"hashes", QJsonArray{QJsonObject{{"algo", 1}, {"value", "a9993e364706816aba3e25717850c26c9cd0d89d"}}}},
        {"dependencies", QJsonArray{QJsonObject{{"modId", 99}, {"relationType", 3}}}}};
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qunsetenv("AWAKE_CORE_CURSEFORGE_AUTHORIZED"); qunsetenv("AWAKE_CORE_CURSEFORGE_API_KEY");
    assert(!CurseForgeService().available());
    QJsonObject current = file();
    bool restrictedProject = false, badPage = false; int filePages = 0; QString searchQuery;
    CurseForgeService service({}, [&](const QUrl& url, qint64) {
        if (url.host() != "api.curseforge.com") return QByteArray("abc");
        if (url.path().endsWith("/files/42")) return QJsonDocument(QJsonObject{{"data", current}}).toJson();
        if (url.path().endsWith("/files")) {
            ++filePages; const int index = QUrlQuery(url).queryItemValue("index").toInt();
            auto entry = current; if (index != 0) entry["id"] = 43;
            return QJsonDocument(QJsonObject{{"data", QJsonArray{entry}}, {"pagination", QJsonObject{{"index", badPage ? 999 : index}, {"pageSize", 50}, {"resultCount", 1}, {"totalCount", 2}}}}).toJson();
        }
        const QJsonObject project{{"id", 12}, {"gameId", 432}, {"classId", 6}, {"name", "Example"}, {"slug", "example"}, {"summary", "desc"}, {"allowModDistribution", !restrictedProject}, {"logo", QJsonObject{{"thumbnailUrl", "https://media.forgecdn.net/avatars/thumbnails/1/2/64/64/x.png"}}}};
        if (url.path().endsWith("/search")) searchQuery = url.query();
        if (url.path().endsWith("/search")) return QJsonDocument(QJsonObject{{"data", QJsonArray{project}}, {"pagination", QJsonObject{{"index", 0}, {"pageSize", 50}, {"resultCount", 1}, {"totalCount", 1}}}}).toJson();
        return QJsonDocument(QJsonObject{{"data", project}}).toJson();
    });
    assert(service.available());
    auto p = QJsonObject{{"projectId", "12"}, {"minecraftVersion", "1.20.1"}, {"loader", "forge"}, {"offset", 0}, {"limit", 50}};
    const auto found = service.search(p)["projects"].toArray();
    assert(found.size() == 1);
    // Logos live on the media CDN; results are requested most popular first.
    assert(found[0].toObject()["iconUrl"] == "https://media.forgecdn.net/avatars/thumbnails/1/2/64/64/x.png");
    assert(searchQuery.contains("sortField=2") && searchQuery.contains("sortOrder=desc"));
    assert(service.project("12").addonId.toString() == "12");
    const auto details=service.lookup("12");
    assert(details["provider"] == "curseforge" && details["projectId"] == "12" && details["title"] == "Example");
    auto v = service.version("12", "42", "1.20.1", "forge");
    assert(v.wire["dependencies"].toArray()[0].toObject()["type"] == "required");
    assert(service.versions(p)["versions"].toArray().size() == 2 && filePages == 2);
    QTemporaryDir dir; assert(dir.isValid());
    service.download(v.wire["files"].toArray()[0].toObject(), dir.path() + "/artifact");
    QFile saved(dir.path() + "/artifact"); assert(saved.open(QIODevice::ReadOnly) && saved.readAll() == "abc");
    current["downloadUrl"] = QJsonValue::Null;
    try { service.version("12", "42", "1.20.1", "forge"); assert(false); } catch (const ServiceError& e) { assert(e.code == "PROVIDER_RESTRICTED"); }
    current = file(); current["fileName"] = "../bad.jar";
    try { service.version("12", "42", "1.20.1", "forge"); assert(false); } catch (const ServiceError& e) { assert(e.code == "INTEGRITY_FAILED"); }
    current = file(); current["modId"] = 13;
    try { service.version("12", "42", "1.20.1", "forge"); assert(false); } catch (const ServiceError& e) { assert(e.code == "INTEGRITY_FAILED"); }
    current = file(); current["gameVersions"] = QJsonArray{"1.20.1", "Fabric"};
    try { service.version("12", "42", "1.20.1", "forge"); assert(false); } catch (const ServiceError& e) { assert(e.code == "DEPENDENCY_CONFLICT"); }
    current = file(); current["downloadUrl"] = "https://evil.example/file.jar";
    try { service.version("12", "42", "1.20.1", "forge"); assert(false); } catch (const ServiceError& e) { assert(e.code == "PROVIDER_RESTRICTED"); }
    current = file(); current["hashes"] = QJsonArray{QJsonObject{{"algo", 1}, {"value", "bad"}}};
    try { service.version("12", "42", "1.20.1", "forge"); assert(false); } catch (const ServiceError& e) { assert(e.code == "INTEGRITY_FAILED"); }
    current = file(); restrictedProject = true;
    try { service.version("12", "42", "1.20.1", "forge"); assert(false); } catch (const ServiceError& e) { assert(e.code == "PROVIDER_RESTRICTED"); }
    restrictedProject = false; badPage = true;
    try { service.compatibleVersions(p); assert(false); } catch (const ServiceError& e) { assert(e.code == "INTEGRITY_FAILED"); }
    auto cancelled = std::make_shared<std::atomic_bool>(true); CurseForgeService stopped(cancelled, [](const QUrl&, qint64) { return QByteArray{}; });
    try { stopped.search(p); assert(false); } catch (const ServiceError& e) { assert(e.code == "CANCELLED"); }
    return 0;
}
