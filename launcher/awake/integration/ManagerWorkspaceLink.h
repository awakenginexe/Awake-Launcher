// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QString>
#include <memory>
namespace Awake::Integration {
class PhysicalInstanceLease;
// Only published Manager workspaces qualify; this never enrolls or adopts a folder.
QJsonObject managerWorkspaceBinding(const QString& root, const QString& expectedIdentity = {}, QString* error = nullptr);
std::shared_ptr<PhysicalInstanceLease> acquireManagerWorkspace(const QString& root, const QString& expectedIdentity = {}, QString* error = nullptr);
// A Launcher instance the user connected to the Manager: same checks, but its files stay in the Launcher's folder.
QJsonObject sharedInstanceBinding(const QString& root, const QString& expectedIdentity = {}, QString* error = nullptr);
std::shared_ptr<PhysicalInstanceLease> acquireSharedInstance(const QString& root, const QString& expectedIdentity = {}, QString* error = nullptr);
}
