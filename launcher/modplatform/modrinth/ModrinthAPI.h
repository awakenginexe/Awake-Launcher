// SPDX-FileCopyrightText: 2022-2023 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include "ModrinthMetadata.h"

#include "BuildConfig.h"
#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <utility>

class ModrinthAPI final : public ResourceAPI {
   public:
    static const ModrinthAPI& get()
    {
        static const ModrinthAPI s_instance;
        return s_instance;
    }

    static std::pair<Task::Ptr, QByteArray*> currentVersion(const QString& hash, const QString& hashFormat);

    static std::pair<Task::Ptr, QByteArray*> currentVersions(const QStringList& hashes, const QString& hashFormat);

    std::pair<Task::Ptr, QByteArray*> latestVersion(
        const QString& hash,
        const QString& hashFormat,
        std::optional<std::vector<Version>> mcVersions,
        std::optional<ModPlatform::ModLoaderTypes> loaders,
        std::optional<std::vector<ModPlatform::IndexedVersionType>> releaseTypes = std::nullopt) const;

    std::pair<Task::Ptr, QByteArray*> latestVersions(
        const QStringList& hashes,
        const QString& hashFormat,
        std::optional<std::vector<Version>> mcVersions,
        std::optional<ModPlatform::ModLoaderTypes> loaders,
        std::optional<std::vector<ModPlatform::IndexedVersionType>> releaseTypes = std::nullopt) const;

    std::pair<Task::Ptr, QByteArray*> getProjects(QStringList addonIds) const override;

    static QString getModpackIdFromUrl(const QUrl& url);

    std::pair<Task::Ptr, QByteArray*> getModCategories() const override;
    static QList<ModPlatform::Category> loadCategories(const QByteArray& response, const QString& projectType);
    QList<ModPlatform::Category> loadModCategories(const QByteArray& response) const override;

   public:
    auto getSortingMethods() const -> QList<ResourceAPI::SortingMethod> override;

    static auto getAuthorURL(const QString& name) -> QString { return Modrinth::authorUrl(name); };

    static auto getModLoaderStrings(const ModPlatform::ModLoaderTypes types) -> QStringList
    {
        QStringList l;
        for (auto loader : { ModPlatform::NeoForge, ModPlatform::Forge, ModPlatform::Fabric, ModPlatform::Quilt, ModPlatform::LiteLoader,
                             ModPlatform::DataPack, ModPlatform::Babric, ModPlatform::BTA, ModPlatform::LegacyFabric, ModPlatform::Ornithe,
                             ModPlatform::Rift }) {
            if ((types & loader) != 0U) {
                l << getModLoaderAsString(loader);
            }
        }
        return l;
    }

    static auto getModLoaderFilters(ModPlatform::ModLoaderTypes types) -> QString
    {
        QStringList l;
        for (const auto& loader : getModLoaderStrings(types)) {
            l << QString("\"categories:%1\"").arg(loader);
        }
        return l.join(',');
    }

    static QString getSideFilters(ModPlatform::SideType side)
    {
        switch (side.value()) {
            case ModPlatform::SideType::ClientSide:
                return {
                    R"("environment:client_only","environment:client_only_server_optional","environment:singleplayer_only","environment:client_or_server","environment:client_or_server_prefers_both")"
                };
            case ModPlatform::SideTypeValue::ServerSide:
                return {
                    R"("environment:server_only","environment:server_only_client_optional","environment:dedicated_server_only","environment:client_or_server","environment:client_or_server_prefers_both")"
                };
            case ModPlatform::SideTypeValue::UniversalSide:
                return { R"("environment:client_and_server","client_or_server_prefers_both")" };
            case ModPlatform::SideTypeValue::NoSide:
            // fallthrough
            default:
                return {};
        }
    }

    static QString mapMCVersionFromModrinth(QString v)
    {
        return Modrinth::minecraftVersion(std::move(v));
    }
    static ModPlatform::ResourceType getResourceType(const QString& param);

   private:
    static QString resourceTypeParameter(ModPlatform::ResourceType type);

    static QString createFacets(const SearchArgs& args)
    {
        QStringList facetsList;

        if (args.loaders.has_value() && args.loaders.value() != 0) {
            facetsList.append(QString("[%1]").arg(getModLoaderFilters(args.loaders.value())));
        }
        if (args.versions.has_value() && !args.versions.value().empty()) {
            facetsList.append(QString("[%1]").arg(getGameVersionsArray(args.versions.value())));
        }
        if (args.side.has_value()) {
            auto side = getSideFilters(args.side.value());
            if (!side.isEmpty()) {
                facetsList.append(QString("[%1]").arg(side));
            }
        }
        if (args.categoryIds.has_value() && !args.categoryIds->empty()) {
            for (const auto& category : args.categoryIds.value()) {
                facetsList.append(QString(R"(["categories:%1"])").arg(category));
            }
        }
        if (!args.excludeDisclosureTypes.empty()) {
            for (const auto& d : args.excludeDisclosureTypes) {
                facetsList.append(QString("[\"disclosure_types!=%1\"]").arg(d.toString()));
            }
        }
        if (args.openSource) {
            facetsList.append("[\"open_source:true\"]");
        }

        facetsList.append(QString("[\"project_type:%1\"]").arg(resourceTypeParameter(args.type)));

        return QString("[%1]").arg(facetsList.join(','));
    }

   public:
    auto getSearchURL(const SearchArgs& args) const -> std::optional<QString> override
    {
        if (args.loaders.has_value() && args.loaders.value() != 0) {
            if (!validateModLoaders(args.loaders.value())) {
                qWarning() << "Modrinth - or our interface - does not support any the provided mod loaders!";
                return {};
            }
        }

        return Modrinth::searchUrl(BuildConfig.MODRINTH_PROD_URL, args.search.value_or(""), createFacets(args), args.offset, 25,
            args.sorting.has_value() ? args.sorting->name : "relevance").toString(QUrl::FullyEncoded);
    };

    auto getInfoURL(const QString& id) const -> std::optional<QString> override
    {
        return BuildConfig.MODRINTH_PROD_URL + "/project/" + id;
    };

    static auto getMultipleModInfoURL(const QStringList& ids) -> QString
    {
        return BuildConfig.MODRINTH_PROD_URL + QString("/projects?ids=[\"%1\"]").arg(ids.join("\",\""));
    };

    auto getVersionsURL(const VersionSearchArgs& args) const -> std::optional<QString> override
    {
        QStringList games;
        if (args.mcVersions) for (const auto& game : *args.mcVersions) games.append(mapMCVersionToModrinth(game));
        return Modrinth::versionsUrl(BuildConfig.MODRINTH_PROD_URL, args.pack->addonId.toString(), games,
            args.loaders ? getModLoaderStrings(*args.loaders) : QStringList{}, args.includeChangelog).toString(QUrl::FullyEncoded);
    };

    static QString getGameVersionsArray(const std::vector<Version>& mcVersions)
    {
        QString s;
        for (const auto& ver : mcVersions) {
            s += QString(R"("versions:%1",)").arg(mapMCVersionToModrinth(ver));
        }
        s.remove(s.length() - 1, 1);  // remove last comma
        return s.isEmpty() ? QString() : s;
    }

    static auto validateModLoaders(ModPlatform::ModLoaderTypes loaders) -> bool
    {
        return (loaders & (ModPlatform::NeoForge | ModPlatform::Forge | ModPlatform::Fabric | ModPlatform::Quilt | ModPlatform::LiteLoader |
                           ModPlatform::DataPack | ModPlatform::Babric | ModPlatform::BTA | ModPlatform::LegacyFabric |
                           ModPlatform::Ornithe | ModPlatform::Rift)) != 0;
    }

    std::optional<QString> getDependencyURL(const DependencySearchArgs& args) const override
    {
        return args.dependency.version.length() != 0
                   ? QString("%1/version/%2").arg(BuildConfig.MODRINTH_PROD_URL, args.dependency.version)
                   : QString(R"(%1/project/%2/version?game_versions=["%3"]&loaders=["%4"]&include_changelog=%5)")
                         .arg(BuildConfig.MODRINTH_PROD_URL)
                         .arg(args.dependency.addonId.toString())
                         .arg(mapMCVersionToModrinth(args.mcVersion))
                         .arg(getModLoaderStrings(args.loader).join("\",\""))
                         .arg(args.includeChangelog ? "true" : "false");
    };

    QJsonArray documentToArray(QJsonDocument& obj) const override { return obj.object().value("hits").toArray(); }
    Result<> loadIndexedPack(ModPlatform::IndexedPack& m, const QJsonObject& obj) const override
    {
        return Modrinth::loadIndexedPack(m, obj);
    }
    Result<ModPlatform::IndexedVersion> loadIndexedPackVersion(QJsonObject& obj, ModPlatform::ResourceType /*unused*/) const override
    {
        return Modrinth::loadIndexedPackVersion(obj);
    };
    Result<> loadExtraPackInfo(ModPlatform::IndexedPack& m, QJsonObject& obj) const override { return Modrinth::loadExtraPackData(m, obj); }
};
