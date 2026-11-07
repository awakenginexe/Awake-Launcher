// SPDX-License-Identifier: GPL-3.0-only
#include "CurseForgeService.h"
#include "awake/integration/DurableIO.h"
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>

namespace Awake::Core {
namespace {
constexpr auto api = "https://api.curseforge.com/v1";
constexpr qint64 jsonLimit = 32 * 1024 * 1024, artifactLimit = 1024LL * 1024 * 1024;
[[noreturn]] void bad(const QString& message) { throw ServiceError{"INTEGRITY_FAILED", message}; }
QString id(const QJsonValue& value) {
    const auto n = value.toDouble(-1);
    if (!value.isDouble() || n < 1 || n > 4294967295.0 || n != qint64(n)) bad("CurseForge identifier is invalid.");
    return QString::number(qint64(n));
}
QString checkedId(const QString& value) {
    if (!QRegularExpression("^[1-9][0-9]{0,9}$").match(value).hasMatch() || value.toULongLong() > 4294967295ULL)
        throw ServiceError{"INVALID_REQUEST", "CurseForge identifier is invalid."};
    return value;
}
int loaderNumber(const QString& loader) {
    if (loader == "forge") return 1; if (loader == "fabric") return 4;
    if (loader == "quilt") return 5; if (loader == "neoforge") return 6;
    throw ServiceError{"INVALID_REQUEST", "CurseForge loader is unsupported."};
}
QUrl endpoint(const QString& path, const QUrlQuery& query = {}) {
    QUrl url(QString(api) + path); url.setQuery(query); return url;
}
bool artifactHost(const QString& host) {
    return host == "edge.forgecdn.net" || host == "mediafilez.forgecdn.net";
}
bool safeUrl(const QUrl& url, bool apiRequest) {
    return url.isValid() && url.scheme() == "https" && url.userInfo().isEmpty() && url.port(-1) == -1 && !url.hasFragment()
        && (apiRequest ? url.host() == "api.curseforge.com" : artifactHost(url.host()));
}
void validatePage(const QJsonObject& raw, int offset, int requested) {
    const auto page = raw["pagination"].toObject(); const auto rows = raw["data"].toArray();
    if (!raw["data"].isArray() || !raw["pagination"].isObject() || !page["index"].isDouble() ||
        !page["resultCount"].isDouble() || !page["totalCount"].isDouble() || page["index"].toInt(-1) != offset ||
        page["resultCount"].toInt(-1) != rows.size() || rows.size() > requested || page["totalCount"].toInt(-1) < offset + rows.size())
        bad("CurseForge pagination is invalid.");
}
QJsonValue nullable(const QString& value) { return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value); }
}
CurseForgeService::CurseForgeService(std::shared_ptr<std::atomic_bool> cancel, Fetch fixture)
    : m_cancel(std::move(cancel)), m_fixture(std::move(fixture)) {}
bool CurseForgeService::available() const {
    return bool(m_fixture) || (qgetenv("AWAKE_CORE_CURSEFORGE_AUTHORIZED") == "1" && !qgetenv("AWAKE_CORE_CURSEFORGE_API_KEY").isEmpty());
}
void CurseForgeService::checkCancelled() const {
    if (m_cancel && m_cancel->load()) throw ServiceError{"CANCELLED", "The operation was cancelled."};
}
QByteArray CurseForgeService::transfer(const QUrl& url, qint64 limit, const QString& path) {
    checkCancelled();
    if (!available()) throw ServiceError{"PROVIDER_UNAVAILABLE", "CurseForge requires an authorized local API key."};
    const bool apiRequest = url.host() == "api.curseforge.com";
    if (!safeUrl(url, apiRequest) || (apiRequest && !path.isEmpty())) throw ServiceError{"PROVIDER_RESTRICTED", "CurseForge URL is not allowed."};
    QFile output(path);
    if (!path.isEmpty() && !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) throw ServiceError{"IO_ERROR", "Private download target is unavailable."};
    bool success = false;
    auto cleanup = qScopeGuard([&] { output.close(); if (!success && !path.isEmpty()) QFile::remove(path); });
    if (m_fixture) {
        auto bytes = m_fixture(url, limit); checkCancelled();
        if (bytes.size() > limit) bad("CurseForge response exceeds its size limit.");
        if (!path.isEmpty() && output.write(bytes) != bytes.size()) throw ServiceError{"IO_ERROR", "Staging write failed."};
        success = true; return path.isEmpty() ? bytes : QByteArray{};
    }
    QNetworkAccessManager network; QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Awake-Core/0.2 (+https://github.com/awakenginexe/Awake-Launcher)");
    request.setRawHeader("Accept", apiRequest ? "application/json" : "application/octet-stream");
    if (apiRequest) request.setRawHeader("x-api-key", qgetenv("AWAKE_CORE_CURSEFORGE_API_KEY"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    auto* reply = network.get(request); QEventLoop loop; QTimer poll, timeout;
    poll.setInterval(50); timeout.setSingleShot(true); timeout.start(180000);
    QByteArray bytes; qint64 received = 0; bool ioFailed = false, tooLarge = false, timedOut = false;
    const auto consume = [&] {
        const auto chunk = reply->readAll(); received += chunk.size();
        if (received > limit) { tooLarge = true; reply->abort(); return; }
        if (path.isEmpty()) bytes.append(chunk);
        else if (output.write(chunk) != chunk.size()) { ioFailed = true; reply->abort(); }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, consume);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (m_cancel && m_cancel->load()) reply->abort(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { timedOut = true; reply->abort(); });
    poll.start(); loop.exec(); consume(); checkCancelled();
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 429) throw ServiceError{"RATE_LIMITED", "CurseForge rate limit reached; retry later.", true};
    if (status == 401 || status == 403 || status == 451) throw ServiceError{"PROVIDER_RESTRICTED", "CurseForge denied this request."};
    if (status == 404) throw ServiceError{"NOT_FOUND", "CurseForge project or file was not found."};
    if (ioFailed) throw ServiceError{"IO_ERROR", "Download staging write failed."};
    if (tooLarge) bad("CurseForge response exceeds its size limit.");
    if (timedOut || reply->error() != QNetworkReply::NoError || status != 200)
        throw ServiceError{"DOWNLOAD_FAILED", "CurseForge request failed (HTTP " + QString::number(status) + ").", true};
    if (!path.isEmpty() && !output.flush()) throw ServiceError{"IO_ERROR", "Staging flush failed."};
    success = true; return bytes;
}
QJsonObject CurseForgeService::json(const QUrl& url) {
    QJsonParseError error; const auto doc = QJsonDocument::fromJson(transfer(url, jsonLimit), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) bad("CurseForge returned invalid JSON.");
    return doc.object();
}
QJsonObject CurseForgeService::projectObject(const QString& projectId) {
    const auto raw = json(endpoint("/mods/" + checkedId(projectId)))["data"].toObject();
    if (id(raw["id"]) != projectId || raw["gameId"].toInt() != 432 || raw["classId"].toInt() != 6 ||
        !raw["name"].isString() || raw["name"].toString().isEmpty() || !raw["slug"].isString()) bad("CurseForge project metadata is invalid.");
    return raw;
}
ModPlatform::IndexedPack CurseForgeService::project(const QString& projectId) {
    const auto raw = projectObject(projectId); ModPlatform::IndexedPack pack;
    pack.provider = ModPlatform::ResourceProvider::FLAME; pack.addonId = projectId;
    pack.name = raw["name"].toString(); pack.slug = raw["slug"].toString(); pack.description = raw["summary"].toString();
    pack.resourceType = ModPlatform::ResourceType::Mod;
    const auto website = QUrl(raw["links"].toObject()["websiteUrl"].toString());
    if (website.scheme() == "https" && website.userInfo().isEmpty() && website.port(-1) == -1 && !website.host().isEmpty()) pack.websiteUrl = website.toString();
    const auto icon = QUrl(raw["logo"].toObject()["thumbnailUrl"].toString());
    if (safeUrl(icon, false)) pack.logoUrl = icon.toString();
    return pack;
}
QJsonObject CurseForgeService::search(const QJsonObject& params) {
    const int offset = params["offset"].toInt(), requested = params["limit"].toInt(50);
    if (offset < 0 || requested < 1 || offset >= 10000) throw ServiceError{"INVALID_REQUEST", "CurseForge search page is invalid."};
    const int count = qMin(qMin(requested, 50), 10000 - offset);
    QUrlQuery query; query.addQueryItem("gameId", "432"); query.addQueryItem("classId", "6");
    query.addQueryItem("searchFilter", params["query"].toString()); query.addQueryItem("gameVersion", params["minecraftVersion"].toString());
    query.addQueryItem("modLoaderType", QString::number(loaderNumber(params["loader"].toString())));
    query.addQueryItem("index", QString::number(offset)); query.addQueryItem("pageSize", QString::number(count));
    const auto raw = json(endpoint("/mods/search", query)); validatePage(raw, offset, count); QJsonArray projects;
    for (const auto& value : raw["data"].toArray()) {
        const auto obj = value.toObject(); const auto projectId = id(obj["id"]);
        if (obj["gameId"].toInt() != 432 || obj["classId"].toInt() != 6 || !obj["name"].isString() || !obj["slug"].isString()) bad("CurseForge search entry is invalid.");
        const auto icon = QUrl(obj["logo"].toObject()["thumbnailUrl"].toString());
        projects.append(QJsonObject{{"provider", "curseforge"}, {"projectId", projectId}, {"slug", obj["slug"]}, {"title", obj["name"]},
            {"description", obj["summary"].toString()}, {"iconUrl", safeUrl(icon, false) ? QJsonValue(icon.toString()) : QJsonValue(QJsonValue::Null)},
            {"gameVersions", QJsonArray{params["minecraftVersion"]}}, {"loaders", QJsonArray{params["loader"]}}});
    }
    const int total = raw["pagination"].toObject()["totalCount"].toInt(); const int next = offset + projects.size();
    return {{"projects", projects}, {"total", total}, {"nextOffset", next < total && next < 10000 && !projects.isEmpty() ? QJsonValue(next) : QJsonValue(QJsonValue::Null)}};
}
ResolvedVersion CurseForgeService::parseVersion(const QJsonObject& raw, const QString& projectId, const QString& game, const QString& loader) {
    const auto fileId = id(raw["id"]);
    if (id(raw["modId"]) != projectId || raw["gameId"].toInt() != 432 || !raw["gameVersions"].isArray() ||
        !raw["fileName"].isString() || !raw["displayName"].isString() || !raw["hashes"].isArray() || !raw["dependencies"].isArray())
        bad("CurseForge file metadata is invalid.");
    const auto filename = raw["fileName"].toString(); const QUrl url(raw["downloadUrl"].toString());
    if (filename.isEmpty() || filename.size() > 255 || QFileInfo(filename).fileName() != filename ||
        filename.contains(QRegularExpression("[\\\\/:*?\"<>|\\x00-\\x1f]")) || filename.endsWith('.') || filename.endsWith(' ') ||
        !filename.endsWith(".jar", Qt::CaseInsensitive)) bad("CurseForge file name is unsafe.");
    if (raw["isAvailable"] == false || !safeUrl(url, false)) throw ServiceError{"PROVIDER_RESTRICTED", "CurseForge file is unavailable for direct download."};
    bool gameFound = false, loaderFound = false; QJsonArray games, loaders;
    const auto allGames = raw["gameVersions"].toArray();
    for (const auto& value : allGames) {
        if (!value.isString()) bad("CurseForge game version is invalid.");
        const auto s = value.toString();
        if (s == game) gameFound = true;
        if (s.compare(loader, Qt::CaseInsensitive) == 0) loaderFound = true;
        const auto lower = s.toLower();
        if (QStringList{"forge", "fabric", "quilt", "neoforge"}.contains(lower)) loaders.append(lower);
        else if (s.contains('.')) games.append(s);
    }
    if (!gameFound || !loaderFound) throw ServiceError{"DEPENDENCY_CONFLICT", "CurseForge file does not support this Minecraft version and loader."};
    const auto size = raw["fileLength"].toDouble(-1);
    if (!raw["fileLength"].isDouble() || size <= 0 || size > artifactLimit || size != qint64(size)) bad("CurseForge artifact size is invalid.");
    QString sha1;
    for (const auto& value : raw["hashes"].toArray()) {
        const auto hash = value.toObject();
        if (hash["algo"].toInt() == 1) {
            const auto candidate = hash["value"].toString().toLower();
            if (!QRegularExpression("^[0-9a-f]{40}$").match(candidate).hasMatch() || (!sha1.isEmpty() && sha1 != candidate)) bad("CurseForge SHA-1 is invalid.");
            sha1 = candidate;
        }
    }
    if (sha1.isEmpty()) bad("CurseForge artifact lacks a supported integrity hash.");
    const int release = raw["releaseType"].toInt();
    if (release < 1 || release > 3) bad("CurseForge release type is invalid.");
    const QString releaseType = release == 1 ? "release" : release == 2 ? "beta" : "alpha";
    const auto published = QDateTime::fromString(raw["fileDate"].toString(), Qt::ISODateWithMs);
    if (!published.isValid()) bad("CurseForge publication date is invalid.");
    QJsonArray dependencies; QList<ModPlatform::Dependency> nativeDependencies;
    for (const auto& value : raw["dependencies"].toArray()) {
        const auto dep = value.toObject(); const auto depId = id(dep["modId"]); const int relation = dep["relationType"].toInt();
        if (relation < 1 || relation > 6) bad("CurseForge dependency type is invalid.");
        const QString type = relation == 1 ? "embedded" : relation == 2 ? "optional" : relation == 3 ? "required" :
            relation == 4 ? "tool" : relation == 5 ? "incompatible" : "include";
        dependencies.append(QJsonObject{{"projectId", depId}, {"versionId", QJsonValue(QJsonValue::Null)}, {"type", type}});
        ModPlatform::Dependency nativeDep; nativeDep.addonId = depId; nativeDep.type = ModPlatform::DependencyType::fromString(type); nativeDependencies.append(nativeDep);
    }
    const auto display = raw["displayName"].toString(); if (display.isEmpty()) bad("CurseForge file name is empty.");
    const QJsonObject artifact{{"fileName", filename}, {"size", QString::number(qint64(size))}, {"sha1", sha1}, {"url", url.toString()}};
    const QJsonObject wire{{"provider", "curseforge"}, {"projectId", projectId}, {"versionId", fileId}, {"versionNumber", display},
        {"name", display}, {"releaseType", releaseType}, {"gameVersions", games}, {"loaders", loaders},
        {"files", QJsonArray{artifact}}, {"dependencies", dependencies}};
    ModPlatform::IndexedVersion native; native.addonId = projectId; native.fileId = fileId; native.version = display; native.versionNumber = display;
    native.versionType = ModPlatform::IndexedVersionType::fromString(releaseType); native.mcVersion = {game}; native.downloadUrl = url.toString();
    native.date = raw["fileDate"].toString(); native.fileName = filename; native.hashType = "sha1"; native.hash = sha1; native.dependencies = nativeDependencies;
    native.loaders = ModPlatform::getModLoaderFromString(loader);
    return {wire, native, published};
}
ResolvedVersion CurseForgeService::version(const QString& projectId, const QString& fileId, const QString& game, const QString& loader) {
    const auto project = projectObject(projectId);
    if (project["allowModDistribution"] == false) throw ServiceError{"PROVIDER_RESTRICTED", "CurseForge project does not allow third-party distribution."};
    const auto raw = json(endpoint("/mods/" + checkedId(projectId) + "/files/" + checkedId(fileId)))["data"].toObject();
    if (id(raw["id"]) != fileId) bad("CurseForge returned a different file.");
    return parseVersion(raw, projectId, game, loader);
}
QList<ResolvedVersion> CurseForgeService::compatibleVersions(const QJsonObject& params) {
    const auto projectId = checkedId(params["projectId"].toString()), game = params["minecraftVersion"].toString(), loader = params["loader"].toString();
    const auto project = projectObject(projectId);
    if (project["allowModDistribution"] == false) throw ServiceError{"PROVIDER_RESTRICTED", "CurseForge project does not allow third-party distribution."};
    const auto loaderId = loaderNumber(loader); QList<ResolvedVersion> all;
    for (int offset = 0; offset < 10000;) {
        checkCancelled(); QUrlQuery query; query.addQueryItem("gameVersion", game); query.addQueryItem("modLoaderType", QString::number(loaderId));
        query.addQueryItem("index", QString::number(offset)); query.addQueryItem("pageSize", "50");
        const auto raw = json(endpoint("/mods/" + projectId + "/files", query)); validatePage(raw, offset, 50);
        const auto rows = raw["data"].toArray();
        for (const auto& row : rows) {
            const auto obj = row.toObject(); const auto versions = obj["gameVersions"].toArray();
            bool matches = false; for (const auto& v : versions) if (v.toString().compare(loader, Qt::CaseInsensitive) == 0) matches = true;
            if (versions.contains(game) && matches) {
                try { all.append(parseVersion(obj, projectId, game, loader)); }
                catch (const ServiceError& e) { if (e.code != "PROVIDER_RESTRICTED") throw; }
            }
        }
        offset += rows.size(); const auto total = raw["pagination"].toObject()["totalCount"].toInt();
        if (offset >= total) break;
        if (rows.isEmpty() || offset >= 10000) throw ServiceError{"LIMIT_EXCEEDED", "CurseForge version list exceeds pagination limits."};
    }
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.publishedAt > b.publishedAt; });
    return all;
}
QJsonObject CurseForgeService::versions(const QJsonObject& params) {
    const int offset = params["offset"].toInt(), limit = params["limit"].toInt(50);
    if (offset < 0 || limit < 1) throw ServiceError{"INVALID_REQUEST", "CurseForge version page is invalid."};
    const auto all = compatibleVersions(params); QJsonArray rows;
    for (int i = offset; i < all.size() && i - offset < limit; ++i) rows.append(all[i].wire);
    const auto next = offset + rows.size();
    return {{"versions", rows}, {"total", all.size()}, {"nextOffset", next < all.size() ? QJsonValue(next) : QJsonValue(QJsonValue::Null)}};
}
QJsonObject CurseForgeService::lookup(const QString& id) { return projectMetadata(project(id)); }
void CurseForgeService::download(const QJsonObject& artifact, const QString& destination) {
    bool ok = false; const auto size = artifact["size"].toString().toLongLong(&ok); const auto expected = artifact["sha1"].toString();
    if (!ok || size <= 0 || size > artifactLimit || !QRegularExpression("^[0-9a-f]{40}$").match(expected).hasMatch()) bad("CurseForge artifact integrity data is invalid.");
    const auto part = destination + ".part"; auto cleanup = qScopeGuard([&] { QFile::remove(part); });
    transfer(QUrl(artifact["url"].toString()), size, part); QFile input(part); QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!input.open(QIODevice::ReadOnly) || input.size() != size) bad("CurseForge artifact size does not match metadata.");
    while (!input.atEnd()) { checkCancelled(); const auto bytes = input.read(1024 * 1024);
        if (input.error() != QFileDevice::NoError) throw ServiceError{"IO_ERROR", "Cannot verify staged artifact."}; hash.addData(bytes); }
    input.close(); if (QString::fromLatin1(hash.result().toHex()) != expected) bad("CurseForge artifact hash does not match metadata.");
    QString error; if (!Awake::Integration::DurableIO::copy(part, destination, &error)) throw ServiceError{"IO_ERROR", error};
}
}
