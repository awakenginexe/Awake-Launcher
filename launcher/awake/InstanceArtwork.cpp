// SPDX-License-Identifier: GPL-3.0-only
#include "InstanceArtwork.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRandomGenerator>

namespace Awake {
QStringList screenshotFiles(const QString& gameRoot)
{
    if (gameRoot.isEmpty())
        return {};
    QDir directory(QDir(gameRoot).filePath("screenshots"));
    QStringList result;
    for (const auto& file : directory.entryInfoList(QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name)) {
        const auto suffix = file.suffix().toLower();
        if ((suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "webp") && file.size() <= 32 * 1024 * 1024)
            result.append(file.absoluteFilePath());
    }
    return result;
}

Artwork loadRandomScreenshot(const QString& gameRoot, const QString& previousFile, const std::shared_ptr<std::atomic_bool>& canceled)
{
    auto files = screenshotFiles(gameRoot);
    QString fallback;
    if (files.size() > 1 && files.removeOne(previousFile))
        fallback = previousFile;
    while (!files.isEmpty() || !fallback.isEmpty()) {
        if (canceled && canceled->load())
            return {};
        if (files.isEmpty()) {
            files.append(fallback);
            fallback.clear();
        }
        const auto index = QRandomGenerator::global()->bounded(static_cast<int>(files.size()));
        const auto path = files.takeAt(index);
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 16384 || size.height() > 16384 || qint64(size.width()) * size.height() > 32000000)
            continue;
        if (size.width() > 1920 || size.height() > 1080)
            reader.setScaledSize(size.scaled(QSize(1920, 1080), Qt::KeepAspectRatio));
        auto image = reader.read();
        if (!image.isNull())
            return { path, std::move(image) };
    }
    return {};
}

QImage fallbackArtwork()
{
    return QImage(":/backgrounds/awake-minecraft");
}

Artwork loadArtwork(const QString& gameRoot, const QString& previousFile, const std::shared_ptr<std::atomic_bool>& canceled)
{
    auto artwork = loadRandomScreenshot(gameRoot, previousFile, canceled);
    if (!artwork.image.isNull() || (canceled && canceled->load()))
        return artwork;
    artwork.image = fallbackArtwork();
    artwork.path = QStringLiteral(":/backgrounds/awake-minecraft");
    return artwork;
}

QImage frostedImage(const QImage& image)
{
    if (image.isNull())
        return {};
    const auto small = image.scaled(QSize(36, 24), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return small.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
}  // namespace Awake
