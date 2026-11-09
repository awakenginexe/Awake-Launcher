// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ModrinthService.h"
#include <QHash>

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
    QJsonObject lookup(const QString& id);
    void download(const QJsonObject& artifact, const QString& destination);
    // Accept exactly the files a published pack names, even when their provider tags are incomplete.
    void setPackAuthoritative(bool value) { m_packAuthoritative = value; }
    struct PackEntry { QString projectId, fileId, name, fileName, folder; QJsonObject artifact; bool mod = false, blocked = false; };
    // One batched read per project/file list instead of three requests per pack entry.
    QList<PackEntry> resolvePack(const QJsonArray& projects);
private:
    std::shared_ptr<std::atomic_bool> m_cancel;
    Fetch m_fixture;
    bool m_packAuthoritative = false;
    int m_redirects = 0;
    QHash<QString, QJsonObject> m_primed;
    QByteArray transfer(const QUrl& url, qint64 limit, const QString& path = {}, const QByteArray& post = {});
    QJsonObject json(const QUrl& url);
    QJsonObject anyProject(const QString& id);
    QJsonObject projectObject(const QString& id);
    ResolvedVersion parseVersion(const QJsonObject& raw, const QString& projectId, const QString& minecraft, const QString& loader);
};
}
