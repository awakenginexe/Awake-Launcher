// SPDX-License-Identifier: GPL-3.0-only
#include <QAction>
#include <QDialog>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMenu>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <memory>
#include "Application.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "awake/web/AwakeWebAssets.h"
#include "awake/web/AwakeWebBridge.h"
#include "settings/SettingsObject.h"
#include "translations/TranslationsModel.h"
#include "ui/MainWindow.h"
#include "ui/InstanceWindow.h"

class AwakeWebShellTest : public QObject {
    Q_OBJECT
    MainWindow* window = nullptr;
    QWebEngineView* view = nullptr;
    Awake::Web::Bridge* bridge = nullptr;
    QUrl folderUrl;
    QVariant evaluate(const QString& code)
    {
        auto result = std::make_shared<QVariant>();
        QEventLoop loop;
        const QPointer<QEventLoop> guardedLoop(&loop);
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        view->page()->runJavaScript(code, [result, guardedLoop](const QVariant& value) {
            *result = value;
            if (guardedLoop) guardedLoop->quit();
        });
        timeout.start(5000);
        loop.exec();
        return *result;
    }
   private slots:
    void recordFolder(const QUrl& url) { folderUrl = url; }
    void initTestCase()
    {
        QTRY_COMPARE_WITH_TIMEOUT(APPLICATION->status(), Application::Initialized, 15000);
        window = APPLICATION->showMainWindow(false);
        view = window->findChild<QWebEngineView*>("awakeWebView");
        bridge = window->findChild<Awake::Web::Bridge*>();
        QVERIFY(view && bridge);
        QTRY_VERIFY_WITH_TIMEOUT(view->isVisible(), 20000);
        QTRY_COMPARE(evaluate("document.querySelectorAll('.instance-select').length").toInt(), 2);
        QCOMPARE(view->url(), QUrl("awake://ui/"));
    }
    void browserSelectionAndPersistentPreference()
    {
        evaluate("document.querySelectorAll('.instance-select')[0].click()");
        QTRY_VERIFY(!APPLICATION->settings()->get("SelectedInstance").toString().isEmpty());
        QTRY_VERIFY(evaluate("document.querySelector('#play') !== null").toBool());
        QTRY_VERIFY(evaluate("(() => { const image = document.querySelector('.artwork-image'); return image && image.complete && image.naturalWidth === 600 && image.src.startsWith('awake://ui/images/'); })()").toBool());
        evaluate("document.querySelector('.pin-button').click()");
        QTRY_COMPARE(APPLICATION->settings()->get("AwakePinnedInstances").toStringList().size(), 1);
        evaluate("document.querySelector('.view-options').open = true; const c = document.querySelectorAll('.options-panel input[type=checkbox]')[2]; c.click()");
        QTRY_VERIFY(APPLICATION->settings()->get("AwakeReduceMotion").toBool());
        QTRY_VERIFY(evaluate("document.querySelector('.launcher').classList.contains('reduced-motion')").toBool());
        QCOMPARE(APPLICATION->instances()->count(), 2);
    }
    void nativeSearchShortcut()
    {
        view->setFocus();
        QTest::keyClick(view, Qt::Key_F, Qt::ControlModifier);
        QTRY_COMPARE(evaluate("document.activeElement.id").toString(), QString("instance-search"));
    }
    void nativeFolderUsesSelectedInstanceOnly()
    {
        QDesktopServices::setUrlHandler("file", this, "recordFolder");
        const auto id = APPLICATION->settings()->get("SelectedInstance").toString();
        QVERIFY(bridge->invokeAction("folder", id).value("ok").toBool());
        QTRY_VERIFY(folderUrl.isLocalFile());
        QCOMPARE(folderUrl.toLocalFile(), QFileInfo(APPLICATION->instances()->getInstanceById(id)->instanceRoot()).absoluteFilePath());
        QDesktopServices::unsetUrlHandler("file");
    }
    void nativeDialogsAndMenus()
    {
        for (const auto& action : {QString("create"), QString("import"), QString("settings")}) {
            bool opened = false;
            QTimer closer;
            closer.setInterval(20);
            connect(&closer, &QTimer::timeout, this, [&opened] {
                if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                    opened = true;
                    dialog->reject();
                }
            });
            closer.start();
            QVERIFY(bridge->invokeAction(action, {}).value("ok").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(opened, 5000);
            closer.stop();
        }
        for (const auto& action : {QString("accounts"), QString("application"), QString("manage"), QString("launchOptions")}) {
            bool opened = false;
            QTimer closer;
            closer.setInterval(20);
            connect(&closer, &QTimer::timeout, this, [&opened] {
                if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                    opened = true;
                    menu->close();
                }
            });
            closer.start();
            QVERIFY(bridge->invokeAction(action, APPLICATION->settings()->get("SelectedInstance").toString()).value("ok").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(opened, 5000);
            closer.stop();
        }
    }
    void nativeEditorAndSuspension()
    {
        const auto id = APPLICATION->settings()->get("SelectedInstance").toString();
        QVERIFY(bridge->invokeAction("edit", id).value("ok").toBool());
        InstanceWindow* editor = nullptr;
        QTRY_VERIFY(([&] {
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* candidate = qobject_cast<InstanceWindow*>(widget); candidate && candidate->isVisible()) {
                    editor = candidate;
                    return candidate->instanceId() == id;
                }
            return false;
        })());
        editor->close();
        window->showMinimized();
        QTRY_COMPARE(view->page()->lifecycleState(), QWebEnginePage::LifecycleState::Frozen);
        window->showNormal();
        QTRY_COMPARE(view->page()->lifecycleState(), QWebEnginePage::LifecycleState::Active);
        view->setZoomFactor(2);
        QTRY_VERIFY(evaluate("document.documentElement.scrollWidth <= window.innerWidth").toBool());
        view->setZoomFactor(1);
    }
    void liveLocaleAndRendererBoundary()
    {
        for (const auto& locale : {QString("th"), QString("zh_CN"), QString("zh_TW"), QString("en_US")}) {
            QVERIFY(APPLICATION->translations()->selectLanguage(locale));
            bridge->scheduleState();
            const auto expected = locale == "en_US" ? "en" : locale == "zh_CN" ? "zh-CN" : locale == "zh_TW" ? "zh-TW" : "th";
            QTRY_COMPARE(evaluate("document.documentElement.lang").toString(), QString(expected));
        }
        evaluate("window.location.href = 'file:///C:/Windows/win.ini'");
        QCoreApplication::processEvents();
        QCOMPARE(view->url(), QUrl("awake://ui/"));
        QVERIFY(evaluate("typeof qt.webChannelTransport === 'object'").toBool());
    }
    void closeAndReopenReleasesRenderer()
    {
        const auto id = APPLICATION->settings()->get("SelectedInstance").toString();
        auto* editor = APPLICATION->showInstanceWindow(APPLICATION->instances()->getInstanceById(id));
        editor->show();
        QPointer<QWebEngineView> oldView(view);
        window->close();
        QVERIFY(oldView.isNull());
        window = APPLICATION->showMainWindow(false);
        view = window->findChild<QWebEngineView*>("awakeWebView");
        bridge = window->findChild<Awake::Web::Bridge*>();
        QVERIFY(view && bridge);
        QTRY_VERIFY_WITH_TIMEOUT(view->isVisible(), 20000);
        QTRY_COMPARE(evaluate("document.querySelectorAll('.instance-select').length").toInt(), 2);
        editor->close();
    }
};

static void writeFixture(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("Cannot write shell test fixture");
}

#include "NativeTestAccounts.h"
int main(int argc, char** argv)
{
    const auto original = QDir::currentPath();
    QDir().mkpath(original + "/.validation");
    QTemporaryDir data(original + "/.validation/awake-web-shell-XXXXXX");
    if (!data.isValid()) return 1;
    seedStartupAccount(data.path());
    writeFixture(data.path() + "/awakelauncher.cfg", "Language=en_US\nIgnoreJavaWizard=true\nAutomaticJavaDownload=true\nAutomaticJavaSwitch=true\nUserAskedAboutAutomaticJavaDownload=true\nProxyType=HTTP\nProxyAddr=127.0.0.1\nProxyPort=9\n");
    for (const auto& id : {QString("one"), QString("two")}) {
        const auto path = data.path() + "/instances/" + id;
        QDir().mkpath(path);
        writeFixture(path + "/instance.cfg", "InstanceType=OneSix\nname=" + id.toUtf8() + "\niconKey=grass\n");
        writeFixture(path + "/mmc-pack.json", R"({"formatVersion":1,"components":[{"uid":"net.minecraft","version":"1.21.8","important":true}]})");
        if (id == "one") {
            QDir().mkpath(path + "/.minecraft/screenshots");
            QImage image(600, 400, QImage::Format_RGB32);
            image.fill(Qt::darkGreen);
            if (!image.save(path + "/.minecraft/screenshots/fixture.png")) qFatal("Cannot write artwork test fixture");
        }
    }
    qputenv("AWAKE_FRONTEND", "web");
    Awake::Web::registerScheme();
    Q_INIT_RESOURCE(awake_web);
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
        QTimer::singleShot(0, &app, [&] { AwakeWebShellTest test; result = QTest::qExec(&test, argc, argv); app.quit(); });
        app.exec();
    }
    QDir::setCurrent(original);
    return result;
}
#include "AwakeWebShell_test.moc"
