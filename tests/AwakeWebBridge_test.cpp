// SPDX-License-Identifier: GPL-3.0-only
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>
#include "Application.h"
#include "InstanceList.h"
#include "awake/web/AwakeWebAssets.h"
#include "awake/web/AwakeWebBridge.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "ui/MainWindow.h"

using namespace Awake::Web;
class AwakeWebBridgeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QTRY_COMPARE_WITH_TIMEOUT(APPLICATION->status(), Application::Initialized, 15000); }
    void realDtoAndNativeSelection()
    {
        auto* window = APPLICATION->showMainWindow(false);
        Assets assets;
        int actions = 0;
        QString lastAction, lastId;
        APPLICATION->settings()->set("InstSortMode", "Playtime");
        Bridge bridge(&assets, [window](const QString& id) {
            QMetaObject::invokeMethod(window, "instanceSelectRequest", Qt::DirectConnection, Q_ARG(QString, id));
            return APPLICATION->settings()->get("SelectedInstance").toString() == id;
        }, [&actions, &lastAction, &lastId](const QString& action, const QString& id) {
            ++actions; lastAction = action; lastId = id; return QVariantMap{{"ok", true}};
        });
        auto state = bridge.snapshot();
        QCOMPARE(state.value("sortMode").toString(), QString("TotalTimePlayed"));
        QVERIFY(bridge.setPreference("sortMode", "TotalTimePlayed").value("ok").toBool());
        QCOMPARE(APPLICATION->settings()->get("InstSortMode").toString(), QString("Playtime"));
        const auto instances = state.value("instances").toList();
        QCOMPARE(instances.size(), 2);
        QVariantMap dto;
        for (const auto& item : instances)
            if (item.toMap().value("id").toString() == "one") dto = item.toMap();
        QCOMPARE(dto.value("minecraftVersion").toString(), QString("1.21.8"));
        QCOMPARE(dto.value("loader").toString(), QString("Fabric"));
        QCOMPARE(dto.value("loaderVersion").toString(), QString("0.16.14"));
        QVERIFY(dto.value("iconUrl").toString().startsWith("awake://ui/images/"));
        QVERIFY(!dto.contains("gameRoot"));
        QVERIFY(!dto.contains("settings"));
        QVERIFY(bridge.selectInstance("one").value("ok").toBool());
        QCOMPARE(APPLICATION->settings()->get("SelectedInstance").toString(), QString("one"));
        QVERIFY(!bridge.selectInstance("missing").value("ok").toBool());
        QVERIFY(!bridge.launchInstance("missing").value("ok").toBool());
        QVERIFY(!bridge.invokeAction("edit", "missing").value("ok").toBool());
        QVERIFY(!bridge.invokeAction("executeCommand", "one").value("ok").toBool());
        QVERIFY(!bridge.setPreference("JavaPath", "/arbitrary").value("ok").toBool());
        QVERIFY(bridge.setPreference("pin", QVariantMap{{"id", "one"}, {"pinned", true}}).value("ok").toBool());
        QVERIFY(APPLICATION->settings()->get("AwakePinnedInstances").toStringList().contains("one"));
        QCOMPARE(actions, 0);
        QVERIFY(bridge.invokeAction("settings", {}).value("ok").toBool());
        QVERIFY(!bridge.selectInstance("two").value("ok").toBool());
        QTRY_COMPARE(actions, 1);
        QVERIFY(bridge.launchInstance("one").value("ok").toBool());
        QTRY_COMPARE(actions, 2);
        QCOMPARE(lastAction, QString("launch"));
        QCOMPARE(lastId, QString("one"));
        QCOMPARE(APPLICATION->settings()->get("SelectedInstance").toString(), QString("one"));
        auto* instance = APPLICATION->instances()->getInstanceById("one");
        instance->setRunning(true);
        QVERIFY(!bridge.launchInstance("one").value("ok").toBool());
        QCOMPARE(actions, 2);
        instance->setRunning(false);
    }
    void rapidArtworkAndDestroyedBridge()
    {
        Assets assets;
        auto bridge = std::make_unique<Bridge>(&assets, [](const QString& id) {
            APPLICATION->settings()->set("SelectedInstance", id);
            return true;
        }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        QSignalSpy artwork(bridge.get(), &Bridge::artworkChanged);
        QVERIFY(bridge->selectInstance("one").value("ok").toBool());
        QCoreApplication::processEvents();
        QVERIFY(bridge->selectInstance("two").value("ok").toBool());
        artwork.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!artwork.isEmpty(), 5000);
        for (const auto& event : artwork) QCOMPARE(event.at(0).toString(), QString("two"));
        QVERIFY(artwork.last().at(1).toString().isEmpty());
        QVERIFY(artwork.last().at(2).toString().isEmpty()); // Missing screenshots are an ordinary empty state.
        QVERIFY(bridge->selectInstance("one").value("ok").toBool());
        QCoreApplication::processEvents();
        bridge.reset();
        QCoreApplication::processEvents();
    }
};

static void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("Cannot write web bridge test fixture");
}

#include "NativeTestAccounts.h"
int main(int argc, char** argv)
{
    const auto original = QDir::currentPath();
    QDir().mkpath(original + "/.validation");
    QTemporaryDir data(original + "/.validation/awake-bridge-XXXXXX");
    if (!data.isValid()) return 1;
    seedStartupAccount(data.path());
    writeFile(data.path() + "/awakelauncher.cfg", "Language=en_US\nIgnoreJavaWizard=true\nAutomaticJavaDownload=true\nAutomaticJavaSwitch=true\nUserAskedAboutAutomaticJavaDownload=true\nProxyType=HTTP\nProxyAddr=127.0.0.1\nProxyPort=9\n");
    for (const auto& id : {QString("one"), QString("two")}) {
        const auto path = data.path() + "/instances/" + id;
        QDir().mkpath(path);
        writeFile(path + "/instance.cfg", "InstanceType=OneSix\nname=" + id.toUtf8() + "\niconKey=grass\n");
        writeFile(path + "/mmc-pack.json", R"({"formatVersion":1,"components":[{"uid":"net.minecraft","version":"1.21.8","important":true},{"uid":"net.fabricmc.fabric-loader","version":"0.16.14"}]})");
        if (id == "one") {
            QDir().mkpath(path + "/.minecraft/screenshots");
            QImage image(600, 400, QImage::Format_RGB32);
            image.fill(Qt::darkGreen);
            image.save(path + "/.minecraft/screenshots/fixture.png");
        }
    }
    qputenv("AWAKE_FRONTEND", "widgets");
    auto directory = data.path().toUtf8();
    char dataOption[] = "-d";
    char* appArgs[] = {argv[0], dataOption, directory.data(), nullptr};
    int appArgc = 3;
    int result = 1;
    {
        Application app(appArgc, appArgs);
        Q_INIT_RESOURCE(multimc);
        Q_INIT_RESOURCE(backgrounds);
        Q_INIT_RESOURCE(documents);
        Q_INIT_RESOURCE(awakelauncher);
        Q_INIT_RESOURCE(awake_translations);
        Q_INIT_RESOURCE(pe_light);
        Q_INIT_RESOURCE(pe_dark);
        Q_INIT_RESOURCE(pe_colored);
        Q_INIT_RESOURCE(pe_blue);
        Q_INIT_RESOURCE(breeze_dark);
        Q_INIT_RESOURCE(breeze_light);
        Q_INIT_RESOURCE(OSX);
        Q_INIT_RESOURCE(iOS);
        Q_INIT_RESOURCE(flat);
        Q_INIT_RESOURCE(flat_white);
        Q_INIT_RESOURCE(shaders);
        QTimer temporaryWarning;
        temporaryWarning.setInterval(20);
        QObject::connect(&temporaryWarning, &QTimer::timeout, &app, [&] {
            if (app.status() == Application::Initialized) temporaryWarning.stop();
            else if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                if (warning->text().startsWith("Your instance folder is in a temporary folder:")) warning->accept();
        });
        temporaryWarning.start();
        QTimer::singleShot(0, &app, [&] { AwakeWebBridgeTest test; result = QTest::qExec(&test, argc, argv); app.quit(); });
        app.exec();
    }
    QDir::setCurrent(original);
    return result;
}
#include "AwakeWebBridge_test.moc"
