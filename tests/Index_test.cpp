#include <QTest>

#include <meta/Index.h>
#include <meta/VersionList.h>
#include <meta/Version.h>

class IndexTest : public QObject {
    Q_OBJECT
   private slots:
    void loaderSelectionMatchesNativeCreation()
    {
        for (const auto& uid : {QString("net.fabricmc.fabric-loader"), QString("org.quiltmc.quilt-loader"), QString("net.minecraftforge"), QString("net.neoforged")}) {
            Meta::VersionList list(uid);
            auto loader = std::make_shared<Meta::Version>(uid, "0.19.5");
            loader->setType("release");
            loader->setTime(1);
            const bool intermediary = uid.contains("fabric") || uid.contains("quilt");
            loader->setRequires({Meta::Require{intermediary ? "net.fabricmc.intermediary" : "net.minecraft", intermediary ? "" : "1.21.8", ""}}, {});
            list.setVersions({loader});
            auto selected = list.getRecommendedForMinecraft("1.21.8");
            QVERIFY(selected);
            QCOMPARE(selected->descriptor(), QString("0.19.5"));
            QVERIFY(!list.getRecommendedForMinecraft("1.12.2"));
        }
    }
    void test_hasUid_and_getList()
    {
        Meta::Index windex({ std::make_shared<Meta::VersionList>("list1"), std::make_shared<Meta::VersionList>("list2"),
                             std::make_shared<Meta::VersionList>("list3") });
        QVERIFY(windex.hasUid("list1"));
        QVERIFY(!windex.hasUid("asdf"));
        QVERIFY(windex.get("list2") != nullptr);
        QCOMPARE(windex.get("list2")->uid(), QString("list2"));
        QVERIFY(windex.get("adsf") != nullptr);
    }

    void test_merge()
    {
        Meta::Index windex({ std::make_shared<Meta::VersionList>("list1"), std::make_shared<Meta::VersionList>("list2"),
                             std::make_shared<Meta::VersionList>("list3") });
        QCOMPARE(windex.lists().size(), 3);
        windex.merge(std::shared_ptr<Meta::Index>(
            new Meta::Index({ std::make_shared<Meta::VersionList>("list1"), std::make_shared<Meta::VersionList>("list2"),
                              std::make_shared<Meta::VersionList>("list3") })));
        QCOMPARE(windex.lists().size(), 3);
        windex.merge(std::shared_ptr<Meta::Index>(
            new Meta::Index({ std::make_shared<Meta::VersionList>("list4"), std::make_shared<Meta::VersionList>("list2"),
                              std::make_shared<Meta::VersionList>("list5") })));
        QCOMPARE(windex.lists().size(), 5);
        windex.merge(std::shared_ptr<Meta::Index>(new Meta::Index({ std::make_shared<Meta::VersionList>("list6") })));
        QCOMPARE(windex.lists().size(), 6);
    }
};

QTEST_GUILESS_MAIN(IndexTest)

#include "Index_test.moc"
