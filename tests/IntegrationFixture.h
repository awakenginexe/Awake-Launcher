// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "awake/integration/InstanceAuthority.h"
#include "awake/integration/PhysicalInstanceLease.h"
#include <QUuid>
inline auto fixtureLease(const QString& root, const QString& id, const QString& registry)
{
    using namespace Awake::Integration;
    const auto registration = QUuid::createUuidV5(QUuid("fbd891e5-b202-47cd-83bc-407cbac24092"), id.toUtf8()).toString(QUuid::WithoutBraces);
    if (!InstanceAuthority::enroll(root, id, registry, "qt-fixture", registration)) return std::shared_ptr<PhysicalInstanceLease>{};
    return PhysicalInstanceLease::acquire(root, id, registry);
}
