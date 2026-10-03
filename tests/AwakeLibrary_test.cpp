// SPDX-License-Identifier: GPL-3.0-only
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
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
#include <QToolButton>
#include "Application.h"
#include "InstanceList.h"
#include "awake/LibraryDelegate.h"
#include "settings/SettingsObject.h"
#include "translations/TranslationsModel.h"
#include "ui/MainWindow.h"
#include "ui/instanceview/InstanceView.h"
#include "ui/pages/global/AccountListPage.h"

class AwakeLibraryTest : public QObject {
    Q_OBJECT
   private slots:
    void initTestCase() { QTRY_COMPARE_WITH_TIMEOUT(APPLICATION->status(), Application::Initialized, 15000); }
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
    for (const auto& id : { QString("vanilla"), QString("fabric") }) {
        const auto instance = path + "/instances/" + id;
        QDir().mkpath(instance);
        writeFile(instance + "/instance.cfg",
                  "InstanceType=OneSix\nname=" + (id == "fabric" ? QByteArray("Fabric fixture") : QByteArray("Vanilla fixture")) +
                      "\niconKey=grass\n");
        QJsonArray components{ QJsonObject{ { "uid", "net.minecraft" }, { "version", "1.21.8" }, { "important", true } } };
        if (id == "fabric")
            components.append(QJsonObject{ { "uid", "net.fabricmc.fabric-loader" }, { "version", "0.16.14" } });
        writeFile(instance + "/mmc-pack.json", QJsonDocument(QJsonObject{ { "formatVersion", 1 }, { "components", components } }).toJson());
    }
    qputenv("AWAKELAUNCHER_DATA_DIR", path.toUtf8());
    // Application parses its own command line; QtTest arguments belong only to qExec.
    int applicationArgc = 1;
    int result = 1;
    {
        Application app(applicationArgc, argv);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen")
            QApplication::setFont(QFont("Segoe UI", 9));
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
