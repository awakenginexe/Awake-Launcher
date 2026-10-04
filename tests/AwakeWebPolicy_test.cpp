// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include "awake/web/AwakeWebPolicy.h"

using namespace Awake::Web;
class AwakeWebPolicyTest : public QObject {
    Q_OBJECT
private slots:
    void resourceBoundary()
    {
        QCOMPARE(resourcePath(QUrl("awake://ui/")), QString(":/awake-web/index.html"));
        QCOMPARE(resourcePath(QUrl("awake://ui/assets/app-123.js")), QString(":/awake-web/assets/app-123.js"));
        QCOMPARE(resourcePath(QUrl("awake://ui/qwebchannel.js")), QString(":/awake-web/qwebchannel.js"));
        for (const auto& bad : {"file:///C:/secret.txt", "https://ui/assets/app.js", "awake://other/index.html",
                               "awake://user:pass@ui/index.html", "awake://ui:42/index.html", "awake://ui/../secret",
                               "awake://ui/assets/%2e%2e/secret", "awake://ui/assets/%252e%252e/secret", "awake://ui/assets/a%2fb.js",
                               "awake://ui/assets/a%5cb.js", "awake://ui/index.html?path=secret", "awake://ui/private.txt"}) {
            QVERIFY2(resourcePath(QUrl(bad)).isEmpty(), bad);
        }
        QVERIFY(internalUrl(QUrl("awake://ui/images/0123456789abcdef.png")));
        QVERIFY(!internalUrl(QUrl("awake://ui/images/../../secret.png")));
        QVERIFY(!internalUrl(QUrl("awake://ui/images/secret.png")));
    }
    void commandBoundary()
    {
        QVERIFY(preferenceAllowed("javaProfile", "awake"));
        QVERIFY(preferenceAllowed("javaProfile", "graalvm"));
        QVERIFY(!preferenceAllowed("javaProfile", "maximum-fps"));
        QVERIFY(!preferenceAllowed("javaProfile", QVariantMap{{"path", "C:/anything.exe"}}));
        QVERIFY(actionAllowed("launchOptions"));
        QVERIFY(!actionAllowed("executeCommand"));
        QVERIFY(preferenceAllowed("reducedMotion", true));
        QVERIFY(!preferenceAllowed("reducedMotion", "true"));
        QVERIFY(preferenceAllowed("sortMode", "LastLaunch"));
        QVERIFY(!preferenceAllowed("sortMode", "unknown"));
        QVERIFY(preferenceAllowed("language", "zh-CN"));
        QVERIFY(preferenceAllowed("language", "zh-TW"));
        QVERIFY(!preferenceAllowed("language", "zh-HK"));
        QVERIFY(!preferenceAllowed("language", "zh_CN"));
        QVERIFY(preferenceAllowed("pin", QVariantMap{{"id", "real-instance"}, {"pinned", false}}));
        QVERIFY(!preferenceAllowed("pin", QVariantMap{{"id", "x"}, {"pinned", true}, {"path", "/secret"}}));
        QVERIFY(!preferenceAllowed("Language", "en"));
        QVERIFY(externalUrl(QUrl("https://prismlauncher.org/wiki/")));
        QVERIFY(!externalUrl(QUrl("https://user:password@example.com/")));
        QVERIFY(!externalUrl(QUrl("javascript:alert(1)")));
    }
    void safeDto()
    {
        InstanceData instance;
        instance.id = "one";
        instance.name = "Thai ไทย 中文";
        instance.lastLaunch = 1000;
        instance.totalTimePlayed = 42;
        auto dto = instanceDto(instance);
        QCOMPARE(dto.size(), 13);
        QCOMPARE(dto.value("name").toString(), instance.name);
        QCOMPARE(dto.value("lastLaunch").toLongLong(), 1000LL);
        QCOMPARE(dto.value("totalTimePlayed").toLongLong(), 42LL);
        QCOMPARE(dto.value("minecraftVersion").toString(), QString());
        QVERIFY(!dto.contains("gameRoot"));
        QVERIFY(!dto.contains("settings"));
        QVERIFY(!dto.contains("accessToken"));
        QCOMPARE(frontendLocale("en_US"), QString("en"));
        QCOMPARE(frontendLocale("th_TH"), QString("th"));
        QCOMPARE(frontendLocale("zh_CN"), QString("zh-CN"));
        QCOMPARE(frontendLocale("zh_TW"), QString("zh-TW"));
        QCOMPARE(nativeLocale("en"), QString("en_US"));
        QCOMPARE(nativeLocale("th"), QString("th"));
        QCOMPARE(nativeLocale("zh-CN"), QString("zh_CN"));
        QCOMPARE(nativeLocale("zh-TW"), QString("zh_TW"));
        QVERIFY(nativeLocale("zh-HK").isEmpty());
    }
};
QTEST_GUILESS_MAIN(AwakeWebPolicyTest)
#include "AwakeWebPolicy_test.moc"
