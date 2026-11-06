// SPDX-FileCopyrightText: 2022-2023 flowln <flowlnlnln@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only
#include "ModrinthMetadata.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include <array>
namespace Modrinth {
namespace {
const auto types = std::array{
    std::pair{ModPlatform::ResourceType::Mod, "mod"}, std::pair{ModPlatform::ResourceType::ResourcePack, "resourcepack"},
    std::pair{ModPlatform::ResourceType::ShaderPack, "shader"}, std::pair{ModPlatform::ResourceType::DataPack, "datapack"},
    std::pair{ModPlatform::ResourceType::Modpack, "modpack"}};
QString array(const QStringList& list) { return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact)); }
}
QString authorUrl(const QString& name) { return "https://modrinth.com/user/" + name; }
QString minecraftVersion(QString version)
{
    const bool pre = version.contains("-pre");
    if (pre) version.replace("-pre", " Pre-Release ");
    version.replace('-', ' ');
    if (pre) version.replace(" Pre Release ", " Pre-Release ");
    return version;
}
ModPlatform::ResourceType resourceType(const QString& type)
{
    for (const auto& [key, value] : types) if (value == type) return key;
    return ModPlatform::ResourceType::Unknown;
}
QString resourceParameter(ModPlatform::ResourceType type)
{
    for (const auto& [key, value] : types) if (key == type) return value;
    return {};
}
QUrl searchUrl(const QString& base, const QString& query, const QString& facets, int offset, int limit, const QString& order)
{
    QUrl url(base + "/search"); QUrlQuery args;
    args.addQueryItem("offset", QString::number(offset)); args.addQueryItem("limit", QString::number(limit));
    args.addQueryItem("query", query); args.addQueryItem("index", order); args.addQueryItem("facets", facets); url.setQuery(args);
    return url;
}
QUrl versionsUrl(const QString& base, const QString& project, const QStringList& games, const QStringList& loaders, bool changelog)
{
    QUrl url(base + "/project/" + project + "/version"); QUrlQuery args;
    if (!games.isEmpty()) args.addQueryItem("game_versions", array(games));
    if (!loaders.isEmpty()) args.addQueryItem("loaders", array(loaders));
    args.addQueryItem("include_changelog", changelog ? "true" : "false"); url.setQuery(args); return url;
}
}
