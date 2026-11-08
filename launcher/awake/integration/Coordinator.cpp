// SPDX-License-Identifier: GPL-3.0-only
#include "Coordinator.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QIcon>
#include <QPixmap>
#include <QTimer>
#include <QUuid>
#include <QtConcurrentRun>
#include <limits>
#include "Application.h"
#include "InstanceDiscovery.h"
#include "InstanceList.h"
#include "icons/IconList.h"
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
                         LaunchStarter launchStarter,
                         QObject* parent)
    : QObject(parent)
    , m_instances(instances)
    , m_activityReader(std::move(activityReader))
    , m_launchStarter(std::move(launchStarter))
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
    // Only a Manager that asks for this extension sees the additions; older ones keep the read-only contract.
    IntegrationServer::Options options;
    options.extension = "launcher.v3.2";
    options.capabilities = managerCapabilities();
    options.legacyCapabilities = capabilities();
    return m_server.start(
        m_dataRoot, m_information, [this](const Request& request, IntegrationServer::Reply reply) { handle(request, std::move(reply)); },
        options, reason);
}
void Coordinator::stop()
{
    m_server.stop();
}
MinecraftInstance* Coordinator::findInstance(const QString& physicalId) const
{
    for (int i = 0; i < m_instances->count(); ++i) {
        auto* instance = m_instances->at(i);
        if (physicalIdentity(QDir(m_dataRoot).absoluteFilePath(instance->instanceRoot())) == physicalId) return instance;
    }
    return nullptr;
}
bool Coordinator::isCoordinating(MinecraftInstance* instance)
{
    if (m_coordinating.isEmpty()) return false;  // The normal case, and the only cost when no Manager is connecting anything.
    for (auto it = m_coordinating.begin(); it != m_coordinating.end();) it = it->hasExpired() ? m_coordinating.erase(it) : std::next(it);
    return m_coordinating.contains(physicalIdentity(QDir(m_dataRoot).absoluteFilePath(instance->instanceRoot())));
}
QJsonObject Coordinator::launchStatus(const QString& operationId)
{
    auto& operation = m_launches[operationId];
    if (operation.started && (operation.state == "queued" || operation.state == "acquiring")) {
        auto* instance = m_instances->getInstanceById(operation.instanceId);
        const auto activity = instance ? m_activityReader(instance) : QString("idle");
        operation.state = activity == "launch_active" ? "completed" : activity == "idle" || activity == "deleting" ? "failed" : "acquiring";
    }
    const bool failed = operation.state == "failed";
    QJsonObject result{ { "operationId", operationId }, { "state", operation.state }, { "terminal", failed || operation.state == "completed" } };
    if (failed) result["error"] = QJsonObject{ { "code", "INTERACTION_REQUIRED" }, { "message", "The game did not start. Check Awake Launcher for details." }, { "retryable", false } };
    return result;
}
bool Coordinator::handleManagerRequest(const Request& request, IntegrationServer::Reply& reply)
{
    const auto& params = request.params;
    if (request.method == "operations.get") {
        const auto id = params["operationId"].toString();
        reply(m_launches.contains(id) ? QJsonObject{ { "result", launchStatus(id) } } : failure("NOT_FOUND", "Unknown operation."));
        return true;
    }
    if (request.method != "launch.request" && request.method != "instances.enroll" && request.method != "instances.icon") return false;
    const auto target = params["physicalId"].toString();
    if (target.isEmpty() || target.size() > 256) { reply(failure("INVALID_REQUEST", "A physicalId from discovery is required.")); return true; }
    auto* instance = findInstance(target);
    if (request.method == "instances.enroll") {
        const auto phase = params["phase"].toString();
        if (phase == "begin") {
            if (!instance) { reply(failure("NOT_FOUND", "No loaded native instance has that physicalId.")); return true; }
            if (instance->isManagerLinked() && !instance->isManagerShared()) { reply(failure("INVALID_REQUEST", "This instance already belongs to the Manager.")); return true; }
            if (m_activityReader(instance) != "idle") { reply(failure("BUSY", "Close the game using this instance first.", true)); return true; }
            m_coordinating[target] = QDeadlineTimer(5 * 60 * 1000);
        } else if (phase == "finish" || phase == "abort") {
            m_coordinating.remove(target);
            if (phase == "finish") m_instances->loadList();
        } else {
            reply(failure("INVALID_REQUEST", "Unknown phase."));
            return true;
        }
        reply({ { "result", QJsonObject{ { "accepted", true } } } });
        return true;
    }
    if (!instance) { reply(failure("NOT_FOUND", "No loaded native instance has that physicalId.")); return true; }
    if (request.method == "instances.icon") {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!APPLICATION->icons()->getIcon(instance->iconKey()).pixmap(96, 96).save(&buffer, "PNG") || png.isEmpty() || png.size() > 256 * 1024) {
            reply(failure("NOT_FOUND", "This instance has no usable image."));
            return true;
        }
        reply({ { "result", QJsonObject{ { "mimeType", "image/png" }, { "base64", QString::fromLatin1(png.toBase64()) } } } });
        return true;
    }
    const auto key = params["idempotencyKey"].toString();
    if (key.isEmpty() || key.size() > 128) { reply(failure("INVALID_REQUEST", "An idempotency key is required.")); return true; }
    if (!m_launchKeys.contains(key)) {
        if (m_launches.size() >= 64) { m_launches.clear(); m_launchKeys.clear(); }
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_launches[id].instanceId = instance->id();
        m_launchKeys[key] = id;
        // launch() can open account dialogs; the Manager is answered first and follows with operations.get.
        QTimer::singleShot(0, this, [this, id, instanceId = instance->id()] {
            auto* current = m_instances->getInstanceById(instanceId);
            // launch() can run a nested event loop, so nothing here may outlive that call by reference.
            const bool started = current && m_launchStarter(current);
            if (!m_launches.contains(id)) return;
            m_launches[id].started = started;
            if (!started) m_launches[id].state = "failed";
        });
    }
    reply({ { "result", launchStatus(m_launchKeys[key]) } });
    return true;
}
void Coordinator::handle(const Request& request, IntegrationServer::Reply reply)
{
    if (physicalIdentity(m_dataRoot) != m_profileIdentity) {
        reply(failure("IDENTITY_UNVERIFIABLE", "The data profile was replaced; restart Launcher."));
        return;
    }
    if (handleManagerRequest(request, reply)) return;
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
