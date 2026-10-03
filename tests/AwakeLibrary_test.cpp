// SPDX-License-Identifier: GPL-3.0-only
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include "Application.h"
#include "InstanceList.h"
#include "awake/LibraryDelegate.h"
#include "awake/LibraryWidget.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "translations/TranslationsModel.h"
#include "ui/MainWindow.h"
#include "ui/instanceview/InstanceView.h"
#include "ui/pages/global/AccountListPage.h"

class AwakeLibraryTest : public QObject {
    Q_OBJECT
   private slots:
    void initTestCase() { QTRY_COMPARE_WITH_TIMEOUT(APPLICATION->status(), Application::Initialized, 15000); }
    void emptyLibraryStartup()
    {
        if (!qEnvironmentVariableIsSet("AWAKE_TEST_EMPTY_LIBRARY"))
            QSKIP("Covered by the isolated AwakeEmptyLibrary test");
        auto* window = APPLICATION->showMainWindow(false);
        auto* view = window->findChild<InstanceView*>();
        QVERIFY(view);
        QCOMPARE(view->model()->rowCount(), 0);
        QVERIFY(window->isVisible());
        QVERIFY(!window->findChild<QAction*>("actionLaunchInstance")->isEnabled());
        QVERIFY(window->findChild<QAction*>("actionAddInstance")->isEnabled());
        QVERIFY(!window->findChild<QCheckBox*>("awakePin")->isEnabled());
        QVERIFY(!window->findChild<QToolButton*>("awakeMore")->isEnabled());
        const auto output = qEnvironmentVariable("AWAKE_UI_OUTPUT");
        if (!output.isEmpty()) {
            QDir().mkpath(output);
            window->resize(1100, 740);
            QCoreApplication::processEvents();
            QVERIFY(window->grab().save(output + "/glass-empty.png"));
        }
    }
    void settingsRefreshKeepsNewShell()
    {
        auto* window = APPLICATION->showMainWindow(false);
        APPLICATION->settings()->set("MenuBarInsteadOfToolBar", false);
        QVERIFY(QMetaObject::invokeMethod(window, "globalSettingsClosed", Qt::DirectConnection));
        for (const auto& name : { "mainToolBar", "newsToolBar", "instanceToolBar" }) {
            auto* toolbar = window->findChild<QToolBar*>(name);
            QVERIFY(toolbar);
            QVERIFY(!toolbar->isVisible());
        }
    }
    void searchSelectionPinAndViewMode()
    {
        auto* window = APPLICATION->showMainWindow(false);
        auto* search = window->findChild<QLineEdit*>("awakeSearch");
        auto* mode = window->findChild<QComboBox*>("awakeViewMode");
        auto* pin = window->findChild<QCheckBox*>("awakePin");
        auto* pinnedOnly = window->findChild<QCheckBox*>("awakePinnedOnly");
        auto* view = window->findChild<InstanceView*>();
        QVERIFY(search && mode && pin && pinnedOnly && view);
        QCOMPARE(view->model()->rowCount(), 2);
        auto* delegate = dynamic_cast<Awake::LibraryDelegate*>(view->itemDelegate());
        QVERIFY(delegate);
        for (int row = 0; row < view->model()->rowCount(); ++row)
            QVERIFY(delegate->metadata(view->model()->index(row, 0)).contains("Minecraft 1.21.8"));
        search->setText("fabric");
        QCOMPARE(view->model()->rowCount(), 1);
        QVERIFY(delegate->metadata(view->model()->index(0, 0)).contains("Fabric 0.16.14"));
        view->setCurrentIndex(view->model()->index(0, 0));
        QCOMPARE(view->currentIndex().data().toString(), QString("Fabric fixture"));
        QVERIFY(pin->isEnabled());
        pin->setChecked(true);
        QVERIFY(APPLICATION->settings()->get("AwakePinnedInstances").toStringList().contains("fabric"));
        search->clear();
        pinnedOnly->setChecked(true);
        QCOMPARE(view->model()->rowCount(), 1);
        pinnedOnly->setChecked(false);
        QCOMPARE(view->model()->rowCount(), 2);
        mode->setCurrentIndex(1);
        QVERIFY(APPLICATION->settings()->get("AwakeCompactLibrary").toBool());
        QCoreApplication::processEvents();
        const auto listRect = view->visualRect(view->model()->index(0, 0));
        QVERIFY(listRect.width() > 176);
        mode->setCurrentIndex(0);
        QCoreApplication::processEvents();
        QCOMPARE(view->visualRect(view->model()->index(0, 0)).width(), 176);
        const auto output = qEnvironmentVariable("AWAKE_UI_OUTPUT");
        if (!output.isEmpty()) {
            QDir().mkpath(output);
            window->resize(1100, 740);
            QCoreApplication::processEvents();
            QVERIFY(window->grab().save(output + "/library-grid.png"));
            mode->setCurrentIndex(1);
            QCoreApplication::processEvents();
            QVERIFY(window->grab().save(output + "/library-list.png"));
        }
        search->setText("no matching instance");
        QCOMPARE(view->model()->rowCount(), 0);
        QVERIFY(!pin->isEnabled());
        QVERIFY(!window->findChild<QAction*>("actionLaunchInstance")->isEnabled());
        QVERIFY(!window->findChild<QToolButton*>("awakeMore")->isEnabled());
        search->clear();
        QCOMPARE(view->model()->rowCount(), 2);
    }
    void javaDetailsFollowLaunchPolicy()
    {
        auto* window = APPLICATION->showMainWindow(false);
        auto* view = window->findChild<InstanceView*>();
        auto* settings = APPLICATION->instances()->getInstanceById("fabric")->settings();
        const auto configuredPath = APPLICATION->settings()->get("JavaPath").toString();
        QVERIFY(QFile::exists(configuredPath));
        auto runtimeText = [&] {
            view->setCurrentIndex(QModelIndex());
            view->setCurrentIndex(view->model()->index(0, 0));
            for (auto* label : window->findChildren<QLabel*>()) {
                if (label->text().startsWith("Java: "))
                    return label->text().section('\n', 0, 0);
            }
            return QString();
        };
        auto* search = window->findChild<QLineEdit*>("awakeSearch");
        search->setText("fabric");
        settings->set("OverrideJavaLocation", false);
        settings->set("AutomaticJava", false);
        QCOMPARE(runtimeText(), QString("Java: Automatic (selected at launch)"));
        settings->set("OverrideJavaLocation", true);
        settings->set("JavaPath", configuredPath);
        settings->set("AutomaticJava", true);
        QCOMPARE(runtimeText(), "Java: " + configuredPath);
        settings->set("JavaPath", QDir::currentPath() + "/missing-java");
        QCOMPARE(runtimeText(), QString("Java: Automatic (selected at launch)"));
        APPLICATION->settings()->set("AutomaticJavaSwitch", false);
        QCOMPARE(runtimeText(), "Java: " + settings->get("JavaPath").toString());
        settings->set("OverrideJavaLocation", false);
        settings->set("AutomaticJava", false);
        QApplication::processEvents();
        QCOMPARE(runtimeText(), "Java: " + configuredPath);
        APPLICATION->settings()->set("AutomaticJavaSwitch", true);
        search->clear();
    }
    void authenticationConfigurationRefreshes()
    {
        AccountListPage page;
        auto* action = page.findChild<QAction*>("actionAddMicrosoft");
        QVERIFY(action);
        QVERIFY(!action->isEnabled());
        QVERIFY(!APPLICATION->capabilities().testFlag(Application::SupportsMSA));
        APPLICATION->settings()->set("MSAClientIDOverride", "test-public-client-id");
        QVERIFY(action->isEnabled());
        QVERIFY(APPLICATION->capabilities().testFlag(Application::SupportsMSA));
        APPLICATION->settings()->set("MSAClientIDOverride", "");
        QVERIFY(!action->isEnabled());
        QVERIFY(!APPLICATION->capabilities().testFlag(Application::SupportsMSA));
    }
    void screenshotSelectionAndReducedMotion()
    {
        auto* window = APPLICATION->showMainWindow(false);
        auto* library = window->findChild<Awake::LibraryWidget*>();
        auto* view = window->findChild<InstanceView*>();
        auto* search = window->findChild<QLineEdit*>("awakeSearch");
        auto* reduced = window->findChild<QAction*>("awakeReduceMotion");
        QVERIFY(library && view && search && reduced);
        reduced->setChecked(true);
        QVERIFY(APPLICATION->settings()->get("AwakeReduceMotion").toBool());
        search->setText("fabric");
        view->setCurrentIndex(view->model()->index(0, 0));
        QTRY_VERIFY_WITH_TIMEOUT(library->artworkPath().contains("fabric"), 5000);
        QVERIFY(!library->animationRunning());
        const auto first = library->artworkPath();
        search->setText("vanilla");
        view->setCurrentIndex(view->model()->index(0, 0));
        search->setText("fabric");
        view->setCurrentIndex(view->model()->index(0, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!library->artworkLoading(), 5000);
        QVERIFY(library->artworkPath().contains("fabric"));
        QVERIFY(library->artworkPath() != first);
        const auto output = qEnvironmentVariable("AWAKE_UI_OUTPUT");
        if (!output.isEmpty()) {
            QDir().mkpath(output);
            window->resize(1200, 760);
            QCoreApplication::processEvents();
            QVERIFY(window->grab().save(output + "/glass-world.png"));
            window->resize(900, 640);
            QCoreApplication::processEvents();
            QVERIFY(window->grab().save(output + "/glass-compact.png"));
        }
        search->setText("no instance");
        QCOMPARE(library->artworkPath(), QString());
        QCOMPARE(library->artworkInstance(), QString());
        reduced->setChecked(false);
        search->clear();
        view->setCurrentIndex(view->model()->index(0, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!library->artworkLoading(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!library->animationRunning(), 1500);
        QVERIFY(!APPLICATION->settings()->get("AwakeReduceMotion").toBool());
    }
    void bundledLanguagesSwitchWithoutDownloads()
    {
        auto* translations = APPLICATION->translations();
        QCOMPARE(translations->rowCount(), 4);
        auto* window = APPLICATION->showMainWindow(false);
        auto* heading = window->findChild<QLabel*>("awakeHeading");
        QVERIFY(heading);
        const QMap<QString, QString> names{ { "th", QString::fromUtf8("คลังอินสแตนซ์") },
                                            { "zh_CN", QString::fromUtf8("实例库") },
                                            { "zh_TW", QString::fromUtf8("實例庫") },
                                            { "en_US", "Library" } };
        for (auto i = names.begin(); i != names.end(); ++i) {
            QVERIFY(translations->selectLanguage(i.key()));
            QTRY_COMPARE(heading->text(), i.value());
            QCoreApplication::processEvents();
            const auto output = qEnvironmentVariable("AWAKE_UI_OUTPUT");
            if (!output.isEmpty())
                QVERIFY(window->grab().save(output + "/library-" + i.key() + ".png"));
        }
        QVERIFY(translations->selectLanguage("en_US"));
    }
};

static void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("Cannot write UI test fixture");
}

int main(int argc, char** argv)
{
    const auto originalPath = QDir::currentPath();
    QTemporaryDir data(originalPath + "/awake-ui-XXXXXX");
    if (!data.isValid())
        return 1;
    const QString path = data.path();
    writeFile(
        path + "/awakelauncher.cfg",
        QByteArray("Language=en_US\nApplicationTheme=awake-dark\nIconTheme=pe_light\n"
                   "IgnoreJavaWizard=true\nAutomaticJavaDownload=true\nAutomaticJavaSwitch=true\nUserAskedAboutAutomaticJavaDownload=true\n"
                   "ProxyType=HTTP\nProxyAddr=127.0.0.1\nProxyPort=9\nJavaPath=") +
            QByteArray(AWAKE_TEST_JAVA_PATH) + "\n");
    const QStringList fixtureIds =
        qEnvironmentVariableIsSet("AWAKE_TEST_EMPTY_LIBRARY") ? QStringList{} : QStringList{ "vanilla", "fabric" };
    for (const auto& id : fixtureIds) {
        const auto instance = path + "/instances/" + id;
        QDir().mkpath(instance);
        writeFile(instance + "/instance.cfg",
                  "InstanceType=OneSix\nname=" + (id == "fabric" ? QByteArray("Fabric fixture") : QByteArray("Vanilla fixture")) +
                      "\niconKey=grass\n");
        QJsonArray components{ QJsonObject{ { "uid", "net.minecraft" }, { "version", "1.21.8" }, { "important", true } } };
        if (id == "fabric")
            components.append(QJsonObject{ { "uid", "net.fabricmc.fabric-loader" }, { "version", "0.16.14" } });
        writeFile(instance + "/mmc-pack.json", QJsonDocument(QJsonObject{ { "formatVersion", 1 }, { "components", components } }).toJson());
        const auto screenshots = instance + "/.minecraft/screenshots";
        QDir().mkpath(screenshots);
        // Synthetic color fixtures verify local artwork selection, not Minecraft rendering.
        QImage image(1200, 760, QImage::Format_RGB32);
        image.fill(id == "fabric" ? QColor("#58766e") : QColor("#8a6f4b"));
        if (!image.save(screenshots + "/one.png"))
            qFatal("Cannot write screenshot fixture");
        image.fill(id == "fabric" ? QColor("#384c62") : QColor("#6f8757"));
        if (!image.save(screenshots + "/two.png"))
            qFatal("Cannot write screenshot fixture");
    }
    qputenv("AWAKELAUNCHER_DATA_DIR", path.toUtf8());
    // Application parses its own command line; QtTest arguments belong only to qExec.
    int applicationArgc = 1;
    int result = 1;
    {
        Application app(applicationArgc, argv);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen") {
            QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/segoeui.ttf");
            QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/segoeuib.ttf");
            QApplication::setFont(QFont("Segoe UI", 9));
        }
#endif
        Q_INIT_RESOURCE(multimc);
        Q_INIT_RESOURCE(backgrounds);
        Q_INIT_RESOURCE(documents);
        Q_INIT_RESOURCE(awakelauncher);
        Q_INIT_RESOURCE(awake_translations);
        Q_INIT_RESOURCE(pe_dark);
        Q_INIT_RESOURCE(pe_light);
        Q_INIT_RESOURCE(pe_colored);
        Q_INIT_RESOURCE(pe_blue);
        Q_INIT_RESOURCE(breeze_dark);
        Q_INIT_RESOURCE(breeze_light);
        Q_INIT_RESOURCE(OSX);
        Q_INIT_RESOURCE(iOS);
        Q_INIT_RESOURCE(flat);
        Q_INIT_RESOURCE(flat_white);
        Q_INIT_RESOURCE(shaders);
        QTimer temporaryPathWarning;
        temporaryPathWarning.setInterval(20);
        QObject::connect(&temporaryPathWarning, &QTimer::timeout, &app, [&] {
            if (app.status() == Application::Initialized) {
                temporaryPathWarning.stop();
            } else if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                // Sandboxed build directories can be below the OS temp root; fixtures never launch a game.
                if (warning->text().startsWith("Your instance folder is in a temporary folder:"))
                    warning->accept();
            }
        });
        temporaryPathWarning.start();
        QTimer::singleShot(0, &app, [&] {
            AwakeLibraryTest test;
            result = QTest::qExec(&test, argc, argv);
            app.quit();
        });
        app.exec();
    }
    QDir::setCurrent(originalPath);
    return result;
}
#include "AwakeLibrary_test.moc"
