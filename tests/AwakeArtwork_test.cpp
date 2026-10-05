// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include "awake/InstanceArtwork.h"

class AwakeArtworkTest : public QObject {
    Q_OBJECT
   private slots:
    void missingFolderDoesNotCreateAnything()
    {
        QTemporaryDir root;
        QVERIFY(Awake::loadRandomScreenshot(root.path()).image.isNull());
        QVERIFY(!QDir(root.path() + "/screenshots").exists());
    }
    void fallbackForMissingOrCorruptScreenshots()
    {
        Q_INIT_RESOURCE(backgrounds);
        QTemporaryDir root;
        const auto missing = Awake::loadArtwork(root.path());
        QVERIFY(missing.path.startsWith(":/backgrounds/"));
        QVERIFY(!missing.image.isNull());
        QCOMPARE(missing.image.size(), QSize(1920, 1080));
        QDir().mkpath(root.path() + "/screenshots");
        QFile corrupt(root.path() + "/screenshots/broken.png");
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("invalid image");
        corrupt.close();
        const auto broken = Awake::loadArtwork(root.path());
        QVERIFY(broken.path.startsWith(":/backgrounds/"));
        QCOMPARE(broken.image.size(), missing.image.size());
    }
    void onlySelectedInstancesScreenshots()
    {
        QTemporaryDir root;
        QDir().mkpath(root.path() + "/other/screenshots");
        QImage image(80, 50, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(root.path() + "/other/screenshots/world.png"));
        QVERIFY(Awake::loadRandomScreenshot(root.path() + "/selected").image.isNull());
    }
    void skipsCorruptAndUnsupportedFiles()
    {
        QTemporaryDir root;
        QDir().mkpath(root.path() + "/screenshots");
        QFile corrupt(root.path() + "/screenshots/broken.png");
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("invalid image");
        corrupt.close();
        QImage image(80, 50, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(root.path() + "/screenshots/world.png"));
        QVERIFY(image.save(root.path() + "/screenshots/not-a-screenshot.bmp"));
        const auto artwork = Awake::loadRandomScreenshot(root.path());
        QVERIFY(artwork.path.endsWith("world.png"));
        QCOMPARE(artwork.image.size(), image.size());
    }
    void choosesAnotherImageWhenAvailable()
    {
        QTemporaryDir root;
        QDir().mkpath(root.path() + "/screenshots");
        QImage image(80, 50, QImage::Format_RGB32);
        image.fill(Qt::green);
        const auto previous = root.path() + "/screenshots/one.png";
        QVERIFY(image.save(previous));
        QVERIFY(image.save(root.path() + "/screenshots/two.png"));
        for (int i = 0; i < 10; ++i)
            QVERIFY(Awake::loadRandomScreenshot(root.path(), previous).path.endsWith("two.png"));
    }
    void boundsDecodedSizeAndFrosting()
    {
        QTemporaryDir root;
        QDir().mkpath(root.path() + "/screenshots");
        QImage image(3840, 2160, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(root.path() + "/screenshots/large.png"));
        const auto artwork = Awake::loadRandomScreenshot(root.path());
        QCOMPARE(artwork.image.size(), QSize(1920, 1080));
        QCOMPARE(Awake::frostedImage(artwork.image).size(), artwork.image.size());
        QVERIFY(Awake::frostedImage({}).isNull());
    }
    void fallsBackToPreviousWhenOtherFilesAreBroken()
    {
        QTemporaryDir root;
        QDir().mkpath(root.path() + "/screenshots");
        QImage image(80, 50, QImage::Format_RGB32);
        image.fill(Qt::green);
        const auto previous = root.path() + "/screenshots/valid.png";
        QVERIFY(image.save(previous));
        QFile corrupt(root.path() + "/screenshots/broken.png");
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("invalid image");
        corrupt.close();
        QCOMPARE(Awake::loadRandomScreenshot(root.path(), previous).path, previous);
    }
    void canceledLoadDoesNotDecode()
    {
        QTemporaryDir root;
        QDir().mkpath(root.path() + "/screenshots");
        QImage image(80, 50, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(root.path() + "/screenshots/valid.png"));
        QVERIFY(Awake::loadRandomScreenshot(root.path(), {}, std::make_shared<std::atomic_bool>(true)).image.isNull());
    }
};
QTEST_GUILESS_MAIN(AwakeArtworkTest)
#include "AwakeArtwork_test.moc"
