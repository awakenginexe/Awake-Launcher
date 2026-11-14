// SPDX-License-Identifier: GPL-3.0-only
#include "ManagerWorkspaceLink.h"
#include "InstanceAuthority.h"
#include "InstanceDiscovery.h"
#include "DurableIO.h"
#include "FileSystem.h"
#include "ProfileAccess.h"
#include "PhysicalInstanceLease.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
namespace Awake::Integration {
namespace {
QJsonObject verifiedBinding(const QString& root, const QString& expected, bool shared, QString* error)
{
    const auto reject = [error]() -> QJsonObject {
        if (error) *error = "Choose a completed Awake Modpack Manager workspace. Its folder or saved ownership could not be verified.";
        return {};
    };
    const auto canonical = QFileInfo(root).canonicalFilePath(), id = physicalIdentityV2(root);
    if (canonical.isEmpty() || id.isEmpty() || (!expected.isEmpty() && id != expected)) return reject();
    const auto binding = InstanceAuthority::binding(id, error);
    const auto registration = binding["registrationId"].toString(), registry = binding["registry"].toString();
    const auto profile = QFileInfo(registry).dir().canonicalPath();
    if (binding.isEmpty() || QUuid(registration).toString(QUuid::WithoutBraces) != registration ||
        (!shared && canonical != profile + "/workspaces/" + registration) || registry != profile + "/ownership" ||
        binding["ownerProfile"] != physicalIdentityV2(profile) || !InstanceAuthority::validate(canonical, id, registry, error)) return reject();
    const auto catalog = profile + "/catalog/" + registration + ".json";
    if (!storageAccessIsPrivate(catalog) || DurableIO::hash(catalog).isEmpty()) return reject();
    QFile file(catalog);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return reject();
    const auto envelope = QJsonDocument::fromJson(file.readAll()).object(), body = envelope["body"].toObject();
    const auto digest = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(body).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    const auto config = DurableIO::hash(canonical + "/instance.cfg"), pack = DurableIO::hash(canonical + "/mmc-pack.json");
    if (envelope.size() != 2 || envelope["sha256"] != digest || body["format"] != 1 || body["workspaceId"] != registration || body["physicalId"] != id ||
        (shared ? body["sharedRoot"] != canonical : body.contains("sharedRoot")) ||
        config.isEmpty() || config == "missing" || pack.isEmpty() || pack == "missing") return reject();
    return binding;
}
std::shared_ptr<PhysicalInstanceLease> acquireVerified(const QString& root, const QString& expected, bool shared, QString* error)
{
    const auto binding = verifiedBinding(root, expected, shared, error);
    if (binding.isEmpty()) return {};
    auto lease = PhysicalInstanceLease::acquire(root, binding["physicalId"].toString(), binding["registry"].toString(), error);
    if (!lease || !lease->pinRoot(error)) return {};
    // Recovery can be published by the operation that held the lock immediately before us.
    if (verifiedBinding(root, lease->physicalId(), shared, error).isEmpty() || QFileInfo::exists(root + "/.awake-integration/recovery.json")) {
        if (error) *error = "Open Awake Modpack Manager and finish recovering this workspace before playing.";
        return {};
    }
    return lease;
}
}  // namespace
QJsonObject managerWorkspaceBinding(const QString& root, const QString& expected, QString* error) { return verifiedBinding(root, expected, false, error); }
QJsonObject sharedInstanceBinding(const QString& root, const QString& expected, QString* error) { return verifiedBinding(root, expected, true, error); }
std::shared_ptr<PhysicalInstanceLease> acquireManagerWorkspace(const QString& root, const QString& expected, QString* error) { return acquireVerified(root, expected, false, error); }
std::shared_ptr<PhysicalInstanceLease> acquireSharedInstance(const QString& root, const QString& expected, QString* error) { return acquireVerified(root, expected, true, error); }
bool moveSharedInstance(const QString& root, const QString& newRoot, QString* error)
{
    const auto fail = [error](const QString& reason) { if (error && error->isEmpty()) *error = reason; return false; };
    const auto binding = verifiedBinding(root, {}, true, error);
    if (binding.isEmpty()) return false;
    // Holding the lock proves no game or Manager change is using the folder. It lives outside the folder, so it does not block the move.
    const auto lease = PhysicalInstanceLease::acquire(root, binding["physicalId"].toString(), binding["registry"].toString(), error);
    if (!lease) return false;
    if (QFileInfo::exists(root + "/.awake-integration/recovery.json")) return fail("Open Awake Modpack Manager and finish recovering this instance first.");
    const auto catalog = QFileInfo(binding["registry"].toString()).dir().canonicalPath() + "/catalog/" + binding["registrationId"].toString() + ".json";
    QFile file(catalog);
    if (!file.open(QIODevice::ReadOnly)) return fail("The Manager's record of this instance cannot be read.");
    const auto original = file.readAll();
    auto body = QJsonDocument::fromJson(original).object()["body"].toObject();
    file.close();
    if (!FS::renameFolder(root, newRoot)) return fail("Another program is using the instance folder. Close any File Explorer window or program opened inside it, then rename again.");
    // The Manager finds the instance by this path; a stale one makes both programs lose it.
    body["sharedRoot"] = QFileInfo(newRoot).canonicalFilePath();
    const auto digest = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(body).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    if (DurableIO::write(catalog, QJsonDocument(QJsonObject{{"body", body}, {"sha256", digest}}).toJson(QJsonDocument::Compact), error) &&
        !verifiedBinding(newRoot, binding["physicalId"].toString(), true, error).isEmpty()) return true;
    // shortcut: a crash between the move and this record update leaves a stale path, upgrade to a journaled move if that ever happens.
    if (FS::renameFolder(newRoot, root)) DurableIO::write(catalog, original);
    return fail("The Manager's record of this instance could not be updated.");
}
}
