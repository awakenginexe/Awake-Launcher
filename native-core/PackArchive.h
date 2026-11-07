// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QString>

namespace PackArchive {
QJsonObject inspect(const QString &archivePath, const QString &format, const QString &privateStagingDir);
}
