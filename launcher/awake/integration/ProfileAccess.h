// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>
namespace Awake::Integration {
bool profileAccessIsPrivate(const QString& directory);
bool storageAccessIsPrivate(const QString& path);
bool createPrivateStorageDirectory(const QString& directory);
}
