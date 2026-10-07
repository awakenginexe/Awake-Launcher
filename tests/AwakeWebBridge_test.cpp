// SPDX-License-Identifier: GPL-3.0-only
#include <QBuffer>
#include <QCloseEvent>
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
#include <QThread>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <memory>
#include "Application.h"
#include "InstanceList.h"
#include "awake/web/AwakeWebAssets.h"
#include "awake/web/AwakeSkinManager.h"
#include "awake/AwakeTheme.h"
#include "awake/DataLocation.h"
#include "awake/GpuSelection.h"
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
#include "ui/InstanceWindow.h"
#include "ui/widgets/PageContainer.h"
#include "minecraft/PackProfile.h"
#include "minecraft/Component.h"
#include "ui/widgets/JavaSettingsWidget.h"
#include "ui/dialogs/BlockedModsDialog.h"
#include <QStandardPaths>
#include <QTemporaryFile>
#include "ui/dialogs/AwakePopupDialog.h"
#include "java/RuntimeSelection.h"
#include "minecraft/launch/AutoInstallJava.h"
#include "launch/LaunchTask.h"
#include "java/JavaUtils.h"
#include "SysInfo.h"
#include "FileSystem.h"
#include "java/download/ArchiveDownloadTask.h"
#include "net/HttpMetaCache.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/Parsers.h"
#include <QScopeGuard>

#if defined(Q_OS_WIN) && !defined(Q_MOC_RUN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace Awake::Web;
class AwakeWebBridgeTest : public QObject {
    Q_OBJECT
private slots:
    void installedDataLocationsPreservePortableAndCustomPaths()
    {
        QTemporaryDir install;
        const QString normal = "C:/Users/Fixture/AppData/Roaming/AwakeLauncher";
        QCOMPARE(Awake::installedDataPath(install.path(), normal), normal);
        const auto configure = [&](QString mode, QString path = {}) {
            QFile config(install.filePath("data-location.txt"));
            QVERIFY(config.open(QIODevice::WriteOnly));
            QTextStream text(&config);
            text.setEncoding(QStringConverter::Utf16LE);
            text.setGenerateByteOrderMark(true);
            text << mode << '\n' << path << '\n';
        };
        configure("compact");
        QCOMPARE(Awake::installedDataPath(install.path(), normal), install.filePath("AwakeLauncherData"));
        configure("custom", "D:/Minecraft Data/我的实例");
        QCOMPARE(Awake::installedDataPath(install.path(), normal), QString("D:/Minecraft Data/我的实例"));
        configure("custom", "../relative");
        QVERIFY(Awake::installedDataPath(install.path(), normal).isEmpty());
        configure("normal");
        QCOMPARE(Awake::installedDataPath(install.path(), normal), normal);
    }
    void profileRefreshKeepsCapeImagesOnlyForUnchangedUrls()
    {
        MinecraftProfile profile;
        profile.capes.insert("cape", Cape{"cape", "https://textures.minecraft.net/texture/old", "Pan", "cached-png"});
        QByteArray response = R"({"id":"0123456789abcdef0123456789abcdef","name":"Fixture","skins":[],"capes":[{"id":"cape","state":"ACTIVE","url":"http://textures.minecraft.net/texture/old","alias":"Pan"}]})";
        QVERIFY(Parsers::parseMinecraftProfile(response, profile));
        QCOMPARE(profile.capes["cape"].data, QByteArray("cached-png"));
        response.replace("texture/old", "texture/new");
        QVERIFY(Parsers::parseMinecraftProfile(response, profile));
        QVERIFY(profile.capes["cape"].data.isEmpty());
    }
    void skinsUseLocalImagesAndRejectUntrustedCommands()
    {
        auto accounts = APPLICATION->accounts();
        auto previous = accounts->defaultAccount();
        auto account = MinecraftAccount::createBlankMSA();
        account->accountData()->minecraftProfile.name = "Skin fixture";
        account->accountData()->minecraftProfile.id = "0123456789abcdef0123456789abcdef";
        account->accountData()->yggdrasilToken.token = "private-skin-test-token";
        account->accountData()->minecraftProfile.skin.variant = "SLIM";
        QImage skin(64, 64, QImage::Format_ARGB32);
        skin.fill(Qt::transparent);
        skin.setPixelColor(8, 8, Qt::red);
        QBuffer bytes(&account->accountData()->minecraftProfile.skin.data);
        QVERIFY(bytes.open(QIODevice::WriteOnly));
        QVERIFY(skin.save(&bytes, "PNG"));
        accounts->addAccount(account);
        accounts->setDefaultAccount(account);
        auto cleanup = qScopeGuard([&] {
            for (int i = 0; i < accounts->count(); ++i)
                if (accounts->at(i) == account) { accounts->removeAccount(accounts->index(i, 0)); break; }
            accounts->setDefaultAccount(previous);
        });
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        QVariantMap result;
        QVERIFY(QMetaObject::invokeMethod(&bridge, "skinState", Qt::DirectConnection,
            Q_RETURN_ARG(QVariantMap, result), Q_ARG(QString, account->internalId())));
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(result.value("defaults").toList().size(), 18);
        QCOMPARE(result.value("minecraftDefault").toMap().value("id").toString(), QString("default/alex/SLIM"));
        account->accountData()->minecraftProfile.id = "ffffffff000000000000000000000000";
        QCOMPARE(bridge.skinState(account->internalId()).value("minecraftDefault").toMap().value("id").toString(), QString("default/zuri/CLASSIC"));
        account->accountData()->minecraftProfile.id = "0123456789abcdef0123456789abcdef";
        QCOMPARE(result.value("current").toMap().value("textureHash").toString().size(), 64);
        QVERIFY(result.value("current").toMap().value("textureUrl").toString().startsWith("awake://ui/images/"));
        const auto snapshot = bridge.snapshot();
        const auto exposed = QJsonDocument::fromVariant(snapshot).toJson();
        QVERIFY(!exposed.contains("private-skin-test-token"));
        bool foundHead = false;
        for (const auto& item : snapshot.value("accounts").toList())
            if (item.toMap().value("id") == account->internalId())
                foundHead = item.toMap().value("headUrl").toString().startsWith("awake://ui/images/");
        QVERIFY(foundHead);
        const QVariantMap payload{{"id", "../../secret.png"}, {"variant", "CLASSIC"}, {"capeId", ""}};
        QVERIFY(QMetaObject::invokeMethod(&bridge, "skinCommand", Qt::DirectConnection,
            Q_RETURN_ARG(QVariantMap, result), Q_ARG(QString, QString("skin-test")),
            Q_ARG(QString, account->internalId()), Q_ARG(QString, QString("apply")),
            Q_ARG(QVariantMap, payload)));
        QVERIFY(!result.value("ok").toBool());
        QVERIFY(!bridge.skinCommand("skin-lookup-test", account->internalId(), "lookup", {{"username", "../../private"}}).value("ok").toBool());
        QVERIFY(!bridge.skinCommand("skin-cape-test", account->internalId(), "apply", {{"id", "default/steve/CLASSIC"}, {"variant", "CLASSIC"}, {"capeId", "someone-elses-cape"}}).value("ok").toBool());
        QVERIFY(!bridge.skinCommand("skin-reset-cape-test", account->internalId(), "reset", {{"capeId", "someone-elses-cape"}}).value("ok").toBool());
        QTemporaryDir library;
        const auto original = account->accountData()->minecraftProfile.skin.data;
        const auto originalVariant = account->accountData()->minecraftProfile.skin.variant;
        account->accountData()->yggdrasilToken.token.clear();
        QString savedId;
        {
            Awake::Web::Skins skins(&assets, nullptr, library.path());
            skins.state(account->internalId());
            QSignalSpy finished(&skins, &Awake::Web::Skins::finished);
            QVERIFY(!skins.command("save-invalid", account->internalId(), "saveLocal", {{"id", "../../private"}, {"variant", "SLIM"}, {"name", "Local"}}).value("ok").toBool());
            QVERIFY(skins.command("save-local", account->internalId(), "saveLocal", {{"id", "current/" + account->internalId()}, {"variant", "CLASSIC"}, {"name", "My local skin 我的"}}).value("ok").toBool());
            QTRY_COMPARE(finished.count(), 1);
            const auto saved = finished.last().at(1).toMap().value("saved").toList();
            QCOMPARE(saved.size(), 1);
            savedId = saved.first().toMap().value("id").toString();
            QCOMPARE(saved.first().toMap().value("name").toString(), QString("My local skin 我的"));
            QCOMPARE(saved.first().toMap().value("variant").toString(), QString("CLASSIC"));
            QVERIFY(saved.first().toMap().value("textureUrl").toString().startsWith("awake://ui/images/"));
            QCOMPARE(account->accountData()->minecraftProfile.skin.data, original);
            QCOMPARE(account->accountData()->minecraftProfile.skin.variant, originalVariant);
        }
        Awake::Web::Skins reopened(&assets, nullptr, library.path());
        const auto restored = reopened.state("").value("saved").toList();
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.first().toMap().value("id").toString(), savedId);
        QCOMPARE(restored.first().toMap().value("name").toString(), QString("My local skin 我的"));
        QVERIFY(!reopened.state("").value("editable").toBool());
    }
    void liveSkinLookup()
    {
        if (!qEnvironmentVariableIsSet("AWAKE_SKIN_LIVE")) QSKIP("Live public skin lookup was not requested");
        auto accounts = APPLICATION->accounts();
        auto account = MinecraftAccount::createOffline("Skin lookup fixture");
        accounts->addAccount(account);
        const auto proxy = APPLICATION->network()->proxy();
        APPLICATION->network()->setProxy(QNetworkProxy::NoProxy);
        auto cleanup = qScopeGuard([&] {
            APPLICATION->network()->setProxy(proxy);
            for (int i = 0; i < accounts->count(); ++i)
                if (accounts->at(i) == account) { accounts->removeAccount(accounts->index(i, 0)); break; }
        });
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        QSignalSpy finished(&bridge, &Bridge::catalogFinished);
        const auto result = bridge.skinCommand("live-skin-check", account->internalId(), "lookup", {{"username", "ChronogenEx"}});
        QVERIFY(result.value("ok").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 90000);
        const auto response = finished.first().at(1).toMap();
        QVERIFY2(response.value("ok").toBool(), qPrintable(response.value("error").toString()));
        QVERIFY(response.value("preview").toMap().value("textureUrl").toString().startsWith("awake://ui/images/"));
        account->accountData()->minecraftProfile.capes.insert("public-pan-fixture", Cape{
            "public-pan-fixture", "https://textures.minecraft.net/texture/28de4a81688ad18b49e735a273e086c18f1e3966956123ccb574034c06f5d336", "Pan", {}});
        finished.clear();
        QVERIFY(bridge.skinCommand("live-cape-check", account->internalId(), "capes", {}).value("ok").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 35000);
        const auto capeResponse = finished.first().at(1).toMap();
        QVERIFY2(capeResponse.value("ok").toBool(), qPrintable(capeResponse.value("error").toString()));
        QVERIFY(capeResponse.value("capes").toList().first().toMap().value("textureUrl").toString().startsWith("awake://ui/images/"));
    }
    void deletingInstanceReturnsToTheEventLoopAndPublishesStatus()
    {
        auto* instances = APPLICATION->instances();
        QTemporaryDir recycleProbe(instances->primaryDir() + "/recycle-probe-XXXXXX");
        QVERIFY(recycleProbe.isValid());
        QString recycledProbe;
        if (!FS::trash(recycleProbe.path(), &recycledProbe))
            QSKIP("Instance recycling is unavailable on this OS or volume (including Windows Server).");
        QVERIFY(QFile(recycledProbe).rename(recycleProbe.path()));
        const QString id = "async-delete-fixture";
        const auto path = instances->primaryDir() + "/" + id;
        QVERIFY(QDir().mkpath(path));
        QFile config(path + "/instance.cfg");
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("InstanceType=OneSix\nname=Async delete fixture\niconKey=grass\n");
        config.close();
        QFile pack(path + "/mmc-pack.json");
        QVERIFY(pack.open(QIODevice::WriteOnly));
        pack.write(R"({"formatVersion":1,"components":[{"uid":"net.minecraft","version":"1.21.8","important":true},{"uid":"net.fabricmc.fabric-loader","version":"0.16.14"}]})");
        pack.close();
        instances->loadList();
        QVERIFY(instances->getInstanceById(id));
        instances->setInstanceGroup(id, "Deletion fixture group");
        QPointer<InstanceWindow> editor = new InstanceWindow(instances->getInstanceById(id));
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        auto* window = APPLICATION->showMainWindow();
        QVERIFY(window);
        QVERIFY(QMetaObject::invokeMethod(window, "instanceSelectRequest", Qt::DirectConnection, Q_ARG(QString, id)));
        QTimer confirmation;
        confirmation.setInterval(10);
        connect(&confirmation, &QTimer::timeout, this, [] {
            if (auto* dialog = QApplication::activeModalWidget(); dialog && dialog->objectName() == "awakeDeleteConfirmation")
                if (auto* button = dialog->findChild<QPushButton*>("deleteInstance")) button->click();
        });
        confirmation.start();
        QVERIFY(QMetaObject::invokeMethod(window, "on_actionDeleteInstance_triggered", Qt::DirectConnection));
        confirmation.stop();
        const auto deletion = bridge.snapshot().value("deletion").toMap();
        QVERIFY2(deletion.value("active").toBool(), "Deletion must remain asynchronous after confirmation returns");
        QCOMPARE(deletion.value("id").toString(), id);
        QCOMPARE(deletion.value("name").toString(), QString("Async delete fixture"));
        QVERIFY(instances->getInstanceById(id));
        QVERIFY(!instances->getInstanceById(id)->canLaunch());
        QVERIFY(editor);
        QVERIFY(!editor->saveAll());
        instances->getInstanceById(id)->settings()->set("notes", "Never flush to the removed root");
        bool eventLoopRan = false;
        QTimer::singleShot(0, this, [&] { eventLoopRan = true; });
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.snapshot().value("deletion").toMap().value("active").toBool(), 15000);
        QVERIFY(eventLoopRan);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!instances->getInstanceById(id));
        QTRY_VERIFY(editor.isNull());
        QVERIFY(!QFileInfo::exists(path));
        if (instances->trashedSomething()) QVERIFY(instances->undoTrashInstance());
        QVERIFY(QDir(path).removeRecursively());
        instances->loadList();
    }
    void failedDeletionPreservesTheInstanceGroupAndReportsTheError()
    {
#ifdef Q_OS_WIN
        auto* instances = APPLICATION->instances();
        const QString id = "locked-delete-fixture";
        const auto path = instances->primaryDir() + "/" + id;
        QVERIFY(QDir().mkpath(path));
        QFile config(path + "/instance.cfg");
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("InstanceType=OneSix\nname=Locked delete fixture\niconKey=grass\n");
        config.close();
        QFile pack(path + "/mmc-pack.json");
        QVERIFY(pack.open(QIODevice::WriteOnly));
        pack.write(R"({"formatVersion":1,"components":[{"uid":"net.minecraft","version":"1.21.8","important":true},{"uid":"net.fabricmc.fabric-loader","version":"0.16.14"}]})");
        pack.close();
        const auto world = path + "/.minecraft/saves/Keep World";
        QVERIFY(QDir().mkpath(world));
        const QByteArray lockedContent("Locked world fixture data");
        const QByteArray preservedContent("World fixture data to preserve");
        QFile lockedPayload(world + "/z-locked-level.dat");
        QVERIFY(lockedPayload.open(QIODevice::WriteOnly));
        lockedPayload.write(lockedContent);
        lockedPayload.close();
        QFile preservedPayload(world + "/a-preserved-region.dat");
        QVERIFY(preservedPayload.open(QIODevice::WriteOnly));
        preservedPayload.write(preservedContent);
        preservedPayload.close();
        instances->loadList();
        auto* instance = instances->getInstanceById(id);
        QVERIFY(instance);
        instances->setInstanceGroup(id, "Keep this group");
        instance->setRunning(true);
        QString error;
        QVERIFY(!instances->removeInstance(id, &error));
        QVERIFY(!error.isEmpty());
        instance->setRunning(false);
        QPointer<InstanceWindow> editor = new InstanceWindow(instance);
        auto* pages = editor->findChild<PageContainer*>();
        QVERIFY(pages);
        QVERIFY(pages->isEnabled());
        QVERIFY(config.open(QIODevice::ReadOnly));
        const auto originalConfig = config.readAll();
        config.close();
        const auto previousSelection = APPLICATION->settings()->get("SelectedInstance");
        APPLICATION->settings()->set("SelectedInstance", id);
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        QSignalSpy failures(&bridge, &Bridge::operationFailed);
        const auto nativePath = QDir::toNativeSeparators(world + "/z-locked-level.dat").toStdWString();
        const HANDLE handle = CreateFileW(nativePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
        auto closeHandle = [](void* value) { CloseHandle(value); };
        std::unique_ptr<void, decltype(closeHandle)> locked(handle, closeHandle);
        QVERIFY(instances->removeInstance(id, &error));
        QVERIFY(pack.open(QIODevice::ReadOnly));
        const auto originalPack = pack.readAll();
        pack.close();
        QVERIFY(!pages->isEnabled());
        QVERIFY(!editor->saveAll());
        QVERIFY(!pages->saveAll());
        QVERIFY(!pages->prepareToClose());
        QVERIFY(!editor->requestClose());
        QCloseEvent closeEvent;
        QApplication::sendEvent(editor, &closeEvent);
        QVERIFY(!closeEvent.isAccepted());
        instance->settings()->set("notes", "Deferred while deleting");
        QVERIFY(config.open(QIODevice::ReadOnly));
        QCOMPARE(config.readAll(), originalConfig);
        config.close();
        auto component = instance->getPackProfile()->getComponent("net.minecraft");
        QVERIFY(component);
        component->m_version = "1.21.9";
        instance->getPackProfile()->buildingFromScratch();
        instance->getPackProfile()->saveNow();
        QVERIFY(pack.open(QIODevice::ReadOnly));
        QCOMPARE(pack.readAll(), originalPack);
        pack.close();
        QVERIFY(!instances->removeInstance(id, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!bridge.launchInstance(id).value("ok").toBool());
        failures.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!instances->isRemoving(), 15000);
        QVERIFY(QFileInfo::exists(path + "/instance.cfg"));
        QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(config.readAll().contains("InstanceType=OneSix"));
        config.close();
        QVERIFY(lockedPayload.open(QIODevice::ReadOnly));
        QCOMPARE(lockedPayload.readAll(), lockedContent);
        lockedPayload.close();
        QVERIFY(preservedPayload.open(QIODevice::ReadOnly));
        QCOMPARE(preservedPayload.readAll(), preservedContent);
        preservedPayload.close();
        QVERIFY(instances->getInstanceById(id));
        QVERIFY(!instances->getInstanceById(id)->isDeleting());
        QVERIFY(editor);
        QVERIFY(pages->isEnabled());
        QVERIFY(QFileInfo::exists(path + "/mmc-pack.json"));
        QVERIFY(pack.open(QIODevice::ReadOnly));
        QVERIFY(pack.readAll().contains("1.21.9"));
        pack.close();
        QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(config.readAll().contains("Deferred while deleting"));
        config.close();
        instance->settings()->set("notes", "Settings save recovered");
        QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(config.readAll().contains("Settings save recovered"));
        config.close();
        QCOMPARE(instances->getInstanceGroup(id), QString("Keep this group"));
        QCOMPARE(APPLICATION->settings()->get("SelectedInstance").toString(), id);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(failures.first().first().toString(), QString("delete"));
        QVERIFY(!failures.first().at(1).toString().isEmpty());
        locked.reset();
        editor->close();
        QTRY_VERIFY(editor.isNull());
        APPLICATION->settings()->set("SelectedInstance", previousSelection);
        QVERIFY(QDir(path).removeRecursively());
        instances->loadList();
#else
        QSKIP("Windows file-sharing lock regression");
#endif
    }
    void gpuChoiceIsSavedOnceAndCancelDoesNotChangeIt() {
        auto* config = APPLICATION->settings();
        const auto previousMode = config->get("AwakeGpuPreference");
        const auto previousSeen = config->get("AwakeGpuChoiceSeen");
        const QVariantMap hardware{{"supported", true}, {"devices", QVariantList{QVariantMap{{"name", "GPU one fixture"}}, QVariantMap{{"name", "GPU two fixture"}}}}};
        config->set("AwakeGpuChoiceSeen", false);
        QTimer cancel;
        cancel.setInterval(20);
        connect(&cancel, &QTimer::timeout, this, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        cancel.start();
        QVERIFY(!Awake::Gpu::confirmBeforeLaunch(nullptr, hardware));
        cancel.stop();
        QVERIFY(!config->get("AwakeGpuChoiceSeen").toBool());
        QCOMPARE(config->get("AwakeGpuPreference"), previousMode);
        QTimer accept;
        accept.setInterval(20);
        connect(&accept, &QTimer::timeout, this, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                auto* choice = dialog->findChild<QComboBox*>("gpuPreferenceChoice");
                if (!choice) return;
                choice->setCurrentIndex(choice->findData("highPerformance"));
                dialog->accept();
            }
        });
        accept.start();
        QVERIFY(Awake::Gpu::confirmBeforeLaunch(nullptr, hardware));
        accept.stop();
        QVERIFY(config->get("AwakeGpuChoiceSeen").toBool());
        QCOMPARE(config->get("AwakeGpuPreference").toString(), QString("highPerformance"));
        QVERIFY(Awake::Gpu::confirmBeforeLaunch(nullptr, hardware));
        config->set("AwakeGpuPreference", previousMode);
        config->set("AwakeGpuChoiceSeen", previousSeen);
    }
    void gpuPreferenceTargetsTheResolvedJavaExecutable() {
#ifdef Q_OS_WIN
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile java(directory.filePath("javaw.exe"));
        QVERIFY(java.open(QIODevice::WriteOnly));
        java.close();
        const auto valueName = QDir::toNativeSeparators(QFileInfo(java).canonicalFilePath()).toStdWString();
        HKEY key = nullptr;
        QCOMPARE(RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr), LSTATUS(ERROR_SUCCESS));
        const std::wstring previous = L"AutoHDREnable=1;";
        QCOMPARE(RegSetValueExW(key, valueName.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(previous.c_str()), static_cast<DWORD>((previous.size() + 1) * sizeof(wchar_t))), LSTATUS(ERROR_SUCCESS));
        auto* config = APPLICATION->settings();
        const auto previousMode = config->get("AwakeGpuPreference");
        const auto previousSeen = config->get("AwakeGpuChoiceSeen");
        config->set("AwakeGpuChoiceSeen", true);
        QString error;
        for (const auto& mode : {QString("highPerformance"), QString("powerSaving"), QString("automatic")}) {
            config->set("AwakeGpuPreference", mode);
            QVERIFY2(Awake::Gpu::applyBeforeJava(java.fileName(), error), qPrintable(error));
            wchar_t buffer[256]{};
            DWORD size = sizeof(buffer);
            QCOMPARE(RegQueryValueExW(key, valueName.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(buffer), &size), LSTATUS(ERROR_SUCCESS));
            QCOMPARE(QString::fromWCharArray(buffer), Awake::Gpu::preferenceValue("AutoHDREnable=1;", mode));
        }
        RegDeleteValueW(key, valueName.c_str());
        RegCloseKey(key);
        config->set("AwakeGpuPreference", previousMode);
        config->set("AwakeGpuChoiceSeen", previousSeen);
#endif
    }
    void deletingInstancesRejectJavaChanges()
    {
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        auto* instance = APPLICATION->instances()->getInstanceById("one");
        const auto previous = instance->settings()->get("AwakeJavaProfile");
        instance->setDeleting(true);
        QVERIFY(!bridge.setJavaProfile("one", "oracle").value("ok").toBool());
        QVERIFY(!bridge.browseJava("delete-guard", "one").value("ok").toBool());
        QCOMPARE(instance->settings()->get("AwakeJavaProfile"), previous);
        instance->setDeleting(false);
    }
    void gpuDiscoveryRunsOffUiThreadAndIsReused()
    {
        Assets assets;
        std::atomic_int detections{0};
        std::atomic_bool release{false};
        auto detector = [&] {
            ++detections;
            while (!release.load()) QThread::msleep(1);
            return QVariantMap{{"ok", true}, {"supported", true}, {"devices", QVariantList{QVariantMap{{"name", "GPU fixture"}}}}};
        };
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; }, nullptr, detector);
        QSignalSpy replies(&bridge, &Bridge::catalogFinished);
        QVERIFY(bridge.gpuSettings("gpu-one").value("ok").toBool());
        bool uiResponsive = false;
        QTimer::singleShot(0, &bridge, [&] { uiResponsive = true; release.store(true); });
        QTRY_COMPARE(replies.size(), 1);
        QVERIFY(uiResponsive);
        QCOMPARE(detections.load(), 1);
        QVERIFY(bridge.gpuSettings("gpu-two").value("ok").toBool());
        QTRY_COMPARE(replies.size(), 2);
        QCOMPARE(detections.load(), 1);
        QVERIFY(bridge.setGpuPreference("automatic").value("ok").toBool());
        QCOMPARE(detections.load(), 1);
    }
    void globalGpuPolicyPreservesOtherGraphicsSettings() {
        using namespace Awake::Gpu;
        QVERIFY(needsPrompt(false, 2));
        QVERIFY(!needsPrompt(true, 3));
        QVERIFY(!needsPrompt(false, 1));
        QCOMPARE(preferenceValue("AutoHDREnable=1;GpuPreference=1;", "highPerformance"), QString("AutoHDREnable=1;GpuPreference=2;"));
        QCOMPARE(preferenceValue("GpuPreference=2;SwapEffectUpgradeEnable=0;", "powerSaving"), QString("SwapEffectUpgradeEnable=0;GpuPreference=1;"));
        QCOMPARE(preferenceValue("AutoHDREnable=1;GpuPreference=2;", "automatic"), QString("AutoHDREnable=1;"));
        QVERIFY(preferenceValue("GpuPreference=2;", "automatic").isEmpty());
        QVERIFY(!validMode("nvidia"));
        QVERIFY(!validMode("inherit"));
    }
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
    void nativeJvmEditsActivateCustomPreset()
    {
        auto* global = APPLICATION->settings();
        const auto previousArgs = global->get("JvmArgs");
        const auto previousPreset = global->get("AwakeJvmPreset");
        global->set("AwakeJvmPreset", "balanced");
        JavaSettingsWidget widget;
        widget.saveSettings();
        QCOMPARE(global->get("AwakeJvmPreset").toString(), QString("balanced"));
        widget.findChild<QPlainTextEdit*>("jvmArgsTextBox")->setPlainText("-Dnative.global=true");
        widget.saveSettings();
        QCOMPARE(global->get("AwakeJvmPreset").toString(), QString("custom"));
        auto* instance = APPLICATION->instances()->getInstanceById("one");
        instance->settings()->set("OverrideJavaArgs", true);
        instance->settings()->set("AwakeJvmPreset", "performance");
        JavaSettingsWidget local(instance);
        local.findChild<QPlainTextEdit*>("jvmArgsTextBox")->setPlainText("-Dnative.instance=true");
        local.saveSettings();
        QCOMPARE(instance->jvmPreset(), QString("custom"));
        QVERIFY(instance->extraArguments().contains("-Dnative.instance=true"));
        global->set("JvmArgs", previousArgs);
        global->set("AwakeJvmPreset", previousPreset);
    }
    void customJvmArgumentsUseGlobalOrInstanceSettings()
    {
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        auto* global = APPLICATION->settings();
        const auto previous = global->get("JvmArgs");
        const auto previousPreset = global->get("AwakeJvmPreset");
        QVERIFY(bridge.setPreference("jvmPreset", "custom").value("ok").toBool());
        QVERIFY(bridge.setPreference("jvmArgs", "-Dawake.global=true").value("ok").toBool());
        QCOMPARE(bridge.snapshot().value("launcherSettings").toMap().value("jvmArgs").toString(), QString("-Dawake.global=true"));
        QVERIFY(!bridge.setPreference("jvmArgs", QString(8193, 'x')).value("ok").toBool());
        auto* instance = APPLICATION->instances()->getInstanceById("one");
        instance->settings()->set("OverrideJavaArgs", false);
        QCOMPARE(instance->extraArguments().first(), QString("-Dawake.global=true"));
        auto options = bridge.instanceDetails("one", "settings").value("settings").toMap();
        options["useGlobalJvmArgs"] = false;
        options["jvmPreset"] = "custom";
        options["jvmArgs"] = "-Dawake.instance=true -Xmx128m";
        QVERIFY(bridge.instanceCommand("one", "saveSettings", options).value("ok").toBool());
        QCOMPARE(instance->extraArguments().first(), QString("-Dawake.instance=true"));
        const auto arguments = instance->javaArguments();
        QVERIFY(arguments.contains("-Dawake.instance=true"));
        const auto heapLimit = QString("-Xmx%1m").arg(instance->settings()->get("MaxMemAlloc").toInt());
        QVERIFY(arguments.lastIndexOf(heapLimit) > arguments.indexOf("-Xmx128m"));
        QCOMPARE(global->get("JvmArgs").toString(), QString("-Dawake.global=true"));
        options["jvmArgs"] = 42;
        QVERIFY(!bridge.instanceCommand("one", "saveSettings", options).value("ok").toBool());
        QCOMPARE(instance->extraArguments().first(), QString("-Dawake.instance=true"));
        options["jvmArgs"] = "-Dawake.instance=true";
        options["useGlobalJvmArgs"] = true;
        QVERIFY(bridge.instanceCommand("one", "saveSettings", options).value("ok").toBool());
        QCOMPARE(instance->extraArguments().first(), QString("-Dawake.global=true"));
        global->set("JvmArgs", previous);
        global->set("AwakeJvmPreset", previousPreset);
    }
    void jvmPresetInheritanceAndCustomTextRetention()
    {
        Assets assets;
        Bridge bridge(&assets, [](const QString&) { return true; }, [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; });
        auto* global = APPLICATION->settings();
        const auto previousPreset = global->get("AwakeJvmPreset");
        auto* instance = APPLICATION->instances()->getInstanceById("one");
        auto* settings = instance->settings();
        settings->set("OverrideJavaArgs", false);
        for (const auto& preset : {"compatible", "balanced", "performance", "custom"}) {
            QVERIFY(bridge.setPreference("jvmPreset", preset).value("ok").toBool());
            QCOMPARE(instance->jvmPreset(), QString(preset));
        }
        QVERIFY(!bridge.setPreference("jvmPreset", "maximum-fps").value("ok").toBool());
        auto options = bridge.instanceDetails("one", "settings").value("settings").toMap();
        options["useGlobalJvmArgs"] = false;
        options["jvmPreset"] = "custom";
        options["jvmArgs"] = "-Dawake.preserved=true";
        QVERIFY(bridge.instanceCommand("one", "saveSettings", options).value("ok").toBool());
        QVERIFY(instance->extraArguments().contains("-Dawake.preserved=true"));
        options["jvmPreset"] = "performance";
        QVERIFY(bridge.instanceCommand("one", "saveSettings", options).value("ok").toBool());
        QCOMPARE(instance->jvmPreset(), QString("performance"));
        QVERIFY(!instance->extraArguments().contains("-Dawake.preserved=true"));
        QCOMPARE(settings->get("JvmArgs").toString(), QString("-Dawake.preserved=true"));
        QVERIFY(bridge.setPreference("jvmPreset", "compatible").value("ok").toBool());
        QCOMPARE(instance->jvmPreset(), QString("performance"));
        options["jvmPreset"] = "maximum-fps";
        QVERIFY(!bridge.instanceCommand("one", "saveSettings", options).value("ok").toBool());
        QCOMPARE(instance->jvmPreset(), QString("performance"));
        options["jvmPreset"] = "custom";
        QVERIFY(bridge.instanceCommand("one", "saveSettings", options).value("ok").toBool());
        QVERIFY(instance->extraArguments().contains("-Dawake.preserved=true"));
        options["useGlobalJvmArgs"] = true;
        QVERIFY(bridge.instanceCommand("one", "saveSettings", options).value("ok").toBool());
        QCOMPARE(instance->jvmPreset(), QString("compatible"));
        const auto inherited = bridge.instanceDetails("one", "settings");
        const auto local = inherited.value("jvmConfig").toMap().value("local").toMap();
        QCOMPARE(local.value("jvmPreset").toString(), QString("custom"));
        QCOMPARE(local.value("jvmArgs").toString(), QString("-Dawake.preserved=true"));
        options = inherited.value("settings").toMap();
        options["useGlobalJvmArgs"] = false;
        options["jvmPreset"] = local.value("jvmPreset");
        options["jvmArgs"] = local.value("jvmArgs");
        QVERIFY(bridge.instanceCommand("one", "saveSettings", options).value("ok").toBool());
        QCOMPARE(instance->jvmPreset(), QString("custom"));
        QVERIFY(instance->extraArguments().contains("-Dawake.preserved=true"));
        global->set("AwakeJvmPreset", previousPreset);
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
    void jvmPresetsRespectRuntimeAndLoaderChoices()
    {
        const QString vm = "OpenJDK 64-Bit Server VM";
        const QStringList balanced{"-XX:+UseG1GC", "-XX:MaxGCPauseMillis=100"};
        const QStringList performance{"-XX:+UseG1GC", "-XX:MaxGCPauseMillis=50"};
        for (const auto& preset : {"compatible", "balanced", "performance", "custom"}) QVERIFY(Java::jvmPresetAllowed(preset));
        QVERIFY(!Java::jvmPresetAllowed("maximum-fps"));
        QCOMPARE(Java::jvmPresetArguments("balanced", 21, "64", vm, {}), balanced);
        QCOMPARE(Java::jvmPresetArguments("performance", 17, "64", vm, {}), performance);
        QCOMPARE(Java::jvmPresetArguments("balanced", 8, "64", vm, {}),
                 (QStringList{"-XX:+UseG1GC", "-XX:MaxGCPauseMillis=200"}));
        QCOMPARE(Java::jvmPresetArguments("performance", 8, "64", vm, {}), balanced);
        QVERIFY(Java::jvmPresetArguments("balanced", 7, "64", vm, {}).isEmpty());
        QVERIFY(Java::jvmPresetArguments("balanced", 21, "32", vm, {}).isEmpty());
        QVERIFY(Java::jvmPresetArguments("balanced", 21, "64", "Eclipse OpenJ9 VM", {}).isEmpty());
        QVERIFY(Java::jvmPresetArguments("balanced", 21, "64", "", {}).isEmpty());
        QVERIFY(Java::jvmPresetArguments("custom", 21, "64", vm, {}).isEmpty());
        QVERIFY(Java::jvmPresetArguments("compatible", 21, "64", vm, {}).isEmpty());
        for (const auto& arg : {"-XX:+UseZGC", "-XX:+UseG1GC", "-XX:-UseG1GC"})
            QVERIFY(Java::jvmPresetArguments("performance", 21, "64", vm, {arg}).isEmpty());
        QCOMPARE(Java::jvmPresetArguments("balanced", 21, "64", vm, {"-XX:MaxGCPauseMillis=75"}),
                 (QStringList{"-XX:+UseG1GC"}));
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
    void blockedModsAlsoWatchSystemDownloads()
    {
        QTemporaryDir configured;
        QTemporaryFile download(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/awake-download-test-XXXXXX.jar");
        QVERIFY(download.open());
        download.write("verified-file");
        download.flush();
        const auto previous = APPLICATION->settings()->get("DownloadsDir");
        APPLICATION->settings()->set("DownloadsDir", configured.path());
        QList<BlockedMod> mods{{QFileInfo(download.fileName()).fileName(), {},
            QString::fromLatin1(QCryptographicHash::hash("verified-file", QCryptographicHash::Sha1).toHex()), false, {}, "mods"}};
        BlockedModsDialog popup(nullptr, {}, {}, mods);
        popup.show();
        QTRY_VERIFY_WITH_TIMEOUT(mods.first().matched, 5000);
        APPLICATION->settings()->set("DownloadsDir", previous);
    }
    void blockedModsRetryFileThatFinishesWriting()
    {
        QTemporaryDir downloads;
        const auto previous = APPLICATION->settings()->get("DownloadsDir");
        APPLICATION->settings()->set("DownloadsDir", downloads.path());
        QFile file(downloads.path() + "/required.jar");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("partial");
        file.close();
        QList<BlockedMod> mods{{"required.jar", {},
            QString::fromLatin1(QCryptographicHash::hash("verified-file", QCryptographicHash::Sha1).toHex()), false, {}, "mods"}};
        BlockedModsDialog popup(nullptr, {}, {}, mods);
        popup.show();
        QTest::qWait(500);
        QVERIFY(!mods.first().matched);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("verified-file");
        file.close();
        QTRY_VERIFY_WITH_TIMEOUT(mods.first().matched, 5000);
        QTRY_COMPARE(popup.result(), static_cast<int>(QDialog::Accepted));
        APPLICATION->settings()->set("DownloadsDir", previous);
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
        const auto previousSeen = APPLICATION->settings()->get("AwakeGpuChoiceSeen");
        APPLICATION->settings()->set("AwakeGpuChoiceSeen", false);
        QTRY_VERIFY(!bridge.gpuSettings().value("loading").toBool());
        const auto gpu = bridge.gpuSettings();
        if (gpu.value("supported").toBool() && gpu.value("devices").toList().size() > 1) {
            QVERIFY(bridge.launchInstance("one").value("gpuChoiceRequired").toBool());
            QCOMPARE(actions, 1);
            QVERIFY(!APPLICATION->settings()->get("AwakeGpuChoiceSeen").toBool());
        }
        APPLICATION->settings()->set("AwakeGpuChoiceSeen", true);
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
        APPLICATION->settings()->set("AwakeGpuChoiceSeen", previousSeen);
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
        QCOMPARE(artwork.last().at(1).toString(), QString("awake://ui/assets/minecraft-background.png"));
        QVERIFY(artwork.last().at(2).toString().isEmpty());
        auto* timer = bridge->findChild<QTimer*>("awakeArtworkTimer");
        const auto events = artwork.size();
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QTest::qWait(500);
        QCOMPARE(artwork.size(), events);
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
        QTimer::singleShot(0, &app, [&] {
            AwakeWebBridgeTest test;
            QStringList arguments;
            for (int index = 0; index < argc; ++index) arguments.append(QString::fromLocal8Bit(argv[index]));
            const auto reportPath = original + "/AwakeWebBridge-results.txt";
            const bool captureReport = !arguments.contains("-o");
            if (captureReport) arguments << "-o" << reportPath + ",txt";
            result = QTest::qExec(&test, arguments);
            if (captureReport && result) {
                QFile report(reportPath);
                if (report.open(QIODevice::ReadOnly)) qCritical().noquote() << report.readAll();
            }
            app.quit();
        });
        app.exec();
    }
    QDir::setCurrent(original);
    return result;
}
#include "AwakeWebBridge_test.moc"
