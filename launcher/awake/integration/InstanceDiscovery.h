// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
namespace Awake::Integration {
struct InstanceSeed {
    QString id, uuid, name, path, modsPath;
    bool running = false;
    QString activity;
};
QString physicalIdentity(const QString& path);
QString physicalIdentityV2(const QString& path);
QJsonObject discover(const QStringList& roots, const QList<InstanceSeed>& loaded);
QJsonObject inventory(const InstanceSeed& instance, int offset, int limit, const QString& expectedRevision = {});
}  // namespace Awake::Integration
