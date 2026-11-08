// SPDX-License-Identifier: GPL-3.0-only
#include "ManagerWorkspaceLink.h"
#include "InstanceAuthority.h"
#include "InstanceDiscovery.h"
#include "DurableIO.h"
#include "ProfileAccess.h"
#include "PhysicalInstanceLease.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
namespace Awake::Integration {
QJsonObject managerWorkspaceBinding(const QString& root, const QString& expected, QString* error)
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
        canonical != profile + "/workspaces/" + registration || registry != profile + "/ownership" ||
        binding["ownerProfile"] != physicalIdentityV2(profile) || !InstanceAuthority::validate(canonical, id, registry, error)) return reject();
    const auto catalog = profile + "/catalog/" + registration + ".json";
    if (!storageAccessIsPrivate(catalog) || DurableIO::hash(catalog).isEmpty()) return reject();
    QFile file(catalog);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return reject();
    const auto envelope = QJsonDocument::fromJson(file.readAll()).object(), body = envelope["body"].toObject();
    const auto digest = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(body).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    const auto config = DurableIO::hash(canonical + "/instance.cfg"), pack = DurableIO::hash(canonical + "/mmc-pack.json");
    if (envelope.size() != 2 || envelope["sha256"] != digest || body["format"] != 1 || body["workspaceId"] != registration || body["physicalId"] != id ||
        config.isEmpty() || config == "missing" || pack.isEmpty() || pack == "missing") return reject();
    return binding;
}
std::shared_ptr<PhysicalInstanceLease> acquireManagerWorkspace(const QString& root, const QString& expected, QString* error)
{
    const auto binding = managerWorkspaceBinding(root, expected, error);
    if (binding.isEmpty()) return {};
    auto lease = PhysicalInstanceLease::acquire(root, binding["physicalId"].toString(), binding["registry"].toString(), error);
    if (!lease || !lease->pinRoot(error)) return {};
    // Recovery can be published by the operation that held the lock immediately before us.
    if (managerWorkspaceBinding(root, lease->physicalId(), error).isEmpty() || QFileInfo::exists(root + "/.awake-integration/recovery.json")) {
        if (error) *error = "Open Awake Modpack Manager and finish recovering this workspace before playing.";
        return {};
    }
    return lease;
}
}
