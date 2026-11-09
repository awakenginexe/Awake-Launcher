// SPDX-License-Identifier: GPL-3.0-only
// A mod published on both Modrinth and CurseForge is one mod. Requirements name a project on their
// own site, so the identical file from the other site has to count as that project.
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
namespace Awake::Core {
/** The project already in the pack, from the other site, that is this dependency's file. Empty if none. */
inline QString crossSiteTwin(const QJsonObject& desired, const QString& dependencyKey, const QJsonObject& artifact)
{
    const QString site = dependencyKey.startsWith("curseforge:") ? QStringLiteral("curseforge") : QStringLiteral("modrinth");
    const auto sha1 = artifact["sha1"].toString(), fileName = artifact["fileName"].toString();
    for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) {
        const auto entry = it.value().toObject();
        const auto version = entry["version"].toObject();
        if (it.key() == dependencyKey || !entry["enabled"].toBool(true) || version["provider"].toString("modrinth") == site) continue;
        const auto file = version["files"].toArray().first().toObject();
        if ((!sha1.isEmpty() && file["sha1"].toString() == sha1) || (!fileName.isEmpty() && file["fileName"].toString().compare(fileName, Qt::CaseInsensitive) == 0)) return it.key();
    }
    return {};
}
/** An enabled project in the pack was recorded as this dependency's file from the other site. */
inline bool providedAcrossSites(const QJsonObject& desired, const QString& dependencyKey)
{
    for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) {
        const auto entry = it.value().toObject();
        if (entry["enabled"].toBool(true) && entry["alsoProvides"].toArray().contains(QJsonValue(dependencyKey))) return true;
    }
    return false;
}
}
