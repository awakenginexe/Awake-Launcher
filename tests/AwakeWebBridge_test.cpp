// SPDX-License-Identifier: GPL-3.0-only
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QPushButton>
#include <QFontDatabase>
#include <QFontInfo>
#include <QRawFont>
#include <QTextLayout>
#include <QLabel>
#include <QCryptographicHash>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <memory>
#include "Application.h"
#include "InstanceList.h"
#include "awake/web/AwakeWebAssets.h"
#include "awake/AwakeTheme.h"
#include "awake/web/AwakeWebBridge.h"
#include "awake/web/AwakePackCatalog.h"
#include "InstanceImportTask.h"
#include "modplatform/atlauncher/ATLPackInstallTask.h"
#include "modplatform/ftb/FTBPackInstallTask.h"
#include "modplatform/import_ftb/PackInstallTask.h"
#include "modplatform/legacy_ftb/PackInstallTask.h"
#include "modplatform/technic/SingleZipPackInstallTask.h"
#include "modplatform/technic/SolderPackInstallTask.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "translations/TranslationsModel.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/BlockedModsDialog.h"
#include "ui/dialogs/AwakePopupDialog.h"
#include "java/RuntimeSelection.h"
#include "minecraft/launch/AutoInstallJava.h"
#include "launch/LaunchTask.h"
#include "java/JavaUtils.h"
#include "SysInfo.h"
#include "java/download/ArchiveDownloadTask.h"
#include "net/HttpMetaCache.h"

using namespace Awake::Web;
class AwakeWebBridgeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QTRY_COMPARE_WITH_TIMEOUT(APPLICATION->status(), Application::Initialized, 15000);
        QVERIFY(QFontDatabase::families().contains("K2D"));
        const auto font = QRawFont::fromFont(QFont("K2D", 12, QFont::Normal));
        QCOMPARE(font.familyName(), QString("K2D"));
        QCOMPARE(font.styleName(), QString("Regular"));
        QVERIFY(font.supportsCharacter(0x0e20));
        QVERIFY(font.supportsCharacter('A'));
    }
    void editorUsesRealDataAndRejectsUnsafeCommands()
    {
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        QVERIFY(!bridge.instanceDetails("missing", "notes").value("ok").toBool());
        QVERIFY(!bridge.instanceDetails("one", "../").value("ok").toBool());
        const auto versions = bridge.instanceDetails("one", "versions").value("rows").toList();
        QVERIFY(!versions.isEmpty());
        QVERIFY(bridge.instanceCommand("one", "saveNotes", "A real saved note").value("ok").toBool());
        QCOMPARE(bridge.instanceDetails("one", "notes").value("text").toString(), QString("A real saved note"));
        QVERIFY(!bridge.instanceCommand("one", "executeCommand", "whoami").value("ok").toBool());
        QVERIFY(!bridge.instanceCommand("one", "removeFile", QVariantMap{{"section", "mods"}, {"id", "../../instance.cfg"}}).value("ok").toBool());
        auto settings = bridge.instanceDetails("one", "settings").value("settings").toMap();
        settings["minMemory"] = 512;
        settings["maxMemory"] = 2048;
        settings["width"] = 1280;
        settings["height"] = 720;
        settings["overrideMemory"] = true;
        settings["overrideWindow"] = true;
        settings["fullscreen"] = false;
        QVERIFY(bridge.instanceCommand("one", "saveSettings", settings).value("ok").toBool());
        QCOMPARE(APPLICATION->instances()->getInstanceById("one")->settings()->get("MaxMemAlloc").toInt(), 2048);
        settings["minMemory"] = 4096;
        QVERIFY(!bridge.instanceCommand("one", "saveSettings", settings).value("ok").toBool());
        auto* instance = APPLICATION->instances()->getInstanceById("one");
        instance->setRunning(true);
        settings["minMemory"] = 512;
        QVERIFY(!bridge.instanceCommand("one", "saveSettings", settings).value("ok").toBool());
        QVERIFY(!bridge.instanceCommand("one", "toggleMod", QVariantMap{{"id", "file.jar"}, {"enabled", false}}).value("ok").toBool());
        instance->setRunning(false);
    }
    void javaInheritanceAndLaunchPrecedence()
    {
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        auto* global = APPLICATION->settings();
        auto* instance = APPLICATION->instances()->getInstanceById("one");
        auto* settings = instance->settings();
        QVERIFY(bridge.setJavaProfile({}, "awake").value("ok").toBool());
        QVERIFY(bridge.setJavaProfile("one", "inherit").value("ok").toBool());
        QVERIFY(bridge.javaSettings("one").value("inherited").toBool());
        QVERIFY(bridge.setJavaProfile({}, "microsoft").value("ok").toBool());
        QCOMPARE(bridge.javaSettings("one").value("profile").toString(), QString("microsoft"));
        QVERIFY(bridge.setJavaProfile("one", "minecraft").value("ok").toBool());
        QVERIFY(bridge.setJavaProfile({}, "graalvm").value("ok").toBool());
        QCOMPARE(bridge.javaSettings("one").value("profile").toString(), QString("minecraft"));
        QVERIFY(!bridge.setJavaProfile("one", "maximum-fps").value("ok").toBool());
        QVERIFY(!bridge.setJavaProfile("missing", "awake").value("ok").toBool());
        instance->setRunning(true);
        QVERIFY(!bridge.setJavaProfile("one", "zulu").value("ok").toBool());
        instance->setRunning(false);
        QVERIFY(bridge.setJavaProfile("one", "inherit").value("ok").toBool());
        settings->set("OverrideJavaLocation", true);
        settings->set("AutomaticJava", false);
        settings->set("JavaPath", "C:/existing-user-java/bin/java.exe");
        QCOMPARE(bridge.javaSettings("one").value("profile").toString(), QString("custom"));
        QVERIFY(!bridge.javaSettings("one").value("inherited").toBool());
        QVERIFY(!bridge.setJavaProfile("one", "custom").value("ok").toBool());

        global->set("JavaPath", "C:/global-custom/bin/java.exe");
        global->set("AutomaticJavaSwitch", false);
        global->set("AwakeJavaProfile", "custom");
        settings->set("AutomaticJava", true);
        settings->set("JavaPath", "C:/previous-managed/bin/java.exe");
        QCOMPARE(bridge.javaSettings("one").value("path").toString(), QString("C:/global-custom/bin/java.exe"));
        auto launch = LaunchTask::create(instance);
        instance->setRunning(false);
        AutoInstallJava step(launch.get());
        QObject::disconnect(&step, nullptr, launch.get(), nullptr);
        step.start();
        QVERIFY(step.wasSuccessful());
        QVERIFY(!settings->get("OverrideJavaLocation").toBool());
        QCOMPARE(settings->get("JavaPath").toString(), QString("C:/global-custom/bin/java.exe"));
        QVERIFY(bridge.setJavaProfile({}, "awake").value("ok").toBool());
        QVERIFY(bridge.setJavaProfile("one", "inherit").value("ok").toBool());
    }
    void runtimeMetadataRejectsIncompatibleAndUntrustedPackages()
    {
        QJsonObject p{{"distribution", "temurin"}, {"major_version", 21}, {"operating_system", "windows"},
            {"architecture", "x64"}, {"archive_type", "zip"}, {"release_status", "ga"}, {"package_type", "jdk"},
            {"directly_downloadable", true}, {"javafx_bundled", false}, {"id", "0123456789abcdef0123456789abcdef"}};
        QVERIFY(!Java::selectRuntimePackage({p}, "temurin", 21, "windows", "x64").isEmpty());
        QVERIFY(Java::selectRuntimePackage({p}, "microsoft", 21, "windows", "x64").isEmpty());
        QVERIFY(Java::selectRuntimePackage({p}, "temurin", 17, "windows", "x64").isEmpty());
        QVERIFY(Java::selectRuntimePackage({p}, "temurin", 21, "windows", "aarch64").isEmpty());
        p["release_status"] = "ea";
        QVERIFY(Java::selectRuntimePackage({p}, "temurin", 21, "windows", "x64").isEmpty());
        QVERIFY(Java::runtimeDownloadUrlAllowed(QUrl("https://aka.ms/download-jdk/java.zip"), "microsoft"));
        QVERIFY(!Java::runtimeDownloadUrlAllowed(QUrl("https://aka.ms.evil.test/download-jdk/java.zip"), "microsoft"));
        QVERIFY(!Java::runtimeDownloadUrlAllowed(QUrl("http://aka.ms/download-jdk/java.zip"), "microsoft"));
        QVERIFY(!Java::runtimeDownloadUrlAllowed(QUrl("https://github.com/other/java.zip"), "temurin"));
        const QByteArray hash(64, 'a');
        QCOMPARE(Java::runtimeChecksum(hash + " *jdk.zip\n"), QString::fromLatin1(hash));
        QVERIFY(Java::runtimeChecksum("not a checksum").isEmpty());
        Java::RuntimeDownloadTask task("graalvm", {}, "windows-x64", {});
        task.start();
        QVERIFY(task.isFinished());
        QVERIFY(!task.wasSuccessful());
        QCOMPARE(Java::runtimeDistribution("awake"), QString("temurin"));
        QVERIFY(Java::runtimeDistribution("minecraft").isEmpty());
    }
    void liveJavaDownloadAndOfflineReuse()
    {
        if (!qEnvironmentVariableIsSet("AWAKE_TEST_JAVA_LIVE")) QSKIP("Live Java installation is opt-in.");
        QTemporaryDir runtimes;
        QVERIFY(runtimes.isValid());
        QVERIFY(!JavaUtils::getJavaCheckPath().isEmpty());
        const auto previousProxy = APPLICATION->network()->proxy();
        APPLICATION->network()->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        Java::RuntimeDownloadTask download("temurin", {21}, SysInfo::getSupportedJavaArchitecture(), runtimes.path());
        download.start();
        QTRY_VERIFY_WITH_TIMEOUT(download.isFinished(), 180000);
        APPLICATION->network()->setProxy(previousProxy);
        QVERIFY2(download.wasSuccessful(), qPrintable(download.failReason()));
        QVERIFY(QFileInfo(download.javaPath()).isFile());
        QFile marker(QDir(QFileInfo(download.javaPath()).absolutePath()).filePath("../awake-runtime.json"));
        QVERIFY(marker.open(QIODevice::ReadOnly));
        const auto info = QJsonDocument::fromJson(marker.readAll()).object();
        QCOMPARE(info["distribution"].toString(), QString("temurin"));
        QCOMPARE(info["major"].toInt(), 21);
        Java::RuntimeDownloadTask cached("awake", {21}, SysInfo::getSupportedJavaArchitecture(), runtimes.path());
        cached.start();
        QTRY_VERIFY_WITH_TIMEOUT(cached.isFinished(), 20000);
        QVERIFY(cached.wasSuccessful());
        QCOMPARE(cached.javaPath(), download.javaPath());
    }
    void newRuntimeDownloadsCannotSkipChecksumUsingOldCache()
    {
        QTemporaryDir stage;
        QVERIFY(stage.isValid());
        const auto entry = APPLICATION->metacache()->resolveEntry("java", "awake-java-integrity-test.zip");
        QDir().mkpath(QFileInfo(entry->getFullPath()).absolutePath());
        QFile cached(entry->getFullPath());
        QVERIFY(cached.open(QIODevice::WriteOnly));
        cached.write("old cached content"); cached.close();
        entry->setStale(false); entry->makeEternal(true);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int requests = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto request = socket->readAll();
                if (!request.contains("GET")) return;
                ++requests;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 16\r\nConnection: close\r\n\r\nuntrusted bytes!");
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        struct ProxyRestore {
            QNetworkAccessManager* network;
            QNetworkProxy proxy;
            ~ProxyRestore() { network->setProxy(proxy); }
        } restore{APPLICATION->network(), APPLICATION->network()->proxy()};
        APPLICATION->network()->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        Java::ArchiveDownloadTask download(QUrl(QString("http://127.0.0.1:%1/awake-java-integrity-test.zip").arg(server.serverPort())),
                                           stage.path(), "sha256", QString(64, 'a'), false);
        download.start();
        QTRY_VERIFY_WITH_TIMEOUT(download.isFinished(), 30000);
        QVERIFY(requests > 0);
        QVERIFY(!download.wasSuccessful());
        QVERIFY(!QFileInfo(QDir(stage.path()).filePath("bin/javaw.exe")).exists());
        QVERIFY(!QFileInfo(QDir(stage.path()).filePath(".awake-java-download")).exists());
    }
    void nativePopupsShowActionsAndKeepHashMatching()
    {
        auto* window = APPLICATION->showMainWindow(false);
        window->resize(1200, 800);
        window->show();
        APPLICATION->settings()->set("AwakeReduceMotion", true);
        const auto output = QDir(QCoreApplication::applicationDirPath()).filePath(".validation");
        QDir().mkpath(output);
        QTemporaryDir downloads;
        const auto previousDownloads = APPLICATION->settings()->get("DownloadsDir");
        APPLICATION->settings()->set("DownloadsDir", downloads.path());
        QList<BlockedMod> mods{{"Required-resource-pack.zip", "https://www.curseforge.com/minecraft/texture-packs",
            QString::fromLatin1(QCryptographicHash::hash("verified-file", QCryptographicHash::Sha1).toHex()), false, {}, "resourcepacks"}};
        BlockedModsDialog popup(window, "Download required files", {}, mods);
        popup.show();
        QCoreApplication::processEvents();
        QVERIFY(popup.windowFlags().testFlag(Qt::FramelessWindowHint));
        bool hasDownloadButton = false;
        for (auto* button : popup.findChildren<QPushButton*>()) if (button->text() == "Download") hasDownloadButton = true;
        QVERIFY(hasDownloadButton);
        QVERIFY(popup.grab().save(output + "/download-popup-current.png"));
        QFile correct(downloads.path() + "/Required-resource-pack.zip");
        QVERIFY(correct.open(QIODevice::WriteOnly));
        correct.write("verified-file");
        correct.close();
        QTRY_VERIFY_WITH_TIMEOUT(mods.first().matched, 5000);
        QTRY_VERIFY(!popup.isVisible());
        QCOMPARE(popup.result(), static_cast<int>(QDialog::Accepted));
        APPLICATION->settings()->set("DownloadsDir", previousDownloads);
        QMetaObject::invokeMethod(window, "instanceSelectRequest", Qt::DirectConnection, Q_ARG(QString, QString("one")));
        bool captured = false;
        QTimer::singleShot(0, window, [&captured, output] {
            auto* confirmation = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (confirmation && confirmation->objectName() == "awakeDeleteConfirmation") {
                captured = confirmation->grab().save(output + "/delete-popup-current.png");
                if (auto* keep = confirmation->findChild<QPushButton*>("keepInstance")) keep->click();
            }
        });
        QMetaObject::invokeMethod(window, "on_actionDeleteInstance_triggered", Qt::DirectConnection);
        QVERIFY(captured);
        QVERIFY(APPLICATION->instances()->getInstanceById("one"));
    }
    void languagesSwitchInTheNativeTranslatorAndSnapshot()
    {
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        for (const auto& locale : QStringList{"zh-CN", "zh-TW", "th", "en"}) {
            QVERIFY(bridge.setPreference("language", locale).value("ok").toBool());
            QCOMPARE(bridge.snapshot().value("locale").toString(), locale);
            QCOMPARE(APPLICATION->settings()->get("Language").toString(),
                locale == "zh-CN" ? QString("zh_CN") : locale == "zh-TW" ? QString("zh_TW") : locale == "en" ? QString("en_US") : locale);
            QCOMPARE(Awake::useRegularUiFont(), locale == "en" || locale == "th");
            if (locale != "en") {
                QVERIFY(QCoreApplication::translate("MainWindow", "Delete this instance?") != "Delete this instance?");
                QVERIFY(QCoreApplication::translate("BlockedModsDialog", "Download required files") != "Download required files");
                QVERIFY(QCoreApplication::translate("AwakePopupDialog", "Close dialog") != "Close dialog");
                QVERIFY(QCoreApplication::translate("Awake::Web::InstanceEditor", "This instance no longer exists.") != "This instance no longer exists.");
            }
            auto* parent = APPLICATION->showMainWindow(false);
            QList<BlockedMod> missing{{"Test.zip", "https://www.curseforge.com/minecraft/texture-packs", "abcd", false, {}, "resourcepacks"}};
            BlockedModsDialog popup(parent, {}, {}, missing);
            popup.show();
            QCoreApplication::processEvents();
            const auto proceedText = QCoreApplication::translate("BlockedModsDialog", "Continue without missing files");
            const auto cancelText = QCoreApplication::translate("BlockedModsDialog", "Cancel installation");
            bool proceedFound = false, cancelFound = false;
            for (auto* button : popup.findChildren<QPushButton*>()) {
                proceedFound |= button->text() == proceedText;
                cancelFound |= button->text() == cancelText;
            }
            QVERIFY(proceedFound);
            QVERIFY(cancelFound);
            if (locale.startsWith("zh")) {
                QTextLayout text(QStringLiteral("中文"), QFont("K2D", 12));
                text.beginLayout();
                text.createLine();
                text.endLayout();
                QVERIFY(!text.glyphRuns().isEmpty());
                for (const auto& run : text.glyphRuns())
                    for (const auto glyph : run.glyphIndexes()) QVERIFY(glyph != 0);
            }
            auto* close = popup.findChild<QPushButton*>("awakePopupClose");
            QVERIFY(close);
            QVERIFY(close->text().isEmpty());
            QVERIFY(!close->icon().isNull());
            QCOMPARE(close->size(), QSize(36, 36));
            if (locale == "en" || locale == "th") {
                QCOMPARE(QFontInfo(close->font()).family(), QString("K2D"));
                QCOMPARE(close->font().weight(), QFont::Normal);
            }
            const auto output = QDir(QCoreApplication::applicationDirPath()).filePath(".validation");
            QVERIFY(popup.grab().save(output + "/download-popup-" + locale + ".png"));
            popup.reject();
            for (const auto* source : {"Add an account to play", "Sign in with the Microsoft account that owns Minecraft. You can add it in the Accounts menu, then come back and press Play.", "Not now", "Add account"}) {
                if (locale != "en") QVERIFY(QCoreApplication::translate("AwakePopupDialog", source) != source);
            }
            bool accountCaptured = false;
            QTimer::singleShot(50, parent, [&] {
                auto* account = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!account || account->objectName() != "awakeAccountRequired") return;
                QCoreApplication::processEvents();
                account->repaint();
                accountCaptured = account->grab().save(output + "/account-required-" + locale + ".png");
                account->findChild<QPushButton*>("accountSetupLater")->click();
            });
            QVERIFY(!AwakePopupDialog::confirmAccountSetup(parent));
            QVERIFY(accountCaptured);
        }
        QVERIFY(!bridge.setPreference("language", "invalid").value("ok").toBool());
        QCOMPARE(bridge.snapshot().value("locale").toString(), QString("en"));
    }
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
        bridge->setArtworkFocused(true);
        QCoreApplication::processEvents();
        QVERIFY(bridge->selectInstance("two").value("ok").toBool());
        artwork.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!artwork.isEmpty(), 5000);
        for (const auto& event : artwork) QCOMPARE(event.at(0).toString(), QString("two"));
        QVERIFY(artwork.last().at(1).toString().startsWith("awake://ui/images/"));
        QVERIFY(artwork.last().at(2).toString().isEmpty());
        QVERIFY(bridge->selectInstance("one").value("ok").toBool());
        QCoreApplication::processEvents();
        bridge.reset();
        QCoreApplication::processEvents();
    }
    void artworkRefreshTimerPausesAndResumesWithFocus()
    {
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        auto* timer = bridge.findChild<QTimer*>("awakeArtworkTimer");
        QVERIFY(timer);
        QCOMPARE(timer->interval(), 60'000);
        QVERIFY(!timer->isActive());
        bridge.setArtworkFocused(true);
        QVERIFY(timer->isActive());
        bridge.setActive(false);
        QVERIFY(!timer->isActive());
        bridge.setActive(true);
        QVERIFY(timer->isActive());
        bridge.setArtworkFocused(false);
        QVERIFY(!timer->isActive());
        bridge.setArtworkFocused(true);
        QVERIFY(timer->isActive());
    }
    void providerRejectsUntrustedSelections()
    {
        Assets assets;
        PackCatalog catalog(&assets);
        QSignalSpy replies(&catalog, &PackCatalog::finished);
        const QStringList providers{"modrinth", "curseforge", "atlauncher", "ftb", "ftb-legacy", "ftb-app", "technic"};
        for (const auto& provider : providers) {
            QString error;
            QVERIFY(!catalog.createTask(provider, "untrusted", "untrusted", nullptr, &error));
            QVERIFY(!error.isEmpty());
            catalog.versions(provider, provider, "untrusted");
        }
        QTRY_COMPARE(replies.size(), providers.size());
        for (const auto& reply : replies) {
            const auto result = reply.at(1).toMap();
            QVERIFY(!result.value("ok").toBool());
            QVERIFY(!result.value("error").toString().isEmpty());
            QVERIFY(!result.contains("versions"));
        }
        replies.clear();
        catalog.search("unknown", "executeCommand", {}, 0);
        catalog.search("offset", "modrinth", {}, -1);
        QTRY_COMPARE(replies.size(), 2);
    }
    void providerFailuresTerminateWithoutDialogsOrFakePacks()
    {
        QTcpServer proxy;
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        connect(&proxy, &QTcpServer::newConnection, &proxy, [&proxy] {
            while (auto* socket = proxy.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, socket, [socket, buffer] {
                    buffer->append(socket->readAll());
                    if (!buffer->contains("\r\n\r\n"))
                        return;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 4\r\nConnection: close\r\n\r\noops");
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        auto network = APPLICATION->network();
        const auto previousProxy = network->proxy();
        network->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", proxy.serverPort()));
        struct RestoreProxy {
            QNetworkAccessManager* network;
            QNetworkProxy proxy;
            ~RestoreProxy() { network->setProxy(proxy); }
        } restore{network, previousProxy};
        Assets assets;
        PackCatalog catalog(&assets);
        QSignalSpy replies(&catalog, &PackCatalog::finished);
        const QStringList providers{"modrinth", "curseforge", "atlauncher", "ftb", "ftb-legacy", "technic"};
        for (const auto& provider : providers)
            catalog.search(provider, provider, "fixture", 0);
        QTRY_COMPARE_WITH_TIMEOUT(replies.size(), providers.size(), 10000);
        QSet<QString> answered;
        for (const auto& reply : replies) {
            answered.insert(reply.at(0).toString());
            const auto result = reply.at(1).toMap();
            QVERIFY(!result.value("ok").toBool());
            QVERIFY(!result.value("error").toString().isEmpty());
            QVERIFY(!result.contains("packs"));
        }
        QCOMPARE(answered.size(), providers.size());
        QVERIFY(!QApplication::activeModalWidget());
        QTest::qWait(50);
        QCOMPARE(replies.size(), providers.size());
        auto pending = std::make_unique<PackCatalog>(&assets);
        pending->search("destroyed", "modrinth", {}, 0);
        QCoreApplication::processEvents();
        pending.reset();
        QCoreApplication::processEvents();
    }
    void missingLocalFtbSearchHasOrdinaryEmptyState()
    {
        Assets assets;
        PackCatalog catalog(&assets);
        QSignalSpy replies(&catalog, &PackCatalog::finished);
        catalog.search("local", "ftb-app", "missing-" + QUuid::createUuid().toString(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 1, 10000);
        const auto response = replies.first().at(1).toMap();
        QVERIFY(response.value("ok").toBool());
        QVERIFY(response.value("packs").toList().isEmpty());
        QVERIFY(!response.value("hasMore").toBool());
    }
    void localFtbDetectionCachesNativeInstallSelection()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto instancePath = directory.path() + "/fixture";
        QVERIFY(QDir().mkpath(instancePath));
        QFile manifest(instancePath + "/instance.json");
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(R"({"uuid":"web-catalog-local-fixture","id":123,"versionId":456,"name":"Web Catalog Local Fixture","version":"1.10","mcVersion":"1.21.1","totalPlayTime":0,"modLoader":"neoforge-21.1.250"})");
        manifest.close();
        const auto previousPath = APPLICATION->settings()->get("FTBAppInstancesPath");
        APPLICATION->settings()->set("FTBAppInstancesPath", directory.path());
        struct RestorePath {
            QVariant path;
            ~RestorePath() { APPLICATION->settings()->set("FTBAppInstancesPath", path); }
        } restore{previousPath};
        Assets assets;
        PackCatalog catalog(&assets);
        QSignalSpy replies(&catalog, &PackCatalog::finished);
        catalog.search("fixture", "ftb-app", "Web Catalog Local Fixture", 0);
        QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 1, 10000);
        const auto response = replies.first().at(1).toMap();
        QVERIFY(response.value("ok").toBool());
        const auto found = response.value("packs").toList();
        QCOMPARE(found.size(), 1);
        const auto pack = found.first().toMap();
        QCOMPARE(pack.value("minecraft").toString(), QString("1.21.1"));
        QVERIFY(!pack.contains("path"));
        const auto id = pack.value("id").toString();
        QVERIFY(!id.contains(directory.path()));
        replies.clear();
        catalog.versions("fixture-versions", "ftb-app", id);
        QTRY_COMPARE(replies.size(), 1);
        const auto releases = replies.first().at(1).toMap().value("versions").toList();
        QCOMPARE(releases.size(), 1);
        QCOMPARE(releases.first().toMap().value("name").toString(), QString("1.10"));
        QString error;
        std::unique_ptr<InstanceTask> task(catalog.createTask("ftb-app", id, "456", nullptr, &error));
        QVERIFY2(task != nullptr, qPrintable(error));
        QVERIFY(qobject_cast<FTBImportAPP::PackInstallTask*>(task.get()));
        QVERIFY(!catalog.createTask("ftb-app", id, "457", nullptr, &error));
        QVERIFY(!error.isEmpty());
    }
    void liveProviderSearchVersionsAndNativeTasks()
    {
        if (qgetenv("AWAKE_TEST_LIVE_CATALOG") != "1")
            QSKIP("Set AWAKE_TEST_LIVE_CATALOG=1 to exercise the actual provider APIs.");
        const auto previousKey = APPLICATION->settings()->get("FlameKeyOverride");
        const auto key = qgetenv("AWAKE_TEST_CURSEFORGE_KEY");
        if (!key.isEmpty())
            APPLICATION->settings()->set("FlameKeyOverride", QString::fromUtf8(key));
        auto network = APPLICATION->network();
        const auto previousProxy = network->proxy();
        network->setProxy(QNetworkProxy::NoProxy);
        struct RestoreSettings {
            QNetworkAccessManager* network;
            QNetworkProxy proxy;
            QVariant key;
            ~RestoreSettings() { network->setProxy(proxy); APPLICATION->settings()->set("FlameKeyOverride", key); }
        } restore{network, previousProxy, previousKey};
        Assets assets;
        PackCatalog catalog(&assets);
        QSignalSpy replies(&catalog, &PackCatalog::finished);
        const QStringList providers{"modrinth", "curseforge", "atlauncher", "ftb", "ftb-legacy", "technic", "ftb-app"};
        for (const auto& provider : providers) {
            replies.clear();
            catalog.search("search-" + provider, provider, {}, 0);
            QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 1, 90000);
            auto response = replies.first().at(1).toMap();
            if (!response.value("ok").toBool()) {
                qWarning().noquote() << provider << "provider unavailable:" << response.value("error").toString();
                QVERIFY(!response.value("error").toString().isEmpty());
                QVERIFY(!response.contains("packs"));
                continue;
            }
            const auto packs = response.value("packs").toList();
            qInfo() << provider << "returned" << packs.size() << "actual packs";
            if (provider == "ftb-app" && packs.isEmpty())
                continue;
            QVERIFY2(!packs.isEmpty(), qPrintable(provider + " live catalog unexpectedly empty"));
            const auto packId = packs.first().toMap().value("id").toString();
            QVERIFY(!packId.isEmpty());
            replies.clear();
            catalog.versions("versions-" + provider, provider, packId);
            QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 1, 90000);
            response = replies.first().at(1).toMap();
            QVERIFY2(response.value("ok").toBool(), qPrintable(provider + ": " + response.value("error").toString()));
            const auto versions = response.value("versions").toList();
            QVERIFY2(!versions.isEmpty(), qPrintable(provider + " selected live pack has no downloadable versions"));
            QString error;
            auto task = std::unique_ptr<InstanceTask>(catalog.createTask(provider, packId, versions.first().toMap().value("id").toString(), nullptr, &error));
            QVERIFY2(task != nullptr, qPrintable(error));
            if (provider == "modrinth" || provider == "curseforge")
                QVERIFY(qobject_cast<InstanceImportTask*>(task.get()));
            else if (provider == "atlauncher")
                QVERIFY(qobject_cast<ATLauncher::PackInstallTask*>(task.get()));
            else if (provider == "ftb")
                QVERIFY(qobject_cast<FTB::PackInstallTask*>(task.get()));
            else if (provider == "ftb-legacy")
                QVERIFY(qobject_cast<LegacyFTB::PackInstallTask*>(task.get()));
            else if (provider == "technic")
                QVERIFY(qobject_cast<Technic::SingleZipPackInstallTask*>(task.get()) || qobject_cast<Technic::SolderPackInstallTask*>(task.get()));
            else if (provider == "ftb-app")
                QVERIFY(qobject_cast<FTBImportAPP::PackInstallTask*>(task.get()));
            QVERIFY(!catalog.createTask(provider, packId, "uncached-version", nullptr, &error));
            QVERIFY(!error.isEmpty());
        }
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
