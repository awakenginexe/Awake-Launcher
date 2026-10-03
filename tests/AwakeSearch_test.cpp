// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include "awake/InstanceSearch.h"

class AwakeSearchTest : public QObject {
    Q_OBJECT
   private slots:
    void matchesAcrossNameAndGroup()
    {
        QVERIFY(Awake::matchesInstance("Fabric 1.21", "Creative", "creative FABRIC"));
        QVERIFY(Awake::matchesInstance("Fabric 1.21", "Creative", "  fabric   creative "));
        QVERIFY(!Awake::matchesInstance("Fabric 1.21", "Creative", "forge"));
        QVERIFY(!Awake::matchesInstance("Fabric 1.21", "Creative", "fabric survival"));
    }
    void handlesUnicodeAndEmptyQueries()
    {
        QVERIFY(Awake::matchesInstance(QString::fromUtf8("โลกส่วนตัว"), "Vanilla", QString::fromUtf8("ส่วนตัว")));
        QVERIFY(Awake::matchesInstance(QString::fromUtf8("生存世界"), "Vanilla", QString::fromUtf8("生存")));
        QVERIFY(Awake::matchesInstance("Vanilla", "", " \t "));
        QVERIFY(!Awake::matchesInstance("", "", "minecraft"));
    }
};
QTEST_GUILESS_MAIN(AwakeSearchTest)
#include "AwakeSearch_test.moc"
