// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QImage>
#include <QStringList>
#include <atomic>
#include <memory>

namespace Awake {
struct Artwork {
    QString path;
    QImage image;
};
QStringList screenshotFiles(const QString& gameRoot);
Artwork loadRandomScreenshot(const QString& gameRoot,
                             const QString& previousFile = {},
                             const std::shared_ptr<std::atomic_bool>& canceled = {});
Artwork loadArtwork(const QString& gameRoot,
                    const QString& previousFile = {},
                    const std::shared_ptr<std::atomic_bool>& canceled = {});
QImage fallbackArtwork();
QImage frostedImage(const QImage& image);
}  // namespace Awake
