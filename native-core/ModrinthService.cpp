// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthService.h"
#include <algorithm>
#include "Contract.h"
#include "modplatform/modrinth/ModrinthMetadata.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "awake/integration/DurableIO.h"
#include <QCryptographicHash>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <QTimer>
#include <QMutex>
#include <QThread>
#include <QRegularExpression>
namespace Awake::Core {
namespace {
constexpr auto api = "https://api.modrinth.com/v2";
constexpr qint64 jsonLimit = 32 * 1024 * 1024, artifactLimit = 1024 * 1024 * 1024;
[[noreturn]] void bad(const QString& message) { throw ServiceError{"INTEGRITY_FAILED", message}; }
QString segment(const QString& value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); }
QJsonObject page(const QString& key, const QJsonArray& all, int offset, int limit)
{
    QJsonArray items; for (int i = offset; i < all.size() && i < offset + limit; ++i) items.append(all[i]);
    return {{key, items}, {"total", all.size()}, {"nextOffset", offset + items.size() < all.size() ? QJsonValue(offset + items.size()) : QJsonValue(QJsonValue::Null)}};
}
bool compatible(const QJsonObject& raw, const QString& game, const QString& loader)
{ return raw["game_versions"].toArray().contains(game) && raw["loaders"].toArray().contains(loader); }
}
ModrinthService::ModrinthService(std::shared_ptr<std::atomic_bool> cancel, Fetch fixture)
    : m_cancel(std::move(cancel)), m_fixture(std::move(fixture)) {}
void ModrinthService::checkCancelled() const
{ if (m_cancel && m_cancel->load()) throw ServiceError{"CANCELLED", "The operation was cancelled."}; }
QByteArray ModrinthService::transfer(const QUrl& url, qint64 limit, const QString& path)
{
    checkCancelled();
    if (url.scheme() != "https" || !url.userInfo().isEmpty() || url.port(-1) != -1 || url.hasFragment() ||
        !QStringList{"api.modrinth.com", "cdn.modrinth.com", "meta.fabricmc.net", "piston-meta.mojang.com", "meta.prismlauncher.org"}.contains(url.host()))
        throw ServiceError{"PROVIDER_RESTRICTED", "Provider URL is outside the supported HTTPS services."};
    QFile output(path);
    if (!path.isEmpty() && !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) throw ServiceError{"IO_ERROR", "Private download target is unavailable."};
    bool success = false;
    auto cleanup = qScopeGuard([&] { output.close(); if (!success && !path.isEmpty()) QFile::remove(path); });
    if (m_fixture) {
        const auto bytes = m_fixture(url, limit); checkCancelled();
        if (bytes.size() > limit) bad("Provider response exceeds its size limit.");
        if (!path.isEmpty() && output.write(bytes) != bytes.size()) throw ServiceError{"IO_ERROR", "Staging write failed."};
        success = true; return path.isEmpty() ? bytes : QByteArray{};
    }
    if (url.host() == "api.modrinth.com") {
        static QMutex budget;
        static const auto clock = [] { QElapsedTimer value; value.start(); return value; }();
        static qint64 previous = -250;
        for (;;) {
            checkCancelled(); budget.lock(); const auto now = clock.elapsed();
            if (now - previous >= 250) { previous = now; budget.unlock(); break; }
            budget.unlock(); QThread::msleep(25);
        }
    }
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Awake-Core/0.1 (+https://github.com/awakenginexe/Awake-Launcher)");
    request.setRawHeader("Accept", path.isEmpty() ? "application/json" : "application/octet-stream");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    auto* reply = network.get(request);
    QEventLoop loop; QTimer poll, timeout; poll.setInterval(50); timeout.setSingleShot(true); timeout.start(180000);
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
    if (status == 429) throw ServiceError{"RATE_LIMITED", "Modrinth rate limit reached; retry later.", true};
    if (status == 403 || status == 451) throw ServiceError{"PROVIDER_RESTRICTED", "Provider denied this request."};
    if (ioFailed) throw ServiceError{"IO_ERROR", "Download staging write failed."};
    if (tooLarge) bad("Provider response exceeds its size limit.");
    if (timedOut || reply->error() != QNetworkReply::NoError || status != 200)
        throw ServiceError{"DOWNLOAD_FAILED", "Provider request failed (HTTP " + QString::number(status) + ").", true};
    if (!path.isEmpty() && !output.flush()) throw ServiceError{"IO_ERROR", "Staging flush failed."};
    success = true; return bytes;
}
QJsonDocument ModrinthService::json(const QUrl& url)
{
    QJsonParseError error; const auto doc = QJsonDocument::fromJson(transfer(url, jsonLimit), &error);
    if (error.error != QJsonParseError::NoError || doc.isNull()) bad("Provider returned invalid JSON.");
    return doc;
}
QJsonObject ModrinthService::catalog(const QJsonObject& params)
{
    const auto manifest = json(QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json")).object();
    QJsonArray games, loaders;
    for (const auto& value : manifest["versions"].toArray()) games.append(value.toObject()["id"]);
    if (games.isEmpty() || games.size() > 10000) bad("Minecraft version catalog is invalid.");
    const auto game = params["minecraftVersion"].toString();
    if (!game.isEmpty() && !games.contains(game)) throw ServiceError{"INVALID_REQUEST", "Unknown Minecraft version."};
    if (params["loader"] == "fabric") {
        const auto suffix = game.isEmpty() ? QString{} : '/' + segment(game);
        const auto versions = json(QUrl("https://meta.fabricmc.net/v2/versions/loader" + suffix)).array();
        for (const auto& value : versions) {
            const auto object = value.toObject();
            const auto version = (game.isEmpty() ? object : object["loader"].toObject())["version"].toString();
            if (!version.isEmpty() && !loaders.contains(version)) loaders.append(version);
        }
        if (loaders.size() > 2000) bad("Fabric version catalog is too large.");
    }
    const auto loader = params["loader"].toString();
    const QMap<QString, QString> ids{{"forge", "net.minecraftforge"}, {"neoforge", "net.neoforged"}, {"quilt", "org.quiltmc.quilt-loader"}};
    if (ids.contains(loader)) {
        const auto index = [&](const QString& uid) {
            const auto document = json(QUrl("https://meta.prismlauncher.org/v1/" + uid + "/index.json"));
            const auto object = document.object();
            if (!document.isObject() || object["formatVersion"] != 1 || object["uid"] != uid || !object["versions"].isArray())
                bad("Loader catalog is invalid.");
            const auto values = object["versions"].toArray();
            if (values.isEmpty() || values.size() > 20000) bad("Loader catalog is invalid.");
            return values;
        };
        bool intermediary = game.isEmpty();
        if (loader == "quilt" && !game.isEmpty()) {
            for (const auto& value : index("net.fabricmc.intermediary")) {
                const auto item = value.toObject();
                if (item["version"] != game) continue;
                for (const auto& requirement : item["requires"].toArray()) {
                    const auto req = requirement.toObject();
                    if (req["uid"] == "net.minecraft" && req["equals"] == game) intermediary = true;
                }
            }
        }
        for (const auto& value : index(ids.value(loader))) {
            const auto item = value.toObject(); const auto version = item["version"].toString();
            if (version.isEmpty() || !item["requires"].isArray()) bad("Loader version metadata is invalid.");
            bool match = game.isEmpty();
            for (const auto& requirement : item["requires"].toArray()) {
                const auto req = requirement.toObject();
                if ((loader == "quilt" && intermediary && req["uid"] == "net.fabricmc.intermediary") ||
                    (loader != "quilt" && req["uid"] == "net.minecraft" && req["equals"] == game)) match = true;
            }
            if (match && !loaders.contains(version)) loaders.append(version);
        }
        if (loaders.size() > 20000) bad("Loader catalog is too large.");
    }
    return {{"minecraftVersions", games}, {"loaderVersions", loaders}};
}
QJsonObject ModrinthService::search(const QJsonObject& params)
{
    const QJsonArray facets{QJsonArray{"project_type:mod"}, QJsonArray{"versions:" + params["minecraftVersion"].toString()},
        QJsonArray{"categories:" + params["loader"].toString()}};
    const int offset = params["offset"].toInt(), limit = params["limit"].toInt(50);
    const auto raw = json(Modrinth::searchUrl(api, params["query"].toString(), QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)), offset, qMin(limit, 100))).object();
    if (!raw["hits"].isArray() || !raw["total_hits"].isDouble()) bad("Modrinth search response is invalid.");
    QJsonArray projects;
    for (const auto& value : raw["hits"].toArray()) {
        ModPlatform::IndexedPack pack;
        if (!Modrinth::loadIndexedPack(pack, value.toObject())) bad("Modrinth project metadata is invalid.");
        const auto object = value.toObject();
        projects.append(QJsonObject{{"projectId", pack.addonId.toString()}, {"slug", pack.slug}, {"title", pack.name}, {"description", pack.description},
            {"iconUrl", pack.logoUrl.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(pack.logoUrl)}, {"gameVersions", object["versions"].toArray()},
            {"loaders", QJsonArray::fromStringList([&] { QStringList list; for (const auto& c : object["categories"].toArray())
                if (QStringList{"fabric", "forge", "neoforge", "quilt"}.contains(c.toString())) list.append(c.toString()); return list; }())}});
    }
    const int total = raw["total_hits"].toInt();
    return {{"projects", projects}, {"total", total}, {"nextOffset", offset + projects.size() < total ? QJsonValue(offset + projects.size()) : QJsonValue(QJsonValue::Null)}};
}
ResolvedVersion ModrinthService::parseVersion(const QJsonObject& raw, const QString& minecraft, const QString& loader, const QString& artifactSha512)
{
    if (!compatible(raw, minecraft, loader)) throw ServiceError{"DEPENDENCY_CONFLICT", "Selected version does not support this Minecraft version and loader."};
    auto native = Modrinth::loadIndexedPackVersion(raw);
    if (!native || native->fileName.isEmpty()) bad("Modrinth version metadata is invalid.");
    if (!artifactSha512.isEmpty()) {
        if (!QRegularExpression("^[a-f0-9]{128}$").match(artifactSha512).hasMatch()) throw ServiceError{"INVALID_REQUEST", "Invalid selected artifact hash."};
        QJsonObject artifact;
        for (const auto& value : raw["files"].toArray()) if (value.toObject()["hashes"].toObject()["sha512"] == artifactSha512) {
            if (!artifact.isEmpty()) bad("Selected artifact hash is ambiguous.");
            artifact = value.toObject();
        }
        if (artifact.isEmpty()) bad("Selected version does not contain the requested artifact.");
        native->fileName = artifact["filename"].toString(); native->downloadUrl = artifact["url"].toString();
        native->hashType = "sha512"; native->hash = artifactSha512;
    }
    QJsonArray files, dependencies;
    for (const auto& value : raw["files"].toArray()) {
        const auto file = value.toObject(), hashes = file["hashes"].toObject();
        const auto size = file["size"].toDouble(-1);
        if (size < 0 || size > artifactLimit || size != qint64(size)) bad("Artifact size is invalid.");
        if (file["filename"] == native->fileName && file["url"] == native->downloadUrl) {
            files.append(QJsonObject{{"fileName", file["filename"]}, {"size", QString::number(qint64(size))},
                {"sha512", hashes["sha512"]}, {"sha1", hashes["sha1"]}, {"url", file["url"]}});
            break;
        }
    }
    if (files.size() != 1) bad("Selected filename was sanitized or no primary artifact is available.");
    for (const auto& value : raw["dependencies"].toArray()) {
        const auto dep = value.toObject();
        dependencies.append(QJsonObject{{"projectId", dep["project_id"].isString() ? dep["project_id"] : QJsonValue(QJsonValue::Null)},
            {"versionId", dep["version_id"].isString() ? dep["version_id"] : QJsonValue(QJsonValue::Null)}, {"type", dep["dependency_type"]}});
    }
    QJsonObject wire{{"projectId", native->addonId.toString()}, {"versionId", native->fileId.toString()}, {"versionNumber", native->versionNumber},
        {"name", native->version}, {"releaseType", native->versionType.toModrinth()}, {"gameVersions", raw["game_versions"]},
        {"loaders", raw["loaders"]}, {"files", files}, {"dependencies", dependencies}};
    if (!Contract().valid("version", wire)) bad("Provider version exceeds the supported contract or lacks required integrity data.");
    const auto published = QDateTime::fromString(native->date, Qt::ISODateWithMs);
    if (!published.isValid()) bad("Version publication time is invalid.");
    return {wire, *native, published};
}
ResolvedVersion ModrinthService::versionByHash(const QString& sha512, const QString& minecraft, const QString& loader)
{
    if (!QRegularExpression("^[a-f0-9]{128}$").match(sha512).hasMatch()) throw ServiceError{"INVALID_REQUEST", "Invalid package artifact hash."};
    return parseVersion(json(QUrl(QString(api) + "/version_file/" + sha512 + "?algorithm=sha512")).object(), minecraft, loader, sha512);
}
ResolvedVersion ModrinthService::version(const QString& id, const QString& game, const QString& loader, const QString& artifactSha512)
{ return parseVersion(json(QUrl(QString(api) + "/version/" + segment(id))).object(), game, loader, artifactSha512); }
QList<ResolvedVersion> ModrinthService::compatibleVersions(const QJsonObject& params)
{
    const auto game = params["minecraftVersion"].toString(), loader = params["loader"].toString();
    const auto doc = json(Modrinth::versionsUrl(api, params["projectId"].toString(), {game}, {loader}, false));
    if (!doc.isArray() || doc.array().size() > 100000) bad("Modrinth version list is invalid.");
    QList<ResolvedVersion> all;
    for (const auto& value : doc.array()) if (compatible(value.toObject(), game, loader)) all.append(parseVersion(value.toObject(), game, loader));
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.publishedAt > b.publishedAt; });
    return all;
}
QJsonObject ModrinthService::versions(const QJsonObject& params)
{
    QJsonArray all;
    for (const auto& version : compatibleVersions(params)) all.append(version.wire);
    return page("versions", all, params["offset"].toInt(), params["limit"].toInt(50));
}
ModPlatform::IndexedPack ModrinthService::project(const QString& id)
{
    ModPlatform::IndexedPack pack;
    if (!Modrinth::loadIndexedPack(pack, json(QUrl(QString(api) + "/project/" + segment(id))).object()) || pack.resourceType != ModPlatform::ResourceType::Mod)
        bad("Only supported Modrinth mod projects can be installed.");
    return pack;
}
void ModrinthService::download(const QJsonObject& artifact, const QString& destination)
{
    const auto size = artifact["size"].toString().toLongLong();
    if (size <= 0 || size > artifactLimit) bad("Artifact size is unsupported.");
    const auto part = destination + ".part";
    auto cleanup = qScopeGuard([&] { QFile::remove(part); });
    transfer(QUrl(artifact["url"].toString()), size, part);
    QFile input(part); QCryptographicHash sha512(QCryptographicHash::Sha512), sha1(QCryptographicHash::Sha1);
    if (!input.open(QIODevice::ReadOnly) || input.size() != size) bad("Artifact size does not match provider metadata.");
    while (!input.atEnd()) { checkCancelled(); const auto bytes = input.read(1024 * 1024);
        if (input.error() != QFileDevice::NoError) throw ServiceError{"IO_ERROR", "Cannot verify staged artifact."}; sha512.addData(bytes); sha1.addData(bytes); }
    input.close();
    if (QString::fromLatin1(sha512.result().toHex()) != artifact["sha512"].toString() ||
        QString::fromLatin1(sha1.result().toHex()) != artifact["sha1"].toString()) bad("Artifact hashes do not match provider metadata.");
    QString error;
    if (!Awake::Integration::DurableIO::copy(part, destination, &error)) throw ServiceError{"IO_ERROR", error};
}
}
