// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include "awake/LocalePolicy.h"

class AwakeLocaleTest : public QObject {
    Q_OBJECT
   private slots:
    void mapsSystemLocales_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<QString>("expected");
        QTest::newRow("english") << "en_GB" << "en_US";
        QTest::newRow("thai") << "th_TH" << "th";
        QTest::newRow("simplified") << "zh_CN" << "zh_CN";
        QTest::newRow("singapore") << "zh_SG" << "zh_CN";
        QTest::newRow("traditional") << "zh_TW" << "zh_TW";
        QTest::newRow("hong-kong") << "zh_HK" << "zh_TW";
        QTest::newRow("hant") << "zh-Hant" << "zh_TW";
        QTest::newRow("hant-region") << "zh_Hant_MO" << "zh_TW";
        QTest::newRow("explicit-hans") << "zh-Hans-HK" << "zh_CN";
        QTest::newRow("unsupported") << "de_DE" << "en_US";
        QTest::newRow("empty") << "" << "en_US";
    }
    void mapsSystemLocales()
    {
        QFETCH(QString, locale);
        QFETCH(QString, expected);
        QCOMPARE(Awake::localeForSystem(locale), expected);
    }
    void restrictsCatalogs()
    {
        QCOMPARE(Awake::supportedLocales().size(), 4);
        QVERIFY(Awake::isSupportedLocale("en_US"));
        QVERIFY(Awake::isSupportedLocale("th"));
        QVERIFY(Awake::isSupportedLocale("zh_CN"));
        QVERIFY(Awake::isSupportedLocale("zh_TW"));
        QVERIFY(!Awake::isSupportedLocale("de"));
        QVERIFY(!Awake::isSupportedLocale("zh_Hant_HK"));
    }
};
QTEST_GUILESS_MAIN(AwakeLocaleTest)
#include "AwakeLocale_test.moc"
