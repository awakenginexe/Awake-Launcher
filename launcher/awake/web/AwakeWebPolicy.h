// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QUrl>
#include <QVariantMap>

namespace Awake::Web {
bool internalUrl(const QUrl& url);
QString resourcePath(const QUrl& url);
bool externalUrl(const QUrl& url);
bool actionAllowed(const QString& action);
bool preferenceAllowed(const QString& key, const QVariant& value);
QString frontendLocale(QString locale);
QString nativeLocale(QString locale);

struct InstanceData {
    QString id, name, group, minecraftVersion, loader, loaderVersion, iconUrl;
    bool pinned = false, canLaunch = false, running = false, broken = false;
    qint64 lastLaunch = 0, lastTimePlayed = 0, totalTimePlayed = 0;
};
QVariantMap instanceDto(const InstanceData& instance);
}  // namespace Awake::Web
