// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include "BuildConfig.h"
class BuildConfigTest : public QObject {
    Q_OBJECT
private slots:
    void stableTagsDoNotShowABranchSuffix_data() {
        QTest::addColumn<QString>("tag");
        QTest::newRow("release-tag") << QString("v3.0.0");
        QTest::newRow("plain-tag") << QString("3.0.0");
    }
    void stableTagsDoNotShowABranchSuffix() {
        QFETCH(QString, tag); Config config;
        config.VERSION_MAJOR=3; config.VERSION_MINOR=0; config.VERSION_PATCH=0;
        config.VERSION_CHANNEL="release/3.0.0"; config.GIT_TAG=tag;
        QCOMPARE(config.versionString(), QString("3.0.0"));
        QCOMPARE(config.printableVersionString(), QString("3.0.0"));
    }
    void compiledCommitMatchesTheExpectedSource() {
        const auto expected = qEnvironmentVariable("AWAKE_EXPECTED_SOURCE_COMMIT");
        if (expected.isEmpty()) QSKIP("Set AWAKE_EXPECTED_SOURCE_COMMIT to verify build provenance.");
        QCOMPARE(BuildConfig.GIT_COMMIT, expected);
    }
    void unmatchedTagsRetainTheDevelopmentChannel() {
        Config config; config.VERSION_MAJOR=3; config.VERSION_MINOR=0; config.VERSION_PATCH=0;
        config.VERSION_CHANNEL="feature"; config.GIT_TAG="v2.0.2";
        QCOMPARE(config.printableVersionString(), QString("3.0.0-feature"));
    }
};
QTEST_GUILESS_MAIN(BuildConfigTest)
#include "BuildConfig_test.moc"
