// SPDX-License-Identifier: GPL-3.0-only
#include "CoreBackend.h"
#include "InboxService.h"
#include "CurseForgeService.h"
#include "awake/integration/DurableIO.h"
#include "awake/integration/InstanceDiscovery.h"
#include "awake/integration/ProfileAccess.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QRunnable>
#include <QTimer>
#include <algorithm>
namespace Awake::Core {
using namespace Awake::Integration;
namespace {
QJsonObject accepted(const QJsonObject& status)
{
    QJsonObject result;
    for (const auto* key : {"operationId", "state", "terminal", "replayed"}) result[key] = status[key];
    return {{"result", result}};
}
bool legacyResponse(const QJsonValue& value)
{
    if (value.isArray()) { for (const auto& child : value.toArray()) if (!legacyResponse(child)) return false; }
    if (value.isObject()) {
        const auto object = value.toObject();
        if (object["provider"] == "curseforge") return false;
        if (object["loader"].isObject() && !QStringList{"vanilla", "fabric"}.contains(object["loader"].toObject()["id"].toString())) return false;
        for (auto it = object.begin(); it != object.end(); ++it) if (!legacyResponse(it.value())) return false;
    }
    return true;
}
constexpr auto evidenceExtension = "manager.v0.2.1";
QJsonObject serviceFailure(const ServiceError& error)
{
    auto response = failure(error.code, error.message.left(2048), error.retryable);
    if (!error.details.isEmpty()) { auto body = response["error"].toObject(); body["details"] = error.details; response["error"] = body; }
    return response;
}
QJsonObject withoutDetails(QJsonObject operation)
{
    // value(): operator[] on a non-const QJsonObject would insert "error": null into successful operations.
    if (operation.value("error").isObject()) { auto error = operation.value("error").toObject(); error.remove("details"); operation["error"] = error; }
    return operation;
}
QJsonObject page(const QJsonArray& all, const QJsonObject& params)
{
    QJsonArray values; const int offset = params["offset"].toInt(), limit = params["limit"].toInt(100);
    for (int i = offset; i < all.size() && i < offset + limit; ++i) values.append(all[i]);
    return {{"operations", values}, {"total", all.size()}, {"nextOffset", offset + values.size() < all.size() ? QJsonValue(offset + values.size()) : QJsonValue(QJsonValue::Null)}};
}
}
CoreBackend::CoreBackend(QObject* parent) : QObject(parent) { m_pool.setMaxThreadCount(2); }
CoreBackend::~CoreBackend() { m_pool.waitForDone(); }
bool CoreBackend::start(const QString& requested, QString* error)
{
    try {
        if (!m_contract.valid("core.info.params", QJsonObject{})) throw ServiceError{"INTERNAL_ERROR", "Embedded integration contract is unavailable."};
        if (!QDir::isAbsolutePath(requested) || !createPrivateStorageDirectory(requested)) throw ServiceError{"IO_ERROR", "A private absolute local profile is required."};
        m_profile = QFileInfo(requested).canonicalFilePath(); m_profileId = physicalIdentityV2(m_profile);
        if (m_profileId.isEmpty()) throw ServiceError{"IO_ERROR", "Profile physical identity cannot be verified."};
        for (const auto* child : {"workspaces", "catalog", "ownership"})
            if (!createPrivateStorageDirectory(m_profile + '/' + child)) throw ServiceError{"IO_ERROR", "Private backend storage is unavailable."};
        m_store = std::make_unique<OperationStore>(m_profile + "/operations"); QString reason;
        if (!m_store->open(&reason)) throw ServiceError{"RECOVERY_REQUIRED", reason};
        // Inventory, revision and update reads hash every mod repeatedly; unchanged files need no second read.
        Awake::Integration::DurableIO::setHashCacheEnabled(true);
        WorkspaceServices(m_profile, *m_store).startupRecovery();
        WorkspaceServices(m_profile, *m_store).sweepDeleted();
        WorkspaceServices(m_profile, *m_store).sweepShared();
        IntegrationServer::Options options; options.major = 2; options.directory = "core-v2"; options.prefix = "awake-core-v2-";
        options.capabilities = m_contract.capabilities(true);
        options.extension = "manager.v0.2"; options.legacyCapabilities = m_contract.capabilities(true, false);
        options.nextExtension = evidenceExtension; options.nextCapabilities = m_contract.capabilities(true, true, true);
        options.hello = {{"role", "modpack-core"}, {"coreVersion", "0.4.1"}, {"storageVersion", 1}};
        options.validate = [this](const QString& method, const QJsonObject& params) { return m_contract.valid(method + ".params", params); };
        if (!m_server.start(m_profile, {{"role", "modpack-core"}, {"profileId", m_profileId}},
            [this](const Request& request, IntegrationServer::Reply reply) { handle(request, std::move(reply)); }, options, &reason)) throw ServiceError{"IO_ERROR", reason};
        m_idle.start(); auto* idle = new QTimer(this); idle->setInterval(1000);
        connect(idle, &QTimer::timeout, this, [this] {
            if (m_server.clientCount() || !m_jobs.isEmpty() || m_pool.activeThreadCount() || WorkspaceServices(m_profile, *m_store).unsafeToExit()) {
                m_idle.restart(); return;
            }
            if (m_idle.elapsed() >= 60000) QCoreApplication::quit();
        });
        idle->start();
        return true;
    } catch (const ServiceError& e) { if (error) *error = e.code + ": " + e.message; return false; }
}
void CoreBackend::handle(const Request& request, IntegrationServer::Reply reply)
{
    const bool evidence = request.managerExtension == evidenceExtension;
    const auto checked = [this, method = request.method, extensions = request.managerExtensions, evidence, reply](QJsonObject response) {
        // Paged operation lists never carry conflict details (bounded frames); operations.get returns them.
        if ((method == "operations.list" || method == "events.poll") && response.value("result").isObject()) {
            auto result = response.value("result").toObject();
            QJsonArray rows; for (const auto& value : result["operations"].toArray()) rows.append(withoutDetails(value.toObject()));
            result["operations"] = rows; response["result"] = result;
        }
        // manager.v0.2.1 additions are invisible to older clients, whose pinned schemas reject unknown fields.
        if (!evidence) {
            if (response.value("error").isObject()) { auto error = response.value("error").toObject(); error.remove("details"); response["error"] = error; }
            if (response.value("result").isObject()) {
                auto result = response.value("result").toObject();
                if (method == "operations.get") result = withoutDetails(result);
                if (method == "plans.get") result.remove("dependencyResolutions");
                if (method == "mods.list") {
                    QJsonArray rows; for (const auto& value : result["items"].toArray()) { auto row = value.toObject(); row.remove("loaderBridge"); rows.append(row); }
                    result["items"] = rows;
                }
                response["result"] = result;
            }
        }
        if (!extensions && response.contains("result") && (method == "mods.list" || method == "plans.get")) {
            auto result = response["result"].toObject(); const auto key = method == "mods.list" ? "items" : "files";
            QJsonArray rows;
            for (const auto& value : result[key].toArray()) {
                auto row = value.toObject();
                if (method == "mods.list") { row.remove("policy"); row.remove("nativeLock"); }
                else row.remove("kind");
                rows.append(row);
            }
            result[key] = rows; response["result"] = result;
        }
        if (!extensions && response.contains("result") && !legacyResponse(response["result"])) { reply(failure("UNSUPPORTED_CAPABILITY", "This data requires the manager.v0.2 extension; upgrade the client.")); return; }
        const auto key = response.contains("error") ? "error" : method + ".result";
        const auto value = response.contains("error") ? response["error"] : response["result"];
        reply(m_contract.valid(key, value) ? response : failure("INTERNAL_ERROR", "Native result did not match the committed contract."));
    };
    try {
        WorkspaceServices services(m_profile, *m_store);
        if (!evidence && request.params.contains("dependencyResolutions"))
            throw ServiceError{"UNSUPPORTED_CAPABILITY", "Reviewed dependency resolutions require the manager.v0.2.1 extension."};
        if (!request.managerExtensions) {
            bool extended = request.params["provider"] == "curseforge" ||
                (request.params["loader"].isObject() && !QStringList{"vanilla", "fabric"}.contains(request.params["loader"].toObject()["id"].toString())) ||
                (request.params["loader"].isString() && !QStringList{"vanilla", "fabric"}.contains(request.params["loader"].toString()));
            for (const auto& value : request.params["changes"].toArray()) {
                const auto change = value.toObject();
                extended = extended || change["action"] == "policy" || change["provider"] == "curseforge" || change.contains("artifactSha512");
            }
            auto physical = request.params["physicalId"].toString();
            if (physical.isEmpty() && request.params.contains("operationId") && QStringList{"transactions.apply", "recovery.run", "plans.get"}.contains(request.method)) {
                const auto context = services.context(request.params["operationId"].toString())["request"].toObject();
                physical = context["physicalId"].toString();
            }
            if (extended || (!physical.isEmpty() && !services.legacyCompatible(physical))) throw ServiceError{"UNSUPPORTED_CAPABILITY", "This workspace or request requires the manager.v0.2 extension; upgrade the client."};
        }
        if (request.method == "imports.prepare" || request.method == "workspaces.create" || request.method == "workspaces.delete" || request.method == "workspaces.adopt" || request.method == "plans.prepare" || request.method == "transactions.apply" || request.method == "recovery.run") {
            submit(request, checked); return;
        }
        if (request.method == "operations.cancel") {
            const auto id = request.params["operationId"].toString(); const auto state = services.operation(id)["state"].toString();
            const bool cancellable = !m_critical.contains(id) && QStringList{"queued", "resolving", "downloading", "prepared"}.contains(state);
            if (cancellable) {
                requireResult(m_store->note(id, {}, true));
                if (m_jobs.contains(id)) m_jobs[id]->store(true); else requireResult(m_store->transition(id, "cancelled"));
            }
            checked({{"result", QJsonObject{{"cancellationRequested", cancellable}, {"cancellable", cancellable}}}}); return;
        }
        if (request.method == "core.shutdown") {
            if (!m_jobs.isEmpty() || m_pool.activeThreadCount() || services.unsafeToExit()) { checked(failure("BUSY", "Active work, prepared plans or unresolved recovery must be resolved before shutdown.", true)); return; }
            checked({{"result", QJsonObject{{"shuttingDown", true}}}});
            QTimer::singleShot(100, qApp, &QCoreApplication::quit); return;
        }
        if (request.method.startsWith("operations.") || request.method == "events.poll" || request.method == "core.info" || request.method == "recovery.list") {
            checked({{"result", read(request)}}); return;
        }
        m_pool.start(QRunnable::create([this, request, checked] {
            try { checked({{"result", read(request)}}); }
            catch (const ServiceError& error) { checked(serviceFailure(error)); }
            catch (...) { checked(failure("INTERNAL_ERROR", "Native read failed.")); }
        }));
    } catch (const ServiceError& error) { checked(serviceFailure(error)); }
}
void CoreBackend::submit(const Request& request, IntegrationServer::Reply reply)
{
    WorkspaceServices services(m_profile, *m_store); const auto method = request.method; const auto params = request.params;
    const auto existingId = params["operationId"].toString();
    const auto replay = m_store->replay(method, params["idempotencyKey"].toString(), params, existingId);
    if (!replay.isEmpty()) { requireResult(replay); reply(accepted(replay["result"].toObject())); return; }
    auto physical = params["physicalId"].toString();
    if (!existingId.isEmpty()) physical = services.context(existingId)["request"].toObject()["physicalId"].toString();
    if (m_jobs.size() >= 16 || m_jobs.contains(existingId) || (!physical.isEmpty() && m_reserved.contains(physical)))
        throw ServiceError{"BUSY", "The workspace or bounded operation queue is busy.", true};
    if (method == "transactions.apply") {
        const auto plan = services.plan({{"operationId", existingId}, {"limit", 1}});
        if (plan["planDigest"] != params["planDigest"] || plan["instanceRevision"] != params["instanceRevision"]) throw ServiceError{"STALE_PLAN", "Apply does not identify the reviewed plan."};
    }
    if (method == "recovery.run" && services.context(existingId)["method"] == "workspaces.create")
        throw ServiceError{"UNSUPPORTED_CAPABILITY", "Partial workspace initialization requires inspection; automatic deletion is unavailable."};
    const auto begun = m_store->begin(method, params["idempotencyKey"].toString(), params, existingId); requireResult(begun);
    const auto status = begun["result"].toObject(); const auto id = status["operationId"].toString();
    if (status["replayed"].toBool()) { reply(accepted(status)); return; }
    try {
        if (existingId.isEmpty()) services.saveContext(id, method, params);
        auto cancel = std::make_shared<std::atomic_bool>(false); m_jobs[id] = cancel;
        if (!physical.isEmpty()) m_reserved.insert(physical);
        if (method != "plans.prepare" && method != "imports.prepare") m_critical.insert(id);
        reply(accepted(status));
        m_pool.start(QRunnable::create([this, id, physical, method, params, cancel] {
            WorkspaceServices work(m_profile, *m_store, cancel);
            try {
                if (method == "workspaces.create") work.create(id, params);
                else if (method == "workspaces.delete") work.remove(id, params);
                else if (method == "workspaces.adopt") work.adopt(id, params);
                else if (method == "plans.prepare") work.prepare(id, params);
                else if (method == "imports.prepare") work.prepareImport(id, params);
                else if (method == "transactions.apply") work.apply(id, params);
                else work.recover(id);
            } catch (const ServiceError& error) {
                const auto status = m_store->get(id)["result"].toObject(); const auto state = status["state"].toString();
                if (!status["terminal"].toBool()) {
                    const bool live = QStringList{"acquiring", "revalidating", "backing_up", "applying", "verifying", "rolling_back", "recovery_required"}.contains(state) ||
                        (method == "workspaces.create" && QFileInfo::exists(m_profile + "/workspaces/" + id));
                    const auto target = live ? "recovery_required" : error.code == "CANCELLED" ? "cancelled" : "failed";
                    m_store->transition(id, target, {}, serviceFailure(error)["error"].toObject());
                    if (live && method != "workspaces.create") { try { work.recover(id); } catch (...) {} }
                }
            } catch (...) {
                m_store->transition(id, "recovery_required", {}, {{"code", "INTERNAL_ERROR"}, {"message", "Unexpected native failure; recovery evidence retained."}, {"retryable", false}});
            }
            QMetaObject::invokeMethod(this, [this, id, physical, cancel] {
                if (cancel->load() && m_store->get(id)["result"].toObject()["state"] == "prepared") m_store->transition(id, "cancelled");
                m_jobs.remove(id); m_critical.remove(id); m_reserved.remove(physical);
            }, Qt::QueuedConnection);
        }));
    } catch (const ServiceError& error) {
        if (existingId.isEmpty()) m_store->transition(id, "failed", {}, serviceFailure(error)["error"].toObject());
        throw;
    }
}
QJsonObject CoreBackend::read(const Request& request)
{
    WorkspaceServices services(m_profile, *m_store); ModrinthService provider; CurseForgeService curseforge; InboxService inbox(m_profile);
    const auto method = request.method; const auto params = request.params;
    if (method == "core.info") {
        const auto recovery = services.recoveryStatus();
        return {{"role", "modpack-core"}, {"coreVersion", "0.4.1"}, {"profileId", m_profileId}, {"dataRoot", m_profile},
            {"workspaceRoot", m_profile + "/workspaces"}, {"state", recovery["operations"].toArray().isEmpty() && recovery["blockedWorkspaces"].toArray().isEmpty() ? "ready" : "recovery_required"},
            {"supportedLoaders", request.managerExtensions ? QJsonArray{"vanilla", "fabric", "forge", "neoforge", "quilt"} : QJsonArray{"vanilla", "fabric"}}};
    }
    if (method == "catalog.versions") return provider.catalog(params);
    if (method == "providers.search") return params["provider"] == "curseforge" ? curseforge.search(params) : provider.search(params);
    if (method == "providers.project") return {{"project", params["provider"] == "curseforge" ? curseforge.lookup(params["projectId"].toString()) : provider.lookup(params["projectId"].toString())}};
    if (method == "outbox.list") return services.outboxList(params);
    if (method == "outbox.read") return services.outboxRead(params);
    if (method == "providers.versions") return params["provider"] == "curseforge" ? curseforge.versions(params) : provider.versions(params);
    if (method == "imports.inspect") return services.inspectImport(params);
    if (method == "inbox.begin") return inbox.begin(params);
    if (method == "inbox.write") return inbox.write(params);
    if (method == "inbox.commit") return inbox.commit(params);
    if (method == "inbox.status") return inbox.status(params);
    if (method == "inbox.discard") return inbox.discard(params);
    if (method == "workspaces.list") return services.list(params);
    if (method == "workspaces.rename") return services.rename(params);
    if (method == "workspaces.release") return services.release(params);
    if (method == "mods.list") return services.inventory(params);
    if (method == "updates.check") return services.updates(params);
    if (method == "plans.get") return services.plan(params);
    if (method == "operations.get") return services.operation(params["operationId"].toString());
    if (method == "recovery.list") return services.recoveryStatus();
    if (method == "operations.list" || method == "events.poll") {
        QList<QJsonObject> all;
        for (const auto& value : m_store->all()) all.append(services.operation(value.toObject()["operationId"].toString()));
        std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a["sequence"].toString().toULongLong() < b["sequence"].toString().toULongLong(); });
        QJsonArray rows; const auto after = params["afterSequence"].toString("0").toULongLong();
        for (const auto& value : all) if (method != "events.poll" || value["sequence"].toString().toULongLong() > after) rows.append(value);
        auto result = page(rows, params);
        if (method == "events.poll") {
            const auto values = result["operations"].toArray();
            return {{"operations", values}, {"nextSequence", values.isEmpty() ? QString::number(after) : values.last().toObject()["sequence"].toString()}, {"sessionId", m_server.information()["sessionId"]}};
        }
        return result;
    }
    throw ServiceError{"UNSUPPORTED_CAPABILITY", "This capability is unavailable."};
}
}
