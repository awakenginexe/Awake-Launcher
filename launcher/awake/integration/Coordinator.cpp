// SPDX-License-Identifier: GPL-3.0-only
#include "Coordinator.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtConcurrentRun>
#include <limits>
#include "InstanceDiscovery.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "modplatform/packwiz/Packwiz.h"
namespace Awake::Integration {
namespace {
QString digest(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QJsonObject indexedInventory(const InstanceSeed& seed, int offset, int limit, const QString& expected)
{
    auto response = inventory(seed, offset, limit, expected);
    if (response.contains("error"))
        return response;
    auto result = response["result"].toObject();
    QMap<QString, QJsonObject> metadata;
    QSet<QString> duplicates;
    const QDir index(QDir(seed.modsPath).filePath(".index"));
    for (const auto& entry : index.entryInfoList(QDir::Files | QDir::Hidden, QDir::Name)) {
        if (!entry.fileName().endsWith(".pw.toml") || entry.isSymLink())
            continue;
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            return failure("IO_ERROR", "Cannot read native mod metadata.");
        const auto bytes = file.read(4 * 1024 * 1024 + 1);
        if (file.error() != QFileDevice::NoError)
            return failure("IO_ERROR", "Native mod metadata read failed.");
        if (bytes.size() > 4 * 1024 * 1024)
            return failure("LIMIT_EXCEEDED", "Native mod metadata exceeds 4 MiB.");
        const auto mod = Packwiz::V1::parseModIndex(bytes, entry.fileName());
        const auto projectId = mod.projectId.toString(), versionId = mod.fileId.toString();
        if (!mod.isValid() || mod.filename.isEmpty() || mod.filename.size() > 255 || projectId.isEmpty() || versionId.isEmpty() ||
            projectId.size() > 256 || versionId.size() > 256 || mod.name.size() > 1024 || mod.versionNumber.size() > 256)
            continue;
        const auto provider = mod.provider == ModPlatform::ResourceProvider::MODRINTH ? "modrinth"
                              : mod.provider == ModPlatform::ResourceProvider::FLAME  ? "curseforge"
                                                                                      : "";
        if (!*provider)
            continue;
        if (metadata.contains(mod.filename))
            duplicates.insert(mod.filename);
        metadata[mod.filename] = { { "name", mod.name },       { "provider", provider },         { "projectId", projectId },
                                   { "versionId", versionId }, { "version", mod.versionNumber }, { "updateLocked", mod.lockUpdate } };
    }
    QJsonArray rows;
    for (const auto& value : result["items"].toArray()) {
        auto row = value.toObject();
        auto name = row["fileName"].toString();
        if (name.endsWith(".disabled", Qt::CaseInsensitive))
            name.chop(9);
        const bool conflict = duplicates.contains(name);
        row["metadataState"] = conflict ? "conflict" : metadata.contains(name) ? "indexed" : "unindexed";
        row["metadata"] = !conflict && metadata.contains(name) ? QJsonValue(metadata.value(name)) : QJsonValue::Null;
        rows.append(row);
    }
    // Recheck the observation after parsing so metadata changes invalidate this page.
    auto recheck = inventory(seed, offset, limit, result["instanceRevision"].toString());
    if (recheck.contains("error"))
        return recheck;
    result["items"] = rows;
    return { { "result", result } };
}

bool pageValue(const QJsonObject& params, const QString& key, int fallback, int minimum, int maximum, int& out)
{
    if (!params.contains(key)) {
        out = fallback;
        return true;
    }
    const auto value = params.value(key);
    out = value.toInt(-1);
    return value.isDouble() && value.toDouble() == out && out >= minimum && out <= maximum;
}
}  // namespace
Coordinator::Coordinator(InstanceList* instances,
                         const QString& installRoot,
                         const QString& dataRoot,
                         bool portable,
                         ActivityReader activityReader,
                         QObject* parent)
    : QObject(parent)
    , m_instances(instances)
    , m_activityReader(std::move(activityReader))
    , m_dataRoot(QFileInfo(dataRoot).canonicalFilePath())
    , m_profileIdentity(physicalIdentity(m_dataRoot))
{
    m_workers.setMaxThreadCount(2);
    m_information = { { "installationId", digest(("install:" + physicalIdentity(installRoot)).toUtf8()) },
                      { "profileId", digest(("profile:" + m_profileIdentity).toUtf8()) },
                      { "installRoot", QFileInfo(installRoot).canonicalFilePath() },
                      { "dataRoot", m_dataRoot },
                      { "executable", QCoreApplication::applicationFilePath() },
                      { "portable", portable },
                      { "launcherState", "ready" },
                      { "backgroundStartup", false } };
}
bool Coordinator::start(QString* reason)
{
    if (!m_instances || m_profileIdentity.isEmpty()) {
        if (reason)
            *reason = "The effective data profile cannot be identified.";
        return false;
    }
    return m_server.start(
        m_dataRoot, m_information, [this](const Request& request, IntegrationServer::Reply reply) { handle(request, std::move(reply)); },
        reason);
}
void Coordinator::stop()
{
    m_server.stop();
}
void Coordinator::handle(const Request& request, IntegrationServer::Reply reply)
{
    if (physicalIdentity(m_dataRoot) != m_profileIdentity) {
        reply(failure("IDENTITY_UNVERIFIABLE", "The data profile was replaced; restart Launcher."));
        return;
    }
    QStringList roots;
    for (const auto& root : m_instances->instanceDirs())
        roots.append(QDir(m_dataRoot).absoluteFilePath(root));
    if (request.method == "launcher.info") {
        if (!request.params.isEmpty()) {
            reply(failure("INVALID_REQUEST", "launcher.info takes no parameters."));
            return;
        }
        auto info = m_server.information();
        info["instanceRoots"] = QJsonArray::fromStringList(roots);
        reply({ { "result", info } });
        return;
    }
    const bool listing = request.method == "instances.list";
    const QSet<QString> allowed = listing ? QSet<QString>{ "offset", "limit", "instanceRevision" }
                                          : QSet<QString>{ "physicalId", "offset", "limit", "instanceRevision" };
    const auto params = request.params;
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (!allowed.contains(it.key())) {
            reply(failure("INVALID_REQUEST", "Unknown read parameter."));
            return;
        }
    }
    int offset = 0, limit = 200;
    if (!pageValue(params, "offset", 0, 0, std::numeric_limits<int>::max(), offset) || !pageValue(params, "limit", 200, 1, 500, limit) ||
        (params.contains("instanceRevision") &&
         (!params["instanceRevision"].isString() || params["instanceRevision"].toString().size() > 128)) ||
        (offset > 0 && params["instanceRevision"].toString().isEmpty())) {
        reply(failure("INVALID_REQUEST", "Invalid page parameters; continuation requires instanceRevision."));
        return;
    }
    const auto target = params["physicalId"].toString();
    if (!listing && (!params["physicalId"].isString() || target.isEmpty() || target.size() > 256)) {
        reply(failure("INVALID_REQUEST", "A physicalId from discovery is required."));
        return;
    }
    if (m_reads >= 2) {
        reply(failure("BUSY", "Native discovery is busy; retry after the pending read completes.", true));
        return;
    }
    QList<InstanceSeed> seeds;
    for (int i = 0; i < m_instances->count(); ++i) {
        auto* instance = m_instances->at(i);
        seeds.append({ instance->id(), instance->uuid(), instance->name(), QDir(m_dataRoot).absoluteFilePath(instance->instanceRoot()),
                       QDir(m_dataRoot).absoluteFilePath(instance->modsRoot()), instance->isRunning(), m_activityReader(instance) });
    }
    ++m_reads;
    auto* watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, reply = std::move(reply)] {
        --m_reads;
        try {
            reply(watcher->result());
        } catch (...) {
            reply(failure("INTERNAL_ERROR", "Native read failed."));
        }
        watcher->deleteLater();
    });
    const auto expected = params["instanceRevision"].toString();
    watcher->setFuture(QtConcurrent::run(&m_workers, [roots, seeds, listing, target, offset, limit, expected] {
        if (!listing) {
            for (const auto& seed : seeds)
                if (physicalIdentity(seed.path) == target)
                    return indexedInventory(seed, offset, limit, expected);
            return failure("NOT_FOUND", "No loaded native instance has that physicalId.");
        }
        auto found = discover(roots, seeds);
        const auto revision = digest(QJsonDocument(found).toJson(QJsonDocument::Compact));
        if (!expected.isEmpty() && expected != revision)
            return failure("STALE_INSTANCE", "Instance listing changed; restart pagination.");
        const auto all = found["instances"].toArray();
        QJsonArray page;
        const auto end = qMin(qsizetype(offset) + limit, all.size());
        for (qsizetype i = offset; i < end; ++i)
            page.append(all[i]);
        found["instances"] = page;
        found["total"] = all.size();
        found["nextOffset"] = end < all.size() ? QJsonValue(end) : QJsonValue(QJsonValue::Null);
        found["instanceRevision"] = revision;
        found["revisionKind"] = "observation";
        return QJsonObject{ { "result", found } };
    }));
}
}  // namespace Awake::Integration
