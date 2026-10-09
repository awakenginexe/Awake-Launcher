// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QList>
#include <QString>

namespace Awake::LocalImport {
struct Pack {
    QString provider, id, name, versionId, versionName;
    bool operator==(const Pack&) const = default;
};
struct Instance {
    QString path, name, source, minecraft, loader, loaderVersion, error;
    int memory = 0;
    qint64 playtime = 0;
    Pack pack;
};
Instance inspect(const QString& path);
QList<Instance> scan(const QString& path);
QStringList defaultLocations(const QString& source);
bool copyInstance(const Instance& instance, const QString& destination, QString* error);
bool writePackBaseline(const QString& archive, const QString& destination, const QString& provider,
                       const QString& minecraft, const QString& loader, const QString& loaderVersion, QString* error);
}
