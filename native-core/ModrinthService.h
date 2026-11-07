// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QDateTime>
#include <QUrl>
#include <atomic>
#include <functional>
#include <memory>
#include "modplatform/ModIndex.h"
namespace Awake::Core {
struct ServiceError {
    QString code, message;
    bool retryable = false;
    // Structured, contract-validated detail (manager.v0.2.1 clients only), e.g. {"conflicts": [...]}.
    QJsonObject details = {};
};
QJsonObject projectMetadata(const ModPlatform::IndexedPack& pack);
// `bridge` names the loader bridge that admits an artifact not tagged for the workspace loader
// (only "sinytra-connector": a Fabric artifact in a Forge/NeoForge workspace), otherwise empty.
struct ResolvedVersion { QJsonObject wire; ModPlatform::IndexedVersion native; QDateTime publishedAt; QString bridge = {}; };
class ModrinthService {
public:
    // Tests inject bytes, never endpoints or paths supplied over IPC.
    using Fetch = std::function<QByteArray(const QUrl&, qint64)>;
    explicit ModrinthService(std::shared_ptr<std::atomic_bool> cancel = {}, Fetch fixture = {});
    QJsonObject catalog(const QJsonObject& params);
    QJsonObject search(const QJsonObject& params);
    QJsonObject versions(const QJsonObject& params);
    QList<ResolvedVersion> compatibleVersions(const QJsonObject& params);
    ResolvedVersion version(const QString& id, const QString& minecraft, const QString& loader, const QString& artifactSha512 = {}, bool allowBridge = false);
    // Identity and label of any version plus whether it is installable for the workspace (conflict candidates).
    QJsonObject versionInfo(const QString& id, const QString& minecraft, const QString& loader);
    // Newest primary JAR of a project for a Minecraft version on any loader, used only to read its
    // loader metadata as evidence (never installed). Null when no such artifact exists.
    QJsonObject anyArtifact(const QString& projectId, const QString& minecraft);
    ModPlatform::IndexedPack project(const QString& id);
    QJsonObject lookup(const QString& id);
    ResolvedVersion versionByHash(const QString& sha512, const QString& minecraft, const QString& loader, bool allowBridge = false);
    void download(const QJsonObject& artifact, const QString& destination);
    void checkCancelled() const;
private:
    std::shared_ptr<std::atomic_bool> m_cancel;
    Fetch m_fixture;
    QByteArray transfer(const QUrl& url, qint64 limit, const QString& file = {});
    QJsonDocument json(const QUrl& url);
    ResolvedVersion parseVersion(const QJsonObject& object, const QString& minecraft, const QString& loader, const QString& artifactSha512 = {}, bool allowBridge = false);
};
}
