// SPDX-License-Identifier: GPL-3.0-only
#include "WorkspaceServices.h"
#include "Contract.h"
#include "awake/integration/DurableIO.h"
#include "awake/integration/ProfileAccess.h"
#include "awake/integration/InstanceAuthority.h"
#include "awake/integration/TransactionEngine.h"
#include "modplatform/packwiz/Packwiz.h"
#include "ModMetadata.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QRunnable>
#include <QSet>
#include <QThreadPool>
#include <atomic>
#include <optional>
namespace Awake::Core {
using namespace Awake::Integration;
namespace {
QString providerId(const QJsonObject& value) { return value["provider"].toString("modrinth"); }
QString projectKey(const QString& provider, const QString& id) { return provider == "curseforge" ? provider + ':' + id : id; }
QString projectKey(const QJsonObject& row) { return projectKey(providerId(row), row["projectId"].toString()); }
constexpr auto mods = ".minecraft/mods";
// Sinytra Connector loads Fabric mods on Forge/NeoForge; Forgified Fabric API replaces Fabric API there.
constexpr auto connectorProject = "u58R1TMW", forgifiedFabricApiProject = "Aqlf1Shp", fabricApiProject = "P7dR8mSH";
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
void WorkspaceServices::prepare(const QString& id, const QJsonObject& request, const QJsonArray& overrides, const QJsonArray& importWarnings)
{
    requireResult(m_store.transition(id, "resolving"));
    const bool packAuthoritative = request["packAuthoritative"].toBool();
    m_provider.setPackAuthoritative(packAuthoritative); m_curseforge.setPackAuthoritative(packAuthoritative);
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
        if (row["projectId"].isString()) byProject[projectKey(row)] = row;
    }
    for (auto it = desired.begin(); it != desired.end(); ++it) {
        if (!byProject.contains(it.key())) throw ServiceError{"POLICY_CONFLICT", "A tracked file is missing. Repair the discrepancy before preparing changes."};
        auto entry = it.value().toObject(); entry["enabled"] = byProject[it.key()]["enabled"]; it.value() = entry;
    }
    QMap<QString, ResolvedVersion> selected; QMap<QString, ModPlatform::IndexedPack> packs;
    QStringList queue; QSet<QString> touched, removed, unknownRemovals, explicitProjects; QMap<QString, bool> unknownToggles;
    QJsonArray warnings = importWarnings, dependencies; QSet<QString> dependencyIds;
    const auto warn = [&](const QString& text) { if (warnings.size() < 128) warnings.append(text.left(1024)); };
    // Reviewed dependency resolutions (manager.v0.2.1): dependency project -> accepted version, or null for "proceed without".
    QMap<QString, QJsonValue> resolutions;
    for (const auto& value : request["dependencyResolutions"].toArray()) {
        const auto resolution = value.toObject(); const auto key = projectKey(providerId(resolution), resolution["projectId"].toString());
        if (resolutions.contains(key)) throw ServiceError{"INVALID_REQUEST", "A dependency resolution is repeated."};
        resolutions[key] = resolution["versionId"];
    }
    const auto choose = [&](const ResolvedVersion& version, const ModPlatform::IndexedPack& pack) {
        const auto project = projectKey(version.wire), versionId = version.wire["versionId"].toString();
        if (pack.addonId.toString() != version.wire["projectId"].toString() || !Contract().valid("version", version.wire)) throw ServiceError{"INTEGRITY_FAILED", "Provider version identity is invalid."};
        if (removed.contains(project)) throw ServiceError{"DEPENDENCY_CONFLICT", "A required dependency was explicitly removed: " + pack.name.left(256)};
        if (selected.contains(project)) {
            if (selected[project].wire["versionId"] != versionId) throw ServiceError{"DEPENDENCY_CONFLICT", "Two selections require different versions of " + pack.name.left(256) + " (" + project + ")."};
            return;
        }
        const auto old = byProject.value(project);
        if (!old.isEmpty() && old["metadataState"] != "verified") throw ServiceError{"POLICY_CONFLICT", "A tracked mod or its metadata was modified; existing bytes were preserved."};
        if (!old.isEmpty() && old["versionId"] != versionId && old["updateLocked"].toBool()) throw ServiceError{"POLICY_CONFLICT", "A native update lock, exclusion or intentional pin blocks this version change."};
        if (selected.size() >= 1024) throw ServiceError{"LIMIT_EXCEEDED", "Dependency resolution exceeds 1,024 projects."};
        selected[project] = version; packs[project] = pack; queue.append(project);
        auto entry = desired.value(project).toObject();
        // Accepted resolutions belong to one requester version; a different version must be reviewed again.
        if (entry["version"].toObject()["versionId"] != versionId) entry.remove("dependencyResolutions");
        entry["version"] = version.wire;
        if (version.bridge.isEmpty()) entry.remove("loaderBridge"); else entry["loaderBridge"] = version.bridge;
        entry["enabled"] = entry.contains("enabled") ? entry["enabled"] : QJsonValue(true); desired[project] = entry;
    };
    for (const auto& value : request["changes"].toArray()) {
        m_provider.checkCancelled(); const auto change = value.toObject(); const auto action = change["action"].toString();
        if (action == "install" || action == "replace") {
            const auto provider = providerId(change);
            const auto pack = provider == "curseforge" ? m_curseforge.project(change["projectId"].toString()) : m_provider.project(change["projectId"].toString());
            const auto project = projectKey(provider, pack.addonId.toString());
            if (touched.contains(project) || (action == "install" && byProject.contains(project)) || (action == "replace" && !byProject.contains(project)))
                throw ServiceError{"POLICY_CONFLICT", "Conflicting install/replace actions for this project."};
            if (provider == "curseforge" && change.contains("artifactSha512")) throw ServiceError{"INVALID_REQUEST", "CurseForge files are selected by file ID."};
            // "loaderBridge" is set only internally by package import for an exact, hash-identified file;
            // client requests are schema-validated and cannot carry it.
            const bool bridge = provider == "modrinth" && change["loaderBridge"] == "sinytra-connector" && change.contains("artifactSha512");
            touched.insert(project); explicitProjects.insert(project);
            choose(provider == "curseforge" ? m_curseforge.version(change["projectId"].toString(), change["versionId"].toString(), game, loaderId) : m_provider.version(change["versionId"].toString(), game, loaderId, change["artifactSha512"].toString(), bridge), pack);
        } else {
            const auto inventoryId = change["inventoryId"].toString(); const auto row = byInventory.value(inventoryId);
            if (row.isEmpty()) throw ServiceError{"STALE_INSTANCE", "An inventory action no longer identifies a mod."};
            const auto project = row["projectId"].isString() ? projectKey(row) : QString{}, key = project.isEmpty() ? inventoryId : project;
            if (touched.contains(key)) throw ServiceError{"POLICY_CONFLICT", "More than one action targets the same mod."}; touched.insert(key);
            if (!project.isEmpty() && row["metadataState"] != "verified") throw ServiceError{"POLICY_CONFLICT", "Modified tracked data requires repair before mutation."};
            if (action == "policy") {
                if (project.isEmpty()) throw ServiceError{"POLICY_CONFLICT", "Only provider-identified mods can be pinned or excluded."};
                if (change["pinnedVersion"].isString() && change["pinnedVersion"] != row["versionId"]) throw ServiceError{"POLICY_CONFLICT", "A pin must identify the installed version."};
                auto entry = desired.value(project).toObject(); entry["pinnedVersion"] = change["pinnedVersion"]; entry["excluded"] = change["excluded"]; desired[project] = entry;
            } else if (action == "remove") {
                if (project.isEmpty()) unknownRemovals.insert(inventoryId);
                else { removed.insert(project); desired.remove(project); }
            } else {
                const bool enabled = action == "enable";
                if (project.isEmpty()) unknownToggles[inventoryId] = enabled;
                else { auto entry = desired.value(project).toObject(); entry["enabled"] = enabled; desired[project] = entry; }
            }
        }
    }
    const auto splitKey = [](const QString& key) { return key.startsWith("curseforge:") ? QPair<QString, QString>{"curseforge", key.mid(11)} : QPair<QString, QString>{"modrinth", key}; };
    // A reviewed "use this version" resolution selects that version before dependencies are expanded,
    // so every provider pin naming another version of the project is then evaluated against it.
    for (auto it = resolutions.begin(); it != resolutions.end(); ++it) {
        m_provider.checkCancelled();
        if (!it.value().isString()) continue;
        const auto split = splitKey(it.key()); const auto versionId = it.value().toString();
        if (selected.contains(it.key())) {
            if (selected[it.key()].wire["versionId"] != versionId) throw ServiceError{"INVALID_REQUEST", "A dependency resolution names a different version than the selected package file."};
            continue;
        }
        if (desired.value(it.key()).toObject()["version"].toObject()["versionId"] == versionId) continue;
        choose(split.first == "curseforge" ? m_curseforge.version(split.second, versionId, game, loaderId) : m_provider.version(versionId, game, loaderId),
            split.first == "curseforge" ? m_curseforge.project(split.second) : m_provider.project(split.second));
    }
    for (int cursor = 0; cursor < queue.size() && !packAuthoritative; ++cursor) {
        const auto project = queue[cursor]; const auto provider = providerId(selected[project].wire); const bool bridged = !selected[project].bridge.isEmpty();
        for (const auto& value : selected[project].wire["dependencies"].toArray()) {
            m_provider.checkCancelled(); const auto dep = value.toObject(); const auto type = dep["type"].toString();
            if (type != "required") { warn(type + " dependency from " + project + ": " + dep["projectId"].toString() + " " + dep["versionId"].toString()); continue; }
            auto depProject = dep["projectId"].toString(), depVersion = dep["versionId"].toString();
            // Under Connector, Forgified Fabric API provides Fabric API; the Fabric API pin itself never applies.
            if (bridged && provider == "modrinth" && depProject == fabricApiProject) { depProject = forgifiedFabricApiProject; depVersion.clear(); }
            if (depProject.isEmpty() && !depVersion.isEmpty() && provider == "modrinth") depProject = m_provider.versionInfo(depVersion, game, loaderId)["projectId"].toString();
            if (depProject.isEmpty()) throw ServiceError{"DEPENDENCY_CONFLICT", "Required dependency has no resolvable project or version."};
            const auto depKey = projectKey(provider, depProject);
            if (removed.contains(depKey)) throw ServiceError{"DEPENDENCY_CONFLICT", "A required dependency was explicitly removed."};
            const auto current = desired.value(depKey).toObject();
            if (!current.isEmpty() && !current["enabled"].toBool(true)) throw ServiceError{"DEPENDENCY_CONFLICT", "A required dependency is disabled; enable it explicitly."};
            if (!current.isEmpty()) {
                if (byProject.contains(depKey) && !selected.contains(depKey) && byProject[depKey]["metadataState"] != "verified")
                    throw ServiceError{"POLICY_CONFLICT", "A required dependency has modified bytes or metadata."};
                // An installed dependency follows a requester's pin only forward (a newer-published build) and only
                // when nothing else in this plan selects it. Downgrades and every other mismatch go to review.
                const auto currentVersion = current["version"].toObject()["versionId"].toString();
                if (!depVersion.isEmpty() && depVersion != currentVersion && byProject.contains(depKey) && !selected.contains(depKey) &&
                    !byProject[depKey]["updateLocked"].toBool() && !resolutions.contains(depKey)) {
                    std::optional<ResolvedVersion> pinnedVersion, installedVersion;
                    try {
                        pinnedVersion = provider == "curseforge" ? m_curseforge.version(depProject, depVersion, game, loaderId) : m_provider.version(depVersion, game, loaderId);
                        installedVersion = provider == "curseforge" ? m_curseforge.version(depProject, currentVersion, game, loaderId) : m_provider.version(currentVersion, game, loaderId);
                    } catch (const ServiceError& error) { if (error.code != "DEPENDENCY_CONFLICT") throw; }
                    if (pinnedVersion && installedVersion && pinnedVersion->wire["projectId"] == depProject && pinnedVersion->publishedAt > installedVersion->publishedAt) {
                        const auto pack = provider == "curseforge" ? m_curseforge.project(depProject) : m_provider.project(depProject);
                        choose(*pinnedVersion, pack);
                        warn(QString("%1 is updated to %2 because %3 requires it.").arg(pack.name.left(256), pinnedVersion->wire["versionNumber"].toString().left(128), packs.value(project).name.left(256)));
                    }
                }
            } else if (!bridged && !(resolutions.contains(depKey) && resolutions[depKey].isNull())) {
                // Absent: install the exact provider pin when it supports this workspace, else the newest compatible release.
                std::optional<ResolvedVersion> resolved;
                if (!depVersion.isEmpty()) {
                    try { resolved = provider == "curseforge" ? m_curseforge.version(depProject, depVersion, game, loaderId) : m_provider.version(depVersion, game, loaderId); }
                    catch (const ServiceError& error) { if (error.code != "DEPENDENCY_CONFLICT") throw; }
                    if (resolved && resolved->wire["projectId"] != depProject) throw ServiceError{"DEPENDENCY_CONFLICT", "Dependency identity mismatch."};
                }
                if (!resolved) {
                    const QJsonObject query{{"projectId", depProject}, {"minecraftVersion", game}, {"loader", loaderId}, {"limit", 200}};
                    const auto choices = (provider == "curseforge" ? m_curseforge.versions(query) : m_provider.versions(query))["versions"].toArray();
                    if (!choices.isEmpty()) {
                        auto choice = choices.first().toObject();
                        for (const auto& candidate : choices) if (candidate.toObject()["releaseType"] == "release") { choice = candidate.toObject(); break; }
                        resolved = provider == "curseforge" ? m_curseforge.version(depProject, choice["versionId"].toString(), game, loaderId) : m_provider.version(choice["versionId"].toString(), game, loaderId);
                    }
                }
                // No compatible version: the graph check reports it with loader-metadata evidence.
                if (resolved) choose(*resolved, provider == "curseforge" ? m_curseforge.project(depProject) : m_provider.project(depProject));
            }
            if (!desired.contains(depKey)) continue;
            const auto chosenVersion = desired.value(depKey).toObject()["version"].toObject()["versionId"].toString();
            if (!dependencyIds.contains(depKey + ':' + chosenVersion)) {
                dependencyIds.insert(depKey + ':' + chosenVersion);
                QJsonObject dependency{{"projectId", depProject}, {"versionId", chosenVersion}, {"type", "required"}};
                if (provider != "modrinth") dependency["provider"] = provider;
                dependencies.append(dependency);
            }
        }
    }
    // Check the entire resulting active graph, including existing mods affected by removals or version changes.
    // Every violation is collected so the review can show all of them at once, with evidence.
    struct PendingConflict { QString kind, dependency; QList<QPair<QString, QString>> requests; };
    struct Acceptance { QString requester, dependency, pinned; QJsonValue accepted; bool reviewedNow; };
    QMap<QString, PendingConflict> pending; QStringList pendingOrder; QList<Acceptance> acceptances;
    const auto flag = [&](const QString& kind, const QString& dependency, const QString& requester, const QString& pinned) {
        const auto key = kind + '|' + dependency;
        if (!pending.contains(key)) { pending[key] = {kind, dependency, {}}; pendingOrder.append(key); }
        for (const auto& existing : pending[key].requests) if (existing.first == requester) return;
        pending[key].requests.append({requester, pinned});
    };
    const auto enabled = [&](const QString& key) { return desired.contains(key) && desired.value(key).toObject()["enabled"].toBool(true); };
    for (auto it = desired.begin(); it != desired.end() && !packAuthoritative; ++it) {
        const auto entry = it.value().toObject(); if (!entry["enabled"].toBool(true)) continue;
        const auto version = entry["version"].toObject(); const auto provider = providerId(version);
        const bool bridged = !entry["loaderBridge"].toString().isEmpty(); const auto persisted = entry["dependencyResolutions"].toObject();
        if (bridged && !enabled(connectorProject)) flag("loader_bridge", connectorProject, it.key(), {});
        for (const auto& value : version["dependencies"].toArray()) {
            const auto dep = value.toObject(); const auto type = dep["type"].toString();
            if (type != "required" && type != "incompatible") continue;
            auto depProject = dep["projectId"].isString() ? dep["projectId"].toString() : QString{}; auto pinned = dep["versionId"].toString();
            if (bridged && provider == "modrinth" && depProject == fabricApiProject) { if (type == "incompatible") continue; depProject = forgifiedFabricApiProject; pinned.clear(); }
            const auto project = depProject.isEmpty() ? QString{} : projectKey(provider, depProject);
            bool match = false, present = false; QString presentVersion;
            for (auto other = desired.begin(); other != desired.end(); ++other) {
                const auto target = other.value().toObject();
                if (providerId(target["version"].toObject()) != provider) continue;
                if ((!project.isEmpty() && project != other.key()) || !target["enabled"].toBool(true)) continue;
                if (project.isEmpty() && pinned.isEmpty()) continue;
                if (!pinned.isEmpty() && target["version"].toObject()["versionId"] != pinned) {
                    if (!project.isEmpty()) { present = true; presentVersion = target["version"].toObject()["versionId"].toString(); }
                    continue;
                }
                match = true; break;
            }
            if (type == "incompatible") {
                if (match) flag("incompatible", project.isEmpty() ? (provider == "modrinth" ? m_provider.versionInfo(pinned, game, loaderId)["projectId"].toString() : pinned) : project, it.key(), pinned);
                continue;
            }
            if (match) continue;
            if (project.isEmpty()) throw ServiceError{"DEPENDENCY_CONFLICT", "Resulting mod graph violates a declared version-only dependency for " + it.key()};
            std::optional<QJsonValue> accepted; bool reviewedNow = false;
            if (resolutions.contains(project)) { accepted = resolutions[project]; reviewedNow = true; }
            else if (persisted.contains(project)) accepted = persisted[project].toObject()["versionId"];
            if (present && accepted && accepted->isString() && accepted->toString() == presentVersion) { acceptances.append({it.key(), project, pinned, *accepted, reviewedNow}); continue; }
            if (!present && accepted && accepted->isNull()) { acceptances.append({it.key(), project, pinned, QJsonValue(QJsonValue::Null), reviewedNow}); continue; }
            flag(present ? "version_pin" : "missing_dependency", project, it.key(), pinned);
        }
    }
    // Evidence: compare provider claims with the loader metadata inside the verified artifacts.
    int evidenceDownloads = 0;
    const auto artifactName = [](const QJsonObject& artifact) { return artifact["sha512"].toString().isEmpty() ? artifact["sha1"].toString() : artifact["sha512"].toString(); };
    const auto fetchEvidence = [&](const QString& provider, const QJsonObject& artifact) -> QString {
        const auto name = artifactName(artifact); if (name.isEmpty()) return {};
        const auto path = directory + "/evidence/" + name;
        if (QFileInfo::exists(path)) return path;
        if (evidenceDownloads >= 96) return {};
        ++evidenceDownloads; privateDirectory(directory + "/evidence");
        try { if (provider == "curseforge") m_curseforge.download(artifact, path); else m_provider.download(artifact, path); }
        catch (const ServiceError& error) { if (error.code == "CANCELLED") throw; return {}; }
        return path;
    };
    const auto artifactPath = [&](const QString& key) -> QString {
        const auto old = byProject.value(key);
        if (selected.contains(key) && (old.isEmpty() || old["versionId"] != selected[key].wire["versionId"]))
            return fetchEvidence(providerId(selected[key].wire), selected[key].wire["files"].toArray().first().toObject());
        if (!old.isEmpty() && old["metadataState"] == "verified") return instanceRoot + '/' + mods + '/' + old["fileName"].toString();
        return {};
    };
    QMap<QString, QString> titles; int lookups = 0;
    const auto title = [&](const QString& key) -> QString {
        if (packs.contains(key) && !packs[key].name.isEmpty()) return packs[key].name.left(256);
        if (titles.contains(key)) return titles[key];
        const auto split = splitKey(key); QString name = split.second;
        if (lookups++ < 128) {
            try { const auto pack = split.first == "curseforge" ? m_curseforge.project(split.second) : m_provider.project(split.second); if (!pack.name.isEmpty()) name = pack.name.left(256); }
            catch (const ServiceError& error) { if (error.code == "CANCELLED") throw; }
        }
        return titles[key] = name;
    };
    QMap<QString, QJsonObject> pinLabels;
    const auto pinLabel = [&](const QString& key, const QString& versionId) -> QJsonObject {
        if (versionId.isEmpty()) return {};
        if (pinLabels.contains(versionId)) return pinLabels[versionId];
        QJsonObject label{{"versionId", versionId}, {"versionNumber", versionId}, {"compatible", false}};
        if (splitKey(key).first == "modrinth" && pinLabels.size() < 128) {
            try { label = m_provider.versionInfo(versionId, game, loaderId); } catch (const ServiceError& error) { if (error.code == "CANCELLED") throw; }
        }
        return pinLabels[versionId] = label;
    };
    QMap<QString, QString> missingArtifacts;
    const auto dependencyArtifact = [&](const QString& kind, const QString& key) -> QString {
        if (kind != "missing_dependency") return artifactPath(key);
        if (missingArtifacts.contains(key)) return missingArtifacts[key];
        const auto split = splitKey(key); QString path;
        if (split.first == "modrinth") {
            try { const auto artifact = m_provider.anyArtifact(split.second, game); if (!artifact.isEmpty()) path = fetchEvidence(split.first, artifact); }
            catch (const ServiceError& error) { if (error.code == "CANCELLED") throw; }
        }
        return missingArtifacts[key] = path;
    };
    const auto unknownEvidence = [] { return QJsonObject{{"result", "unknown"}, {"modId", QJsonValue::Null}, {"declaredRange", QJsonValue::Null}, {"mandatory", QJsonValue::Null}, {"selectedModVersion", QJsonValue::Null}}; };
    const auto evaluate = [&](const QString& kind, const QString& requester, const QString& dependency) -> QJsonObject {
        auto evidence = unknownEvidence();
        const auto requesterPath = artifactPath(requester), dependencyPath = dependencyArtifact(kind, dependency);
        if (requesterPath.isEmpty() || dependencyPath.isEmpty()) return evidence;
        const auto bridge = desired.value(requester).toObject()["loaderBridge"].toString();
        const auto requesterMetadata = inspectJar(requesterPath, bridge.isEmpty() ? loaderId : QStringLiteral("fabric"));
        const auto dependencyMetadata = inspectJar(dependencyPath, loaderId);
        if (!requesterMetadata.readable || !dependencyMetadata.readable) return evidence;
        std::optional<DeclaredDependency> declared; QString modId, modVersion;
        for (const auto& candidate : requesterMetadata.dependencies) for (const auto& mod : dependencyMetadata.mods)
            if (candidate.modId == mod.first && (!declared || (candidate.mandatory && !declared->mandatory))) { declared = candidate; modId = mod.first; modVersion = mod.second; }
        if (!declared) {
            evidence["result"] = "not_declared"; evidence["modId"] = dependencyMetadata.mods.first().first.left(128);
            if (!dependencyMetadata.mods.first().second.isEmpty() && kind != "missing_dependency") evidence["selectedModVersion"] = dependencyMetadata.mods.first().second.left(128);
            return evidence;
        }
        evidence["modId"] = modId.left(128); evidence["mandatory"] = declared->mandatory;
        evidence["declaredRange"] = declared->range.isEmpty() ? QStringLiteral("*") : declared->range.left(256);
        if (kind == "missing_dependency") { evidence["result"] = declared->mandatory ? "violated" : "not_declared"; return evidence; }
        if (!modVersion.isEmpty()) evidence["selectedModVersion"] = modVersion.left(128);
        const auto result = satisfies(requesterMetadata.format, declared->range, modVersion);
        evidence["result"] = result == RangeResult::Satisfied ? "satisfied" : result == RangeResult::Violated ? "violated" : "unknown";
        return evidence;
    };
    // Resolutions reviewed in this request must still agree with the loader metadata. A pin may be kept unless
    // metadata rejects the version; a provider requirement may be skipped only when metadata does not declare it.
    QMap<QString, QJsonObject> acceptedEvidence;
    for (const auto& acceptance : acceptances) {
        m_provider.checkCancelled();
        if (!acceptance.reviewedNow) continue;
        const auto kind = acceptance.accepted.isNull() ? QStringLiteral("missing_dependency") : QStringLiteral("version_pin");
        const auto evidence = evaluate(kind, acceptance.requester, acceptance.dependency); const auto result = evidence["result"].toString();
        if (result == "violated" || (kind == "missing_dependency" && result != "not_declared")) flag(kind, acceptance.dependency, acceptance.requester, acceptance.pinned);
        else acceptedEvidence[acceptance.requester + '|' + acceptance.dependency] = evidence;
    }
    if (!pending.isEmpty()) {
        QJsonArray conflicts; QStringList summary;
        for (const auto& key : pendingOrder) {
            m_provider.checkCancelled();
            if (conflicts.size() >= 64) break;
            const auto& item = pending[key]; const auto split = splitKey(item.dependency); const auto dependencyTitle = title(item.dependency);
            const auto target = desired.value(item.dependency).toObject()["version"].toObject();
            QJsonValue chosen(QJsonValue::Null);
            if ((item.kind == "version_pin" || item.kind == "incompatible") && !target.isEmpty())
                chosen = QJsonObject{{"versionId", target["versionId"]}, {"versionNumber", target["versionNumber"].toString().left(256)}, {"fileName", target["files"].toArray().first().toObject()["fileName"]}};
            QJsonArray requests, candidates; QSet<QString> candidateIds; QStringList requesterNames; bool reviewable = true;
            if (chosen.isObject() && item.kind == "version_pin") { candidates.append(QJsonObject{{"versionId", target["versionId"]}, {"versionNumber", target["versionNumber"].toString().left(256)}}); candidateIds.insert(target["versionId"].toString()); }
            for (const auto& request : item.requests) {
                if (requests.size() >= 32) break;
                const auto& requester = request.first; const auto& pinned = request.second;
                const auto version = desired.value(requester).toObject()["version"].toObject(); const auto label = pinLabel(item.dependency, pinned);
                const auto evidence = item.kind == "loader_bridge" || item.kind == "incompatible" ? unknownEvidence() : evaluate(item.kind, requester, item.dependency);
                const auto result = evidence["result"].toString();
                if (item.kind == "version_pin" && result == "violated") reviewable = false;
                if (item.kind == "missing_dependency" && result != "not_declared") reviewable = false;
                const auto requesterTitle = title(requester); requesterNames.append(requesterTitle);
                requests.append(QJsonObject{{"provider", providerId(version)}, {"projectId", version["projectId"]}, {"title", requesterTitle}, {"versionId", version["versionId"]},
                    {"versionNumber", version["versionNumber"].toString().left(256)}, {"fileName", version["files"].toArray().first().toObject()["fileName"]},
                    {"requestedVersionId", pinned.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(pinned)},
                    {"requestedVersionNumber", label.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(label["versionNumber"].toString().left(256))}, {"evidence", evidence}});
                if (item.kind == "version_pin" && !explicitProjects.contains(item.dependency) && !label.isEmpty() && label["compatible"].toBool() &&
                    !candidateIds.contains(pinned) && candidates.size() < 8) { candidateIds.insert(pinned); candidates.append(QJsonObject{{"versionId", pinned}, {"versionNumber", label["versionNumber"].toString().left(256)}}); }
            }
            QJsonArray offered; QString message; const bool one = requesterNames.size() == 1;
            const auto names = requesterNames.mid(0, 3).join(", ") + (requesterNames.size() > 3 ? QString(" and %1 more").arg(requesterNames.size() - 3) : QString{});
            if (item.kind == "version_pin") {
                if (reviewable) offered.append("use_version");
                message = QString("%1 %2 a different %3 version than the selected %4.").arg(names, one ? "pins" : "pin", dependencyTitle, target["versionNumber"].toString().left(128));
                message += reviewable ? " Loader metadata does not rule out the selected version; review it, then keep it or choose another listed version."
                                      : " At least one mod's loader metadata rejects the selected version.";
            } else if (item.kind == "missing_dependency") {
                if (reviewable) offered.append("proceed_without");
                message = QString("%1 %2 %3, but no %3 version supports Minecraft %4 with %5.").arg(names, one ? "requires" : "require", dependencyTitle, game, loaderId);
                message += reviewable ? " The mod's own loader metadata does not declare it, so the provider requirement does not appear to apply to this loader."
                                      : " The requirement cannot be ruled out from the mod's loader metadata.";
            } else if (item.kind == "loader_bridge") {
                message = QString("%1 %2 Fabric-only and can load here only through Sinytra Connector, which is not enabled. Add Connector (with Forgified Fabric API) or leave %3 out.")
                    .arg(names, one ? "is" : "are", one ? "it" : "them");
            } else {
                message = QString("%1 %2 %3 incompatible, and both would be enabled.").arg(names, one ? "declares" : "declare", dependencyTitle);
            }
            conflicts.append(QJsonObject{{"kind", item.kind}, {"provider", split.first}, {"projectId", split.second}, {"title", dependencyTitle}, {"selected", chosen},
                {"requests", requests}, {"candidates", candidates}, {"resolutions", offered}, {"message", message.left(1024)}});
            summary.append(dependencyTitle + " (" + requesterNames.mid(0, 2).join(", ") + (requesterNames.size() > 2 ? ", ..." : "") + ")");
        }
        // Keep details well inside one 1 MiB protocol frame: drop trailing requests, then trailing conflicts.
        bool truncated = pending.size() > conflicts.size();
        const auto encodedSize = [&] { return QJsonDocument(QJsonObject{{"conflicts", conflicts}}).toJson(QJsonDocument::Compact).size(); };
        while (encodedSize() > 192 * 1024 && !conflicts.isEmpty()) {
            truncated = true; auto last = conflicts.last().toObject(); auto requests = last["requests"].toArray();
            if (requests.size() > 1) { requests.removeLast(); last["requests"] = requests; conflicts.replace(conflicts.size() - 1, last); }
            else if (conflicts.size() > 1) conflicts.removeLast();
            else break;
        }
        const QJsonObject details{{"conflicts", conflicts}, {"truncated", truncated}};
        if (!Contract().valid("conflictDetails", details)) throw ServiceError{"DEPENDENCY_CONFLICT", "Dependency review is required; conflict details exceeded the contract."};
        throw ServiceError{"DEPENDENCY_CONFLICT", QString("Dependency review required (%1): %2").arg(pending.size()).arg(summary.join("; ")).left(2048), false, details};
    }
    // Record accepted resolutions on the requester's entry so later plans keep honouring them for that version.
    QJsonArray reviewed; QMap<QString, int> reviewedIndex;
    for (const auto& acceptance : acceptances) {
        auto entry = desired.value(acceptance.requester).toObject(); auto stored = entry["dependencyResolutions"].toObject();
        const auto evidenceKey = acceptance.requester + '|' + acceptance.dependency;
        const auto evidence = acceptedEvidence.contains(evidenceKey) ? acceptedEvidence[evidenceKey]["result"].toString() : stored[acceptance.dependency].toObject()["evidence"].toString("unknown");
        stored[acceptance.dependency] = QJsonObject{{"versionId", acceptance.accepted}, {"requestedVersionId", acceptance.pinned.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(acceptance.pinned)}, {"evidence", evidence}};
        entry["dependencyResolutions"] = stored; desired[acceptance.requester] = entry;
        if (!acceptance.reviewedNow) continue;
        const auto split = splitKey(acceptance.dependency); const auto requester = desired.value(acceptance.requester).toObject()["version"].toObject();
        if (!reviewedIndex.contains(acceptance.dependency)) {
            reviewedIndex[acceptance.dependency] = reviewed.size();
            QJsonObject row{{"projectId", split.second}, {"title", title(acceptance.dependency)}, {"versionId", acceptance.accepted}, {"requestedBy", QJsonArray{}}};
            if (split.first != "modrinth") row["provider"] = split.first;
            reviewed.append(row);
        }
        auto row = reviewed[reviewedIndex[acceptance.dependency]].toObject(); auto by = row["requestedBy"].toArray();
        if (by.size() < 32) by.append(QJsonObject{{"projectId", requester["projectId"]}, {"title", title(acceptance.requester)},
            {"requestedVersionId", acceptance.pinned.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(acceptance.pinned)}, {"evidence", evidence}});
        row["requestedBy"] = by; reviewed.replace(reviewedIndex[acceptance.dependency], row);
        const auto targetVersion = desired.value(acceptance.dependency).toObject()["version"].toObject()["versionNumber"].toString();
        warn(acceptance.accepted.isNull()
            ? QString("Reviewed: %1 is installed without %2 (the provider requirement is not declared in its loader metadata).").arg(title(acceptance.requester), title(acceptance.dependency))
            : QString("Reviewed: %1 keeps %2 %3 instead of its provider pin (evidence: %4).").arg(title(acceptance.requester), title(acceptance.dependency), targetVersion, evidence));
    }
    for (auto it = desired.begin(); it != desired.end(); ++it)
        if (it.value().toObject()["loaderBridge"] == "sinytra-connector" && selected.contains(it.key()))
            warn(QString("%1 is a Fabric mod and will load through Sinytra Connector.").arg(title(it.key())));
    QSet<QString> destinations, indexes;
    for (auto it = desired.begin(); it != desired.end(); ++it) {
        const auto entry = it.value().toObject();
        const auto filename = selected.contains(it.key()) ? selected[it.key()].wire["files"].toArray().first().toObject()["fileName"].toString() : entry["fileName"].toString();
        const auto index = selected.contains(it.key()) ? providerId(selected[it.key()].wire) + '-' + selected[it.key()].wire["projectId"].toString() + ".pw.toml" : entry["indexFile"].toString();
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
    // Fetch into the verified evidence store in parallel; the sequential staging below copies from it.
    struct Fetch { QString provider; QJsonObject artifact; QString path; };
    QList<Fetch> fetches;
    for (auto it = selected.begin(); it != selected.end(); ++it) {
        const auto old = byProject.value(it.key()); const auto wire = it.value().wire;
        if (!old.isEmpty() && old["versionId"] == wire["versionId"]) continue;
        const auto artifact = wire["files"].toArray().first().toObject(); const auto name = artifact["sha512"].toString().isEmpty() ? artifact["sha1"].toString() : artifact["sha512"].toString();
        if (!name.isEmpty() && !QFileInfo::exists(directory + "/evidence/" + name)) fetches.append({providerId(wire), artifact, directory + "/evidence/" + name});
    }
    if (fetches.size() > 1 && !m_provider.hasFixture()) {
        privateDirectory(directory + "/evidence");
        QThreadPool pool; pool.setMaxThreadCount(8);
        auto cancel = m_provider.cancelToken(); QMutex lock; std::optional<ServiceError> failure; std::atomic_int finished{0};
        for (const auto& fetch : fetches) {
            pool.start(QRunnable::create([&, fetch] {
                { QMutexLocker guard(&lock); if (failure) return; }
                try {
                    if (fetch.provider == "curseforge") CurseForgeService(cancel).download(fetch.artifact, fetch.path);
                    else ModrinthService(cancel).download(fetch.artifact, fetch.path);
                    ++finished;
                } catch (const ServiceError& error) { QMutexLocker guard(&lock); if (!failure) failure = error; }
            }));
        }
        while (!pool.waitForDone(250)) requireResult(m_store.note(id, {{"phase", "downloading"}, {"completed", QString::number(finished.load())}, {"total", QString::number(fetches.size())}, {"unit", "files"}}));
        if (failure) throw *failure;
    }
    for (auto it = selected.begin(); it != selected.end(); ++it) {
        m_provider.checkCancelled(); const auto project = it.key(); auto entry = desired.value(project).toObject(); const auto old = byProject.value(project);
        const auto wire = it.value().wire;
        if (!old.isEmpty() && old["versionId"] == wire["versionId"]) continue;
        const auto artifact = wire["files"].toArray().first().toObject(); const auto filename = artifact["fileName"].toString();
        if (QFileInfo(filename).fileName() != filename || filename.contains('\\') || !filename.endsWith(".jar", Qt::CaseInsensitive)) throw ServiceError{"INTEGRITY_FAILED", "Unsupported artifact filename."};
        auto target = filename + (entry["enabled"].toBool(true) ? "" : ".disabled");
        for (const auto& suffix : QStringList{"", ".disabled"}) {
            const auto collision = files.value((filename + suffix).toCaseFolded());
            if (!collision.isEmpty() && projectKey(collision) != project) throw ServiceError{"POLICY_CONFLICT", "An artifact would overwrite an unrelated or unknown file. Remove it in a separate reviewed operation first."};
        }
        if (!old.isEmpty() && old["fileName"].toString().compare(target, Qt::CaseInsensitive) == 0) target = old["fileName"].toString();
        const auto artifactName = "artifact-" + QString::number(++ordinal);
        const auto evidenceCopy = directory + "/evidence/" + (artifact["sha512"].toString().isEmpty() ? artifact["sha1"].toString() : artifact["sha512"].toString());
        if (QFileInfo::exists(evidenceCopy)) copy(evidenceCopy, directory + "/staging/" + artifactName); // hash-verified when fetched for evidence
        else if (providerId(wire) == "curseforge") m_curseforge.download(artifact, directory + "/staging/" + artifactName);
        else m_provider.download(artifact, directory + "/staging/" + artifactName);
        auto pack = packs[project]; pack.slug = providerId(wire) + '-' + wire["projectId"].toString();
        auto nativeVersion = it.value().native;
        auto native = Packwiz::V1::createModFormat(QDir(directory + "/native-index"), pack, nativeVersion);
        native.lockUpdate = originalPolicies["projects"].toObject()[project].toObject()["updateLocked"].toBool();
        Packwiz::V1::updateModIndex(QDir(directory + "/native-index"), native);
        const auto indexFile = pack.slug + ".pw.toml", indexPath = directory + "/native-index/" + indexFile;
        const auto parsed = Packwiz::V1::getIndexForMod(QDir(directory + "/native-index"), pack.slug);
        if (!parsed.isValid() || parsed.projectId != native.projectId || parsed.fileId != native.fileId || parsed.filename != filename || parsed.hash != native.hash)
            throw ServiceError{"INTEGRITY_FAILED", "Native mod metadata serialization did not verify."};
        const auto indexName = "index-" + QString::number(ordinal); copy(indexPath, directory + "/staging/" + indexName);
        if (!old.isEmpty() && old["fileName"] != target) remove(QString(mods) + '/' + old["fileName"].toString());
        stage(QString(mods) + '/' + target, artifactName); stage(QString(mods) + "/.index/" + indexFile, indexName);
        entry["projectId"] = wire["projectId"]; if (providerId(wire) != "modrinth") entry["provider"] = providerId(wire); entry["versionId"] = wire["versionId"]; entry["versionNumber"] = wire["versionNumber"];
        entry["fileName"] = filename; entry["sha256"] = checkedHash(directory + "/staging/" + artifactName);
        entry["indexFile"] = indexFile; entry["indexSha256"] = checkedHash(directory + "/staging/" + indexName);
        entry["updateLocked"] = originalPolicies["projects"].toObject()[project].toObject()["updateLocked"].toBool(); desired[project] = entry;
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
    auto trackedOverrides = originalPolicies["overrides"].toObject();
    for (const auto& value : overrides) {
        const auto file = value.toObject(); const auto path = file["path"].toString();
        const auto before = checkedHash(instanceRoot + '/' + path), after = file["sha256"].toString();
        if (changes.contains(path.toCaseFolded())) throw ServiceError{"POLICY_CONFLICT", "An override collides with a selected mod or metadata."};
        if (before != "missing" && before != after && (trackedOverrides[path].toString().isEmpty() || trackedOverrides[path] != before))
            throw ServiceError{"POLICY_CONFLICT", "An override would replace unmanaged or locally modified bytes: " + path};
        if (before == after) continue;
        const auto name = "override-" + QString::number(++ordinal);
        copy(directory + "/import/" + file["stagedName"].toString(), directory + "/staging/" + name);
        if (checkedHash(directory + "/staging/" + name) != after) throw ServiceError{"INTEGRITY_FAILED", "Override staging changed."};
        stage(path, name); trackedOverrides[path] = after;
    }
    if (!trackedOverrides.isEmpty()) finalPolicies["overrides"] = trackedOverrides;

    // A pack update may move to a newer build of the same loader; the workspace metadata moves with it.
    if (request.contains("newLoaderVersion")) {
        QFile pack(instanceRoot + "/mmc-pack.json");
        if (!pack.open(QIODevice::ReadOnly)) throw ServiceError{"IO_ERROR", "Instance metadata cannot be read."};
        auto root = QJsonDocument::fromJson(pack.readAll()).object(); pack.close();
        const QMap<QString, QString> uids{{"forge", "net.minecraftforge"}, {"neoforge", "net.neoforged"}, {"fabric", "net.fabricmc.fabric-loader"}, {"quilt", "org.quiltmc.quilt-loader"}};
        auto components = root["components"].toArray(); bool updated = false;
        for (int i = 0; i < components.size(); ++i) {
            auto component = components[i].toObject();
            if (component["uid"] == uids.value(loaderId)) { component["version"] = request["newLoaderVersion"]; components[i] = component; updated = true; }
        }
        if (!updated) throw ServiceError{"POLICY_CONFLICT", "This project's loader could not be found in its metadata."};
        root["components"] = components;
        QString error; if (!DurableIO::write(directory + "/staging/mmc-pack", QJsonDocument(root).toJson(QJsonDocument::Compact), &error)) throw ServiceError{"IO_ERROR", error};
        stage("mmc-pack.json", "mmc-pack");
        auto updatedRecord = record; auto updatedLoader = loader; updatedLoader["version"] = request["newLoaderVersion"]; updatedRecord["loader"] = updatedLoader;
        writeRecord(directory + "/loader-update.json", updatedRecord);
    }
    if (finalPolicies != originalPolicies) {
        QString error; if (!DurableIO::write(directory + "/staging/policies", QJsonDocument(finalPolicies).toJson(QJsonDocument::Compact), &error)) throw ServiceError{"IO_ERROR", error};
        stage(".awake-integration/policies.json", "policies");
    }
    QList<FileChange> table; for (const auto& change : changes) table.append(change);
    QJsonObject review{{"warnings", warnings}, {"dependencies", dependencies}};
    if (!reviewed.isEmpty()) review["dependencyResolutions"] = reviewed;
    requireResult(engine.prepare(id, table, request["instanceRevision"].toString(), review));
}
void WorkspaceServices::apply(const QString& id, const QJsonObject& request)
{
    const auto initial = context(id)["request"].toObject(); const auto record = workspaceRecord(initial["physicalId"].toString()); auto lease = acquire(record);
    TransactionEngine engine(m_store, root(record), mods);
    requireResult(engine.apply(id, request["planDigest"].toString(), request["instanceRevision"].toString(), lease));
    // No native model is loaded for these roots; parse/hash all native metadata before durable completion.
    const auto rows = scan(record);
    for (const auto& value : rows) if (value.toObject()["metadataState"] == "modified") throw ServiceError{"RECOVERY_REQUIRED", "Applied metadata did not verify."};
    if (QFileInfo::exists(operationDirectory(id) + "/loader-update.json")) writeRecord(m_profile + "/catalog/" + record["workspaceId"].toString() + ".json", readRecord(operationDirectory(id) + "/loader-update.json"));
    requireResult(engine.complete(id, lease));
}
QJsonObject WorkspaceServices::updates(const QJsonObject& params)
{
    const auto snapshot = inventory(params); const auto record = workspaceRecord(params["physicalId"].toString()); QJsonArray updates;
    for (const auto& value : snapshot["items"].toArray()) {
        const auto row = value.toObject(); if (!row["projectId"].isString()) continue;
        const bool excluded = row["updateLocked"].toBool() || row["metadataState"] != "verified"; QJsonValue candidate(QJsonValue::Null);
        if (!excluded) {
            const QJsonObject query{{"projectId", row["projectId"]}, {"minecraftVersion", record["minecraftVersion"]}, {"loader", record["loader"].toObject()["id"]}};
            const auto choices = providerId(row) == "curseforge" ? m_curseforge.compatibleVersions(query) : m_provider.compatibleVersions(query);
            QDateTime installed;
            for (const auto& choice : choices) if (choice.wire["versionId"] == row["versionId"]) { installed = choice.publishedAt; break; }
            if (installed.isValid()) for (const auto& choice : choices) {
                if (choice.publishedAt <= installed) break;
                if (choice.wire["releaseType"] == "release") { candidate = choice.wire; break; }
            }
        }
        QJsonObject update{{"projectId", row["projectId"]}, {"installedVersionId", row["versionId"]}, {"candidate", candidate}, {"excluded", excluded}}; if (row.contains("provider")) update["provider"] = row["provider"]; updates.append(update);
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
    if (saved["method"] == "workspaces.delete") throw ServiceError{"RECOVERY_REQUIRED", "Workspace deletion did not finish; delete it again."};
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
            if (saved["method"] == "workspaces.delete") {
                if (status["terminal"].toBool()) continue;
                const auto path = operationDirectory(id) + "/delete.json";
                if (QFileInfo::exists(path)) {
                    const auto record = readRecord(path); const auto workspaceId = record["workspaceId"].toString();
                    if (!QFileInfo::exists(root(record)) && QFileInfo::exists(m_profile + "/deleted/" + workspaceId)) {
                        QFile::remove(m_profile + "/catalog/" + workspaceId + ".json"); finishDelete(workspaceId);
                        requireResult(m_store.transition(id, "completed", {{"verified", true}})); continue;
                    }
                }
                requireResult(m_store.transition(id, "failed", {}, {{"code", "IO_ERROR"}, {"message", "The Manager closed before this project was deleted. Nothing was removed; delete it again."}, {"retryable", false}})); continue;
            }
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
