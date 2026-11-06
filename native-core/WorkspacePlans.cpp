// SPDX-License-Identifier: GPL-3.0-only
#include "WorkspaceServices.h"
#include "Contract.h"
#include "awake/integration/DurableIO.h"
#include "awake/integration/ProfileAccess.h"
#include "awake/integration/InstanceAuthority.h"
#include "awake/integration/TransactionEngine.h"
#include "modplatform/packwiz/Packwiz.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <optional>
namespace Awake::Core {
using namespace Awake::Integration;
namespace {
constexpr auto mods = ".minecraft/mods";
QString checkedHash(const QString& path)
{ QString error; const auto value = DurableIO::hash(path, &error); if (value.isEmpty()) throw ServiceError{"INTEGRITY_FAILED", error}; return value; }
void privateDirectory(const QString& path)
{ if (!createPrivateStorageDirectory(path)) throw ServiceError{"IO_ERROR", "Private staging is unavailable."}; }
void copy(const QString& source, const QString& target)
{ QString error; if (!DurableIO::copy(source, target, &error)) throw ServiceError{"IO_ERROR", error}; }
QJsonObject page(const QString& key, const QJsonArray& all, const QJsonObject& params)
{
    const int offset = params["offset"].toInt(), limit = params["limit"].toInt(100); QJsonArray values;
    for (int i = offset; i < all.size() && i < offset + limit; ++i) values.append(all[i]);
    return {{key, values}, {"total", all.size()}, {"nextOffset", offset + values.size() < all.size() ? QJsonValue(offset + values.size()) : QJsonValue(QJsonValue::Null)}};
}
}
void WorkspaceServices::prepare(const QString& id, const QJsonObject& request)
{
    requireResult(m_store.transition(id, "resolving"));
    const auto record = workspaceRecord(request["physicalId"].toString()), loader = record["loader"].toObject();
    const auto game = record["minecraftVersion"].toString(), loaderId = loader["id"].toString(), directory = operationDirectory(id), instanceRoot = root(record);
    TransactionEngine engine(m_store, instanceRoot, mods);
    QJsonObject originalPolicies; QJsonArray originalRows;
    {
        auto lease = acquire(record);
        if (QFileInfo::exists(instanceRoot + "/.awake-integration/recovery.json")) throw ServiceError{"RECOVERY_REQUIRED", "Workspace recovery must finish first."};
        if (engine.revision() != request["instanceRevision"]) throw ServiceError{"STALE_INSTANCE", "Workspace differs from the requested baseline."};
        originalPolicies = policies(record); originalRows = scan(record);
        if (engine.revision() != request["instanceRevision"]) throw ServiceError{"STALE_INSTANCE", "Workspace changed during inventory."};
    }
    auto desired = originalPolicies["projects"].toObject();
    QMap<QString, QJsonObject> byInventory, byProject, files;
    for (const auto& value : originalRows) {
        const auto row = value.toObject(); byInventory[row["inventoryId"].toString()] = row; files[row["fileName"].toString().toCaseFolded()] = row;
        if (row["projectId"].isString()) byProject[row["projectId"].toString()] = row;
    }
    for (auto it = desired.begin(); it != desired.end(); ++it) {
        if (!byProject.contains(it.key())) throw ServiceError{"POLICY_CONFLICT", "A tracked file is missing. Repair the discrepancy before preparing changes."};
        auto entry = it.value().toObject(); entry["enabled"] = byProject[it.key()]["enabled"]; it.value() = entry;
    }
    QMap<QString, ResolvedVersion> selected; QMap<QString, ModPlatform::IndexedPack> packs;
    QStringList queue; QSet<QString> touched, removed, unknownRemovals; QMap<QString, bool> unknownToggles;
    QJsonArray warnings, dependencies; QSet<QString> dependencyIds;
    const auto warn = [&](const QString& text) { if (warnings.size() < 128) warnings.append(text.left(1024)); };
    const auto choose = [&](const ResolvedVersion& version, const ModPlatform::IndexedPack& pack) {
        const auto project = version.wire["projectId"].toString(), versionId = version.wire["versionId"].toString();
        if (pack.addonId.toString() != project || !Contract().valid("version", version.wire)) throw ServiceError{"INTEGRITY_FAILED", "Provider version identity is invalid."};
        if (removed.contains(project)) throw ServiceError{"DEPENDENCY_CONFLICT", "A required dependency was explicitly removed."};
        if (selected.contains(project)) {
            if (selected[project].wire["versionId"] != versionId) throw ServiceError{"DEPENDENCY_CONFLICT", "Two requested dependencies require different versions of " + project};
            return;
        }
        const auto old = byProject.value(project);
        if (!old.isEmpty() && old["metadataState"] != "verified") throw ServiceError{"POLICY_CONFLICT", "A tracked mod or its metadata was modified; existing bytes were preserved."};
        if (!old.isEmpty() && old["versionId"] != versionId && old["updateLocked"].toBool()) throw ServiceError{"POLICY_CONFLICT", "A native update lock, exclusion or intentional pin blocks this version change."};
        if (selected.size() >= 1024) throw ServiceError{"LIMIT_EXCEEDED", "Dependency resolution exceeds 1,024 projects."};
        selected[project] = version; packs[project] = pack; queue.append(project);
        auto entry = desired[project].toObject(); entry["version"] = version.wire;
        entry["enabled"] = entry.contains("enabled") ? entry["enabled"] : QJsonValue(true); desired[project] = entry;
    };
    for (const auto& value : request["changes"].toArray()) {
        m_provider.checkCancelled(); const auto change = value.toObject(); const auto action = change["action"].toString();
        if (action == "install" || action == "replace") {
            const auto pack = m_provider.project(change["projectId"].toString()); const auto project = pack.addonId.toString();
            if (touched.contains(project) || (action == "install" && byProject.contains(project)) || (action == "replace" && !byProject.contains(project)))
                throw ServiceError{"POLICY_CONFLICT", "Conflicting install/replace actions for this project."};
            touched.insert(project); choose(m_provider.version(change["versionId"].toString(), game, loaderId), pack);
        } else {
            const auto inventoryId = change["inventoryId"].toString(); const auto row = byInventory.value(inventoryId);
            if (row.isEmpty()) throw ServiceError{"STALE_INSTANCE", "An inventory action no longer identifies a mod."};
            const auto project = row["projectId"].toString(), key = project.isEmpty() ? inventoryId : project;
            if (touched.contains(key)) throw ServiceError{"POLICY_CONFLICT", "More than one action targets the same mod."}; touched.insert(key);
            if (!project.isEmpty() && row["metadataState"] != "verified") throw ServiceError{"POLICY_CONFLICT", "Modified tracked data requires repair before mutation."};
            if (action == "remove") {
                if (project.isEmpty()) unknownRemovals.insert(inventoryId);
                else { removed.insert(project); desired.remove(project); }
            } else {
                const bool enabled = action == "enable";
                if (project.isEmpty()) unknownToggles[inventoryId] = enabled;
                else { auto entry = desired[project].toObject(); entry["enabled"] = enabled; desired[project] = entry; }
            }
        }
    }
    for (int cursor = 0; cursor < queue.size(); ++cursor) {
        const auto project = queue[cursor];
        for (const auto& value : selected[project].wire["dependencies"].toArray()) {
            m_provider.checkCancelled(); const auto dep = value.toObject(); const auto type = dep["type"].toString();
            if (type != "required") { warn(type + " dependency from " + project + ": " + dep["projectId"].toString() + " " + dep["versionId"].toString()); continue; }
            auto depProject = dep["projectId"].toString(), depVersion = dep["versionId"].toString();
            std::optional<ResolvedVersion> resolved;
            if (!depVersion.isEmpty()) { resolved = m_provider.version(depVersion, game, loaderId);
                if (!depProject.isEmpty() && depProject != resolved->wire["projectId"]) throw ServiceError{"DEPENDENCY_CONFLICT", "Dependency identity mismatch."}; depProject = resolved->wire["projectId"].toString(); }
            if (depProject.isEmpty()) throw ServiceError{"DEPENDENCY_CONFLICT", "Required dependency has no resolvable project or version."};
            if (removed.contains(depProject)) throw ServiceError{"DEPENDENCY_CONFLICT", "A required dependency was explicitly removed."};
            const auto current = desired[depProject].toObject();
            if (!current.isEmpty() && !current["enabled"].toBool(true)) throw ServiceError{"DEPENDENCY_CONFLICT", "A required dependency is disabled; enable it explicitly."};
            const auto currentVersion = current["version"].toObject()["versionId"].toString();
            if (current.isEmpty() || (!depVersion.isEmpty() && depVersion != currentVersion)) {
                if (!resolved) {
                    const auto choices = m_provider.versions({{"projectId", depProject}, {"minecraftVersion", game}, {"loader", loaderId}, {"limit", 200}})["versions"].toArray();
                    if (choices.isEmpty()) throw ServiceError{"DEPENDENCY_CONFLICT", "No compatible required dependency version exists."};
                    auto choice = choices.first().toObject();
                    for (const auto& candidate : choices) if (candidate.toObject()["releaseType"] == "release") { choice = candidate.toObject(); break; }
                    resolved = m_provider.version(choice["versionId"].toString(), game, loaderId);
                }
                choose(*resolved, m_provider.project(depProject));
            } else if (byProject.contains(depProject) && byProject[depProject]["metadataState"] != "verified")
                throw ServiceError{"POLICY_CONFLICT", "A required dependency has modified bytes or metadata."};
            depVersion = desired[depProject].toObject()["version"].toObject()["versionId"].toString();
            if (!dependencyIds.contains(depProject + ':' + depVersion)) {
                dependencyIds.insert(depProject + ':' + depVersion);
                dependencies.append(QJsonObject{{"projectId", depProject}, {"versionId", depVersion}, {"type", "required"}});
            }
        }
    }
    // Check the entire resulting active graph, including existing mods affected by removals or version changes.
    for (auto it = desired.begin(); it != desired.end(); ++it) {
        const auto entry = it.value().toObject(); if (!entry["enabled"].toBool(true)) continue;
        for (const auto& value : entry["version"].toObject()["dependencies"].toArray()) {
            const auto dep = value.toObject(); const auto type = dep["type"].toString();
            if (type != "required" && type != "incompatible") continue;
            const auto project = dep["projectId"].toString(), version = dep["versionId"].toString(); bool match = false;
            for (auto other = desired.begin(); other != desired.end(); ++other) {
                const auto target = other.value().toObject();
                if ((!project.isEmpty() && project != other.key()) || !target["enabled"].toBool(true)) continue;
                if ((project.isEmpty() && version.isEmpty()) || (!version.isEmpty() && target["version"].toObject()["versionId"] != version)) continue;
                match = true; break;
            }
            if ((type == "required" && !match) || (type == "incompatible" && match)) throw ServiceError{"DEPENDENCY_CONFLICT", "Resulting mod graph violates a declared dependency for " + it.key()};
        }
    }
    QSet<QString> destinations, indexes;
    for (auto it = desired.begin(); it != desired.end(); ++it) {
        const auto entry = it.value().toObject();
        const auto filename = selected.contains(it.key()) ? selected[it.key()].wire["files"].toArray().first().toObject()["fileName"].toString() : entry["fileName"].toString();
        const auto index = selected.contains(it.key()) ? "modrinth-" + it.key() + ".pw.toml" : entry["indexFile"].toString();
        if (destinations.contains(filename.toCaseFolded()) || indexes.contains(index.toCaseFolded()))
            throw ServiceError{"POLICY_CONFLICT", "Selected projects have colliding Windows filenames or metadata names."};
        destinations.insert(filename.toCaseFolded()); indexes.insert(index.toCaseFolded());
    }
    privateDirectory(directory + "/staging"); privateDirectory(directory + "/native-index");
    QMap<QString, FileChange> changes; int ordinal = 0;
    const auto remove = [&](const QString& path) { changes[path.toCaseFolded()] = {path, {}}; };
    const auto stage = [&](const QString& path, const QString& staged) { changes[path.toCaseFolded()] = {path, staged}; };
    for (const auto& inventoryId : unknownRemovals) remove(QString(mods) + '/' + byInventory[inventoryId]["fileName"].toString());
    for (const auto& project : removed) {
        const auto old = originalPolicies["projects"].toObject()[project].toObject();
        remove(QString(mods) + '/' + byProject[project]["fileName"].toString()); remove(QString(mods) + "/.index/" + old["indexFile"].toString());
    }
    if (!selected.isEmpty()) requireResult(m_store.transition(id, "downloading"));
    int downloaded = 0;
    for (auto it = selected.begin(); it != selected.end(); ++it) {
        m_provider.checkCancelled(); const auto project = it.key(); auto entry = desired[project].toObject(); const auto old = byProject.value(project);
        const auto wire = it.value().wire;
        if (!old.isEmpty() && old["versionId"] == wire["versionId"]) continue;
        const auto artifact = wire["files"].toArray().first().toObject(); const auto filename = artifact["fileName"].toString();
        if (QFileInfo(filename).fileName() != filename || filename.contains('\\') || !filename.endsWith(".jar", Qt::CaseInsensitive)) throw ServiceError{"INTEGRITY_FAILED", "Unsupported artifact filename."};
        auto target = filename + (entry["enabled"].toBool(true) ? "" : ".disabled");
        for (const auto& suffix : QStringList{"", ".disabled"}) {
            const auto collision = files.value((filename + suffix).toCaseFolded());
            if (!collision.isEmpty() && collision["projectId"] != project) throw ServiceError{"POLICY_CONFLICT", "An artifact would overwrite an unrelated or unknown file. Remove it in a separate reviewed operation first."};
        }
        if (!old.isEmpty() && old["fileName"].toString().compare(target, Qt::CaseInsensitive) == 0) target = old["fileName"].toString();
        const auto artifactName = "artifact-" + QString::number(++ordinal); m_provider.download(artifact, directory + "/staging/" + artifactName);
        auto pack = packs[project]; pack.slug = "modrinth-" + project;
        auto nativeVersion = it.value().native;
        auto native = Packwiz::V1::createModFormat(QDir(directory + "/native-index"), pack, nativeVersion);
        native.lockUpdate = old["updateLocked"].toBool();
        Packwiz::V1::updateModIndex(QDir(directory + "/native-index"), native);
        const auto indexFile = pack.slug + ".pw.toml", indexPath = directory + "/native-index/" + indexFile;
        const auto parsed = Packwiz::V1::getIndexForMod(QDir(directory + "/native-index"), pack.slug);
        if (!parsed.isValid() || parsed.projectId != native.projectId || parsed.fileId != native.fileId || parsed.filename != filename || parsed.hash != native.hash)
            throw ServiceError{"INTEGRITY_FAILED", "Native mod metadata serialization did not verify."};
        const auto indexName = "index-" + QString::number(ordinal); copy(indexPath, directory + "/staging/" + indexName);
        if (!old.isEmpty() && old["fileName"] != target) remove(QString(mods) + '/' + old["fileName"].toString());
        stage(QString(mods) + '/' + target, artifactName); stage(QString(mods) + "/.index/" + indexFile, indexName);
        entry["projectId"] = project; entry["versionId"] = wire["versionId"]; entry["versionNumber"] = wire["versionNumber"];
        entry["fileName"] = filename; entry["sha256"] = checkedHash(directory + "/staging/" + artifactName);
        entry["indexFile"] = indexFile; entry["indexSha256"] = checkedHash(directory + "/staging/" + indexName);
        entry["updateLocked"] = old["updateLocked"].toBool(); desired[project] = entry;
        requireResult(m_store.note(id, {{"phase", "downloading"}, {"completed", QString::number(++downloaded)}, {"total", QString::number(selected.size())}, {"unit", "files"}}));
    }
    m_provider.checkCancelled(); auto lease = acquire(record);
    if (engine.revision() != request["instanceRevision"]) throw ServiceError{"STALE_INSTANCE", "Workspace changed during staging. Prepare and review again."};
    const auto toggle = [&](const QJsonObject& row, bool enabled) {
        if (row["enabled"].toBool() == enabled) return;
        const auto previous = row["fileName"].toString(); auto target = previous;
        if (enabled) target.chop(9); else target += ".disabled";
        if (files.contains(target.toCaseFolded())) throw ServiceError{"POLICY_CONFLICT", "Enable/disable destination already exists."};
        const auto name = "toggle-" + QString::number(++ordinal); copy(instanceRoot + '/' + mods + '/' + previous, directory + "/staging/" + name);
        remove(QString(mods) + '/' + previous); stage(QString(mods) + '/' + target, name);
    };
    for (auto it = unknownToggles.begin(); it != unknownToggles.end(); ++it) toggle(byInventory[it.key()], it.value());
    for (auto it = desired.begin(); it != desired.end(); ++it) {
        const auto old = byProject.value(it.key());
        if (!old.isEmpty() && (!selected.contains(it.key()) || selected[it.key()].wire["versionId"] == old["versionId"])) toggle(old, it.value().toObject()["enabled"].toBool(true));
    }
    auto finalPolicies = originalPolicies; finalPolicies["projects"] = desired;
    if (finalPolicies != originalPolicies) {
        QString error; if (!DurableIO::write(directory + "/staging/policies", QJsonDocument(finalPolicies).toJson(QJsonDocument::Compact), &error)) throw ServiceError{"IO_ERROR", error};
        stage(".awake-integration/policies.json", "policies");
    }
    QList<FileChange> table; for (const auto& change : changes) table.append(change);
    requireResult(engine.prepare(id, table, request["instanceRevision"].toString(), {{"warnings", warnings}, {"dependencies", dependencies}}));
}
void WorkspaceServices::apply(const QString& id, const QJsonObject& request)
{
    const auto initial = context(id)["request"].toObject(); const auto record = workspaceRecord(initial["physicalId"].toString()); auto lease = acquire(record);
    TransactionEngine engine(m_store, root(record), mods);
    requireResult(engine.apply(id, request["planDigest"].toString(), request["instanceRevision"].toString(), lease));
    // No native model is loaded for these roots; parse/hash all native metadata before durable completion.
    const auto rows = scan(record);
    for (const auto& value : rows) if (value.toObject()["metadataState"] == "modified") throw ServiceError{"RECOVERY_REQUIRED", "Applied metadata did not verify."};
    requireResult(engine.complete(id, lease));
}
QJsonObject WorkspaceServices::updates(const QJsonObject& params)
{
    const auto snapshot = inventory(params); const auto record = workspaceRecord(params["physicalId"].toString()); QJsonArray updates;
    for (const auto& value : snapshot["items"].toArray()) {
        const auto row = value.toObject(); if (!row["projectId"].isString()) continue;
        const bool excluded = row["updateLocked"].toBool() || row["metadataState"] != "verified"; QJsonValue candidate(QJsonValue::Null);
        if (!excluded) {
            const auto choices = m_provider.compatibleVersions({{"projectId", row["projectId"]}, {"minecraftVersion", record["minecraftVersion"]},
                {"loader", record["loader"].toObject()["id"]}});
            QDateTime installed;
            for (const auto& choice : choices) if (choice.wire["versionId"] == row["versionId"]) { installed = choice.publishedAt; break; }
            if (installed.isValid()) for (const auto& choice : choices) {
                if (choice.publishedAt <= installed) break;
                if (choice.wire["releaseType"] == "release") { candidate = choice.wire; break; }
            }
        }
        updates.append(QJsonObject{{"projectId", row["projectId"]}, {"installedVersionId", row["versionId"]}, {"candidate", candidate}, {"excluded", excluded}});
    }
    if (inventory({{"physicalId", params["physicalId"]}, {"instanceRevision", snapshot["instanceRevision"]}, {"limit", 1}})["instanceRevision"] != snapshot["instanceRevision"])
        throw ServiceError{"STALE_INSTANCE", "Workspace changed during update lookup."};
    // Inventory was already paged: preserve its cursor and total, including unknown files.
    return {{"updates", updates}, {"instanceRevision", snapshot["instanceRevision"]}, {"total", snapshot["total"]}, {"nextOffset", snapshot["nextOffset"]}};
}
void WorkspaceServices::recover(const QString& id)
{
    const auto saved = context(id); const auto request = saved["request"].toObject();
    if (saved["method"] == "workspaces.create") throw ServiceError{"RECOVERY_REQUIRED", "Interrupted workspace initialization requires inspection; no partial files were removed."};
    const auto record = workspaceRecord(request["physicalId"].toString()); auto lease = acquire(record);
    TransactionEngine engine(m_store, root(record), mods); requireResult(engine.recover(id, lease));
}
void WorkspaceServices::startupRecovery()
{
    for (const auto& value : m_store.all()) {
        const auto status = value.toObject(); const auto id = status["operationId"].toString(), state = status["state"].toString();
        if (state == "queued" && !QFileInfo::exists(operationDirectory(id) + "/context.json") && !QFileInfo::exists(m_profile + "/workspaces/" + id)) {
            requireResult(m_store.transition(id, "failed", {}, {{"code", "IO_ERROR"}, {"message", "Process exited before request context publication; no live work started."}, {"retryable", false}}));
            continue;
        }
        try {
            const auto saved = context(id); const auto request = saved["request"].toObject();
            if (saved["method"] == "workspaces.create") {
                if (status["terminal"].toBool()) continue;
                const auto path = operationDirectory(id) + "/created.json";
                if (state == "verifying" && QFileInfo::exists(path)) {
                    const auto created = readRecord(path), record = created["workspace"].toObject(); auto lease = acquire(record);
                    if (checkedHash(root(record) + "/instance.cfg") != created["instance.cfg"] || checkedHash(root(record) + "/mmc-pack.json") != created["mmc-pack.json"] ||
                        checkedHash(root(record) + "/.awake-integration/policies.json") != created["policies.json"] || !scan(record).isEmpty())
                        throw ServiceError{"RECOVERY_REQUIRED", "Interrupted creation contains unrecognized changes."};
                    writeRecord(m_profile + "/catalog/" + id + ".json", record);
                    requireResult(m_store.transition(id, "completed", {{"workspace", workspaceView(record)}, {"verified", true}})); continue;
                }
                if (QFileInfo::exists(m_profile + "/workspaces/" + id)) throw ServiceError{"RECOVERY_REQUIRED", "Partial workspace initialization was preserved for inspection."};
            } else {
                const auto record = workspaceRecord(request["physicalId"].toString());
                if (QFileInfo::exists(root(record) + "/.awake-integration/recovery.json") || QStringList{"acquiring", "revalidating", "backing_up", "applying", "verifying", "rolling_back", "recovery_required"}.contains(state)) { recover(id); continue; }
                if (state == "prepared" && !m_store.hasRequest(id, "transactions.apply")) {
                    if (status["cancellationRequested"].toBool()) requireResult(m_store.transition(id, "cancelled"));
                    continue;
                }
            }
            if (!status["terminal"].toBool()) requireResult(m_store.transition(id, "failed", {}, {{"code", "IO_ERROR"}, {"message", "Process exited before live Apply; prepare again."}, {"retryable", false}}));
        } catch (const ServiceError& error) {
            if (!status["terminal"].toBool()) m_store.transition(id, "recovery_required", {}, {{"code", error.code}, {"message", error.message.left(2048)}, {"retryable", false}});
        }
    }
}
QJsonObject WorkspaceServices::recoveryStatus()
{
    QJsonArray operations, blocked; QSet<QString> identities;
    for (const auto& value : m_store.all()) if (value.toObject()["state"] == "recovery_required") {
        const auto view = operation(value.toObject()["operationId"].toString()); operations.append(view);
        if (view["physicalId"].isString()) identities.insert(view["physicalId"].toString());
    }
    for (const auto& file : QDir(m_profile + "/catalog").entryInfoList({"*.json"}, QDir::Files)) {
        const auto record = readRecord(file.absoluteFilePath());
        if (QFileInfo::exists(root(record) + "/.awake-integration/recovery.json") ||
            !InstanceAuthority::validate(root(record), record["physicalId"].toString(), m_profile + "/ownership")) identities.insert(record["physicalId"].toString());
    }
    for (const auto& id : identities) blocked.append(id);
    return {{"operations", operations}, {"blockedWorkspaces", blocked}};
}
}
