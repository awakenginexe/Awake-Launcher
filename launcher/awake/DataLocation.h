// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace Awake {
inline QString installedDataPath(const QString& installDir, const QString& normalPath)
{
    QFile config(QDir(installDir).filePath("data-location.txt"));
    if (!config.exists()) return normalPath;
    if (!config.open(QIODevice::ReadOnly)) return {};
    QTextStream text(&config);
    const auto mode = text.readLine();
    if (mode == "normal") return normalPath;
    if (mode == "compact") return QDir(installDir).filePath("AwakeLauncherData");
    const auto path = text.readLine();
    return mode == "custom" && QDir::isAbsolutePath(path) ? QDir::cleanPath(path) : QString();
}
}
