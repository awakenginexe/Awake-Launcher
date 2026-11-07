// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ModrinthService.h"

namespace Awake::Core {
class CurseForgeService {
public:
    using Fetch = ModrinthService::Fetch;
    explicit CurseForgeService(std::shared_ptr<std::atomic_bool> cancel = {}, Fetch fixture = {});
    bool available() const;
    void checkCancelled() const;
    QJsonObject search(const QJsonObject& params);
    QJsonObject versions(const QJsonObject& params);
    QList<ResolvedVersion> compatibleVersions(const QJsonObject& params);
    ResolvedVersion version(const QString& projectId, const QString& fileId, const QString& minecraft, const QString& loader);
    ModPlatform::IndexedPack project(const QString& id);
    void download(const QJsonObject& artifact, const QString& destination);
private:
    std::shared_ptr<std::atomic_bool> m_cancel;
    Fetch m_fixture;
    QByteArray transfer(const QUrl& url, qint64 limit, const QString& path = {});
    QJsonObject json(const QUrl& url);
    QJsonObject projectObject(const QString& id);
    ResolvedVersion parseVersion(const QJsonObject& raw, const QString& projectId, const QString& minecraft, const QString& loader);
};
}
