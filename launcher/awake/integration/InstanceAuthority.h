// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QString>
namespace Awake::Integration::InstanceAuthority {
// Enrollment is a native creation/recovery action, never a caller-selected IPC path.
bool enroll(const QString& root, const QString& physicalId, const QString& registry, const QString& ownerProfile,
            const QString& registrationId, QString* error = nullptr);
void* lock(const QString& physicalId, QString* error = nullptr);
QJsonObject binding(const QString& physicalId, QString* error = nullptr);
bool validate(const QString& root, const QString& physicalId, const QString& registry, QString* error = nullptr);
bool requireGameRecord(const QString& physicalId, QString* error = nullptr);
// Undoes enroll for this registration so the instance is ordinary again. Files outside .awake-integration are untouched.
bool unenroll(const QString& root, const QString& physicalId, const QString& registrationId, QString* error = nullptr);
// Removes what a connect attempt left when it stopped before publishing. Returns false, changing nothing, for anything else.
bool clearAbandoned(const QString& root, QString* error = nullptr);
bool blocksNativeLoad(const QString& instanceRoot);
}
