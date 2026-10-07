// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QCryptographicHash>
#include <memory>
#include "Application.h"
#include "InstanceList.h"
#include "InstanceImportTask.h"
#include "InstanceTask.h"
#include "ResourceDownloadTask.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/MetadataHandler.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "minecraft/mod/tasks/LocalShaderPackParseTask.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/flame/FlameCheckUpdate.h"
#include "net/ApiHeaderProxy.h"
#include "settings/SettingsObject.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/ResourceDownloadDialog.h"
#include "ui/pages/modplatform/flame/FlameModel.h"
#include "ui/pages/modplatform/flame/FlamePage.h"
#include "ui/pages/instance/ManagedPackPage.h"
#include "ui/widgets/PageContainer.h"

// Suspend INI writes until the temporary credential has been restored.
class TemporaryFlameKey {
    SettingsObject* m_settings = APPLICATION->settings();
    SettingsObject::Lock m_lock{m_settings};
    QVariant m_previous = m_settings->get("FlameKeyOverride");
   public:
    TemporaryFlameKey() { m_settings->set("FlameKeyOverride", QString::fromUtf8(qgetenv("AWAKE_CF_TEST_KEY"))); }
    ~TemporaryFlameKey() { m_settings->set("FlameKeyOverride", m_previous); }
};

static bool runTask(Task* task, int timeout = 60000)
{
    QSignalSpy finished(task, &Task::finished);
    task->start();
    if (!task->isFinished() && !finished.wait(timeout)) {
        task->abort();
        return false;
    }
    return task->wasSuccessful();
}

class CurseForgeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QTRY_COMPARE_WITH_TIMEOUT(APPLICATION->status(), Application::Initialized, 15000);
        auto* instance = dynamic_cast<MinecraftInstance*>(APPLICATION->instances()->getInstanceById("one"));
        QVERIFY(instance);
        auto* profile = instance->getPackProfile();
        profile->buildingFromScratch();
        profile->setComponentVersion("net.minecraft", "1.21.1", true);
        profile->setComponentVersion("net.fabricmc.fabric-loader", "0.16.14");
    }
    void failedIndexedReplacementKeepsMetadataUntilRetry_data()
    {
        QTest::addColumn<bool>("sameFilename");
        QTest::addColumn<bool>("disabled");
        QTest::newRow("new-filename") << false << false;
        QTest::newRow("same-filename") << true << false;
        QTest::newRow("disabled-old") << false << true;
    }
    void failedIndexedReplacementKeepsMetadataUntilRetry()
    {
        QFETCH(bool, sameFilename);
        QFETCH(bool, disabled);
        auto* instance = dynamic_cast<MinecraftInstance*>(APPLICATION->instances()->getInstanceById("one"));
        QVERIFY(instance);
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        ModFolderModel model(QDir(folder.path()), instance, true, true);
        auto pack = std::make_shared<ModPlatform::IndexedPack>();
        pack->provider = ModPlatform::ResourceProvider::MODRINTH;
        pack->addonId = "fixture-project"; pack->slug = "replacement-fixture"; pack->name = "Replacement fixture";
        ModPlatform::IndexedVersion oldVersion;
        oldVersion.addonId = pack->addonId; oldVersion.fileId = "old-version"; oldVersion.fileName = "old.jar";
        oldVersion.mcVersion = {"1.21.1"}; oldVersion.loaders = ModPlatform::Fabric;
        const auto oldPath = folder.path() + '/' + oldVersion.fileName + (disabled ? ".disabled" : "");
        QFile oldFile(oldPath);
        QVERIFY(oldFile.open(QIODevice::WriteOnly));
        QCOMPARE(oldFile.write("old resource"), 12); oldFile.close();
        LocalResourceUpdateTask oldIndex(model.indexDir(), *pack, oldVersion);
        oldIndex.start(); QVERIFY(oldIndex.wasSuccessful());
        model.update();
        QTRY_COMPARE(model.size(), 1);
        QVERIFY(Metadata::get(model.indexDir(), pack->addonId).fileId == oldVersion.fileId);

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        bool serveReplacement = false;
        const QByteArray replacement = "replacement resource";
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                if (!socket->readAll().contains("GET")) return;
                const auto body = serveReplacement ? replacement : QByteArray();
                socket->write((serveReplacement ? QByteArray("HTTP/1.1 200 OK\r\n") : QByteArray("HTTP/1.1 404 Not Found\r\n")) +
                    "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        struct RestoreProxy {
            QNetworkAccessManager* network;
            QNetworkProxy proxy;
            ~RestoreProxy() { network->setProxy(proxy); }
        } restore{APPLICATION->network(), APPLICATION->network()->proxy()};
        APPLICATION->network()->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        auto version = oldVersion;
        version.fileId = "new-version"; version.fileName = sameFilename ? "old.jar" : "new.jar";
        version.downloadUrl = QString("http://127.0.0.1:%1/replacement.jar").arg(server.serverPort());
        version.hashType = "sha256";
        version.hash = QString::fromLatin1(QCryptographicHash::hash(replacement, QCryptographicHash::Sha256).toHex());
        ResourceDownloadTask failed(pack, version, &model);
        failed.setAskRetry(false);
        QVERIFY(!runTask(&failed));
        QCOMPARE(Metadata::get(model.indexDir(), pack->addonId).fileId, oldVersion.fileId);
        QVERIFY(oldFile.open(QIODevice::ReadOnly));
        QCOMPARE(oldFile.readAll(), QByteArray("old resource")); oldFile.close();

        serveReplacement = true;
        ResourceDownloadTask retry(pack, version, &model);
        retry.setAskRetry(false);
        QVERIFY2(runTask(&retry), qPrintable(retry.failReason()));
        QCOMPARE(Metadata::get(model.indexDir(), pack->addonId).fileId, version.fileId);
        QFile newFile(folder.path() + '/' + version.fileName);
        QVERIFY(newFile.open(QIODevice::ReadOnly));
        QCOMPARE(newFile.readAll(), replacement);
        if (!sameFilename) QVERIFY(!QFileInfo::exists(oldPath));
    }
    void missingKeyRemainsVisible()
    {
        if (!BuildConfig.FLAME_API_KEY.isEmpty()) QSKIP("This test requires an unkeyed build");
        APPLICATION->settings()->set("FlameKeyOverride", "");
        NewInstanceDialog dialog({});
        auto* container = dialog.findChild<PageContainer*>();
        QVERIFY(container);
        QVERIFY(container->getPage("flame"));
        QVERIFY(container->selectPage("flame"));
        auto* notice = dialog.findChild<QLabel*>("curseforgeKeyDiagnostic");
        QVERIFY(notice);
        QVERIFY(notice->text().contains("Settings"));
        QVERIFY(!dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());
        for (const auto& name : {QString("Modrinth"), QString("FTB"), QString("Technic"), QString("ATLauncher")}) {
            bool found = false;
            for (auto* page : container->getPages()) found |= page->displayName().contains(name);
            QVERIFY2(found, qPrintable(name));
        }
    }
    void overrideAndScopedHeaders()
    {
        APPLICATION->settings()->set("FlameKeyOverride", "test-only-override");
        QVERIFY(APPLICATION->capabilities() & Application::SupportsFlame);
        QCOMPARE(APPLICATION->getFlameAPIKey(), QString("test-only-override"));
        Net::ApiHeaderProxy proxy;
        for (const auto& url : {BuildConfig.FLAME_BASE_URL + "/mods/search", "https://" + BuildConfig.FLAME_DOWNLOAD_HOST + "/files/test.jar"}) {
            QNetworkRequest request{QUrl(url)};
            proxy.writeHeaders(request);
            QCOMPARE(request.rawHeader("x-api-key"), QByteArray("test-only-override"));
        }
        for (const auto& url : {QString("https://example.org/"), QString("https://edge.forgecdn.net.example.org/")}) {
            QNetworkRequest request{QUrl(url)};
            proxy.writeHeaders(request);
            QVERIFY(!request.hasRawHeader("x-api-key"));
        }
        APPLICATION->settings()->reset("FlameKeyOverride");
        QVERIFY(APPLICATION->getFlameAPIKey() == BuildConfig.FLAME_API_KEY);
    }
    void buildKeyEnablesCurseForge()
    {
        if (BuildConfig.FLAME_API_KEY.isEmpty()) QSKIP("This test requires a keyed build");
        APPLICATION->settings()->reset("FlameKeyOverride");
        QVERIFY(APPLICATION->settings()->get("FlameKeyOverride").toString().isEmpty());
        QVERIFY(APPLICATION->capabilities() & Application::SupportsFlame);
        NewInstanceDialog dialog({});
        auto* container = dialog.findChild<PageContainer*>();
        QVERIFY(container);
        QVERIFY(dynamic_cast<FlamePage*>(container->getPage("flame")));
        QVERIFY(!dialog.findChild<QLabel*>("curseforgeKeyDiagnostic"));
        if (qEnvironmentVariableIsEmpty("AWAKE_CF_TEST_KEY")) return;
        APPLICATION->network()->setProxy(QNetworkProxy::NoProxy);
        QList<ModPlatform::IndexedPack::Ptr> projects;
        QString failure;
        auto search = FlameAPI::get().searchProjects({.type = ModPlatform::ResourceType::Modpack, .search = "Minimalistic Stuff"},
            {.onSucceed = [&](auto& result) { projects = result; }, .onFail = [&](const auto& reason, int) { failure = reason; }});
        QVERIFY2(runTask(search.get()), qPrintable(failure));
        QVERIFY(!projects.isEmpty());
    }
    void searchFailureIsVisible()
    {
        APPLICATION->settings()->set("FlameKeyOverride", "test-only-override");
        NewInstanceDialog dialog({});
        auto* model = dialog.findChild<Flame::ListModel*>();
        auto* error = dialog.findChild<QLabel*>("curseforgeSearchError");
        QVERIFY(model);
        QVERIFY(error);
        QVERIFY(QMetaObject::invokeMethod(model, "searchRequestFailed", Qt::DirectConnection,
            Q_ARG(QString, QString("HTTP 401 Unauthorized"))));
        QVERIFY(!error->isHidden());
        QVERIFY(error->text().contains("Settings"));
        QVERIFY(error->text().contains("401"));
        APPLICATION->settings()->reset("FlameKeyOverride");
    }
    void resourceDiagnostics()
    {
        if (!BuildConfig.FLAME_API_KEY.isEmpty()) QSKIP("This test requires an unkeyed build");
        auto* instance = dynamic_cast<MinecraftInstance*>(APPLICATION->instances()->getInstanceById("one"));
        QVERIFY(instance);
        QWidget parent;
        using Dialog = ResourceDownload::ResourceDownloadDialog;
        for (auto* raw : {Dialog::createMod(&parent, instance->loaderModList(), instance, true),
                          Dialog::createResourcePack(&parent, instance->resourcePackList(), instance, true),
                          Dialog::createShaderPack(&parent, instance->shaderPackList(), instance, true)}) {
            std::unique_ptr<Dialog> dialog(raw);
            QVERIFY(dialog->findChild<QLabel*>("curseforgeKeyDiagnostic"));
            QVERIFY(dialog->selectPage("modrinth"));
            QVERIFY(dialog->getTasks().isEmpty());
        }
    }
    void managedPackDiagnostic()
    {
        if (!BuildConfig.FLAME_API_KEY.isEmpty()) QSKIP("This test requires an unkeyed build");
        auto* instance = APPLICATION->instances()->getInstanceById("two");
        instance->setManagedPack("flame", "648340", "Missing-key test pack", "test", "test");
        std::unique_ptr<ManagedPackPage> page(ManagedPackPage::createPage(instance));
        page->opened();
        QVERIFY(page->findChild<QLabel*>("curseforgeKeyDiagnostic"));
        QVERIFY(!page->findChild<QPushButton*>("updateButton")->isEnabled());
    }
    void liveResources_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<QString>("query");
        QTest::newRow("mod") << int(ModPlatform::ResourceType::Mod) << QString("AppleSkin");
        QTest::newRow("resourcepack") << int(ModPlatform::ResourceType::ResourcePack) << QString("Default Dark Mode");
        QTest::newRow("shader") << int(ModPlatform::ResourceType::ShaderPack) << QString("MakeUp Ultra Fast");
    }
    void liveResources()
    {
        if (qEnvironmentVariableIsEmpty("AWAKE_CF_TEST_KEY")) QSKIP("Set AWAKE_CF_TEST_KEY for live API/CDN tests");
        QFETCH(int, type);
        QFETCH(QString, query);
        TemporaryFlameKey key;
        APPLICATION->network()->setProxy(QNetworkProxy::NoProxy);
        const auto resourceType = ModPlatform::ResourceType(type);
        const ModPlatform::ModLoaderTypes loaders = resourceType == ModPlatform::ResourceType::Mod ? ModPlatform::Fabric : ModPlatform::None;
        QList<ModPlatform::IndexedPack::Ptr> projects;
        QString failure;
        auto search = FlameAPI::get().searchProjects({.type = resourceType, .search = query},
            {.onSucceed = [&](auto& result) { projects = result; }, .onFail = [&](const auto& reason, int) { failure = reason; }});
        QVERIFY2(runTask(search.get()), qPrintable(failure));
        QVERIFY(!projects.isEmpty());
        auto project = projects.first();
        auto info = FlameAPI::get().getProjectInfo({project},
            {.onSucceed = [&](auto& result) { project = result; }, .onFail = [&](const auto& reason, int) { failure = reason; }});
        QVERIFY2(runTask(info.get()), qPrintable(failure));
        QVERIFY(!project->name.isEmpty());
        const std::vector<Version> mcVersions{Version("1.21.1")};
        QVector<ModPlatform::IndexedVersion> versions;
        auto files = FlameAPI::get().getProjectVersions({.pack = project, .mcVersions = mcVersions, .loaders = loaders, .resourceType = resourceType},
            {.onSucceed = [&](auto& result) { versions = result; }, .onFail = [&](const auto& reason, int) { failure = reason; }});
        QVERIFY2(runTask(files.get()), qPrintable(failure));
        QVERIFY(versions.size() > 1);
        auto oldVersion = versions.last();
        QVERIFY(!oldVersion.downloadUrl.isEmpty());
        auto* instance = dynamic_cast<MinecraftInstance*>(APPLICATION->instances()->getInstanceById("one"));
        ResourceFolderModel* model = nullptr;
        if (resourceType == ModPlatform::ResourceType::Mod) model = instance->loaderModList();
        else if (resourceType == ModPlatform::ResourceType::ResourcePack) model = instance->resourcePackList();
        else model = instance->shaderPackList();
        ResourceDownloadTask download(project, oldVersion, model);
        QVERIFY2(runTask(&download), qPrintable(download.failReason()));
        QVERIFY(QFileInfo::exists(model->dir().filePath(oldVersion.fileName)));
        if (resourceType == ModPlatform::ResourceType::ShaderPack)
            QVERIFY(ShaderPackUtils::validate(QFileInfo(model->dir().filePath(oldVersion.fileName))));
        model->update();
        QTRY_VERIFY_WITH_TIMEOUT(!model->allResources().isEmpty() && model->allResources().first()->metadata(), 15000);
        auto installed = model->allResources();
        auto gameVersions = mcVersions;
        FlameCheckUpdate update(installed, gameVersions,
            resourceType == ModPlatform::ResourceType::Mod ? QList<ModPlatform::ModLoaderType>{ModPlatform::Fabric} : QList<ModPlatform::ModLoaderType>{}, model);
        QSignalSpy checkFailures(&update, &CheckUpdateTask::checkFailed);
        QVERIFY2(runTask(&update), qPrintable(update.failReason()));
        QVERIFY(checkFailures.isEmpty());
        auto updates = update.getUpdates();
        QVERIFY(!updates.empty());
        auto replacement = updates.front().download;
        QVERIFY2(runTask(replacement.get()), qPrintable(replacement->failReason()));
        QVERIFY(QFileInfo::exists(model->dir().filePath(replacement->getFilename())));
        if (resourceType == ModPlatform::ResourceType::ShaderPack)
            QVERIFY(ShaderPackUtils::validate(QFileInfo(model->dir().filePath(replacement->getFilename()))));
        if (oldVersion.fileName != replacement->getFilename()) QVERIFY(!QFileInfo::exists(model->dir().filePath(oldVersion.fileName)));
        QSignalSpy refreshed(model, &ResourceFolderModel::updateFinished);
        model->update();
        QVERIFY(refreshed.wait(15000));
        QTRY_VERIFY_WITH_TIMEOUT(!model->hasPendingParseTasks(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(model->allResources().first()->valid(), 15000);
        QFile config(QDir::current().filePath("awakelauncher.cfg"));
        QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(!config.readAll().contains(qgetenv("AWAKE_CF_TEST_KEY")));
    }
    void liveModpackInstallAndUpdate()
    {
        if (qEnvironmentVariableIsEmpty("AWAKE_CF_TEST_KEY")) QSKIP("Set AWAKE_CF_TEST_KEY for live API/CDN tests");
        TemporaryFlameKey key;
        APPLICATION->network()->setProxy(QNetworkProxy::NoProxy);
        APPLICATION->settings()->set("DownloadGameFilesDuringInstanceCreation", false);
        QList<ModPlatform::IndexedPack::Ptr> projects;
        QString failure;
        auto search = FlameAPI::get().searchProjects({.type = ModPlatform::ResourceType::Modpack, .search = "Minimalistic Stuff"},
            {.onSucceed = [&](auto& result) { projects = result; }, .onFail = [&](const auto& reason, int) { failure = reason; }});
        QVERIFY2(runTask(search.get()), qPrintable(failure));
        QVERIFY(!projects.isEmpty());
        auto project = std::make_shared<ModPlatform::IndexedPack>();
        project->addonId = "648340";
        auto info = FlameAPI::get().getProjectInfo({project},
            {.onSucceed = [&](auto& result) { project = result; }, .onFail = [&](const auto& reason, int) { failure = reason; }});
        QVERIFY2(runTask(info.get()), qPrintable(failure));
        QCOMPARE(project->name, QString("Minimalistic Stuff"));
        QVector<ModPlatform::IndexedVersion> versions;
        auto files = FlameAPI::get().getProjectVersions({.pack = project, .resourceType = ModPlatform::ResourceType::Modpack},
            {.onSucceed = [&](auto& result) { versions = result; }, .onFail = [&](const auto& reason, int) { failure = reason; }});
        QVERIFY2(runTask(files.get()), qPrintable(failure));
        versions.removeIf([](const auto& version) { return version.downloadUrl.isEmpty(); });
        QVERIFY(versions.size() > 1);
        const auto oldVersion = versions.last();
        const auto newVersion = versions.first();
        QTimer unexpectedDialog;
        unexpectedDialog.setInterval(50);
        connect(&unexpectedDialog, &QTimer::timeout, this, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                qWarning() << "Unexpected installer dialog:" << dialog->metaObject()->className();
                dialog->reject();
            }
        });
        unexpectedDialog.start();
        auto makeImport = [&](const auto& version, const QString& originalId = {}) {
            QMap<QString, QString> extra{{"pack_id", project->addonId.toString()}, {"pack_version_id", version.fileId.toString()}};
            if (!originalId.isEmpty()) extra.insert("original_instance_id", originalId);
            auto* import = new InstanceImportTask(version.downloadUrl, true, nullptr, extra);
            import->setName("CurseForge integration test");
            import->setOriginalName(project->name, version.version);
            import->setConfirmUpdate(false);
            return std::unique_ptr<Task>(APPLICATION->instances()->wrapInstanceTask(import));
        };
        auto install = makeImport(oldVersion);
        QVERIFY2(runTask(install.get(), 120000), qPrintable(install->failReason()));
        QTRY_VERIFY_WITH_TIMEOUT(APPLICATION->instances()->getInstanceById("CurseForge integration test"), 15000);
        auto* instance = APPLICATION->instances()->getInstanceById("CurseForge integration test");
        QCOMPARE(instance->getManagedPackID(), project->addonId.toString());
        QCOMPARE(instance->getManagedPackVersionID(), oldVersion.fileId.toString());
        QVERIFY(QFileInfo::exists(instance->instanceRoot() + "/flame/manifest.json"));
        QVERIFY(!QDir(instance->gameRoot() + "/mods").entryList({"*.jar"}, QDir::Files).isEmpty());
        const auto originalId = instance->id();
        // Exercise upstream's support for updating older .minecraft instance roots.
        if (QFileInfo(instance->gameRoot()).fileName() == "minecraft")
            QVERIFY(QDir(instance->instanceRoot()).rename("minecraft", ".minecraft"));
        QCOMPARE(QFileInfo(instance->gameRoot()).fileName(), QString(".minecraft"));
        const auto marker = instance->gameRoot() + "/user-file.txt";
        QFile userFile(marker);
        QVERIFY(userFile.open(QIODevice::WriteOnly));
        userFile.write("preserve user data");
        userFile.close();
        auto update = makeImport(newVersion, originalId);
        QVERIFY2(runTask(update.get(), 120000), qPrintable(update->failReason()));
        QTRY_COMPARE_WITH_TIMEOUT(APPLICATION->instances()->getInstanceById(originalId)->getManagedPackVersionID(), newVersion.fileId.toString(), 15000);
        auto* updated = APPLICATION->instances()->getInstanceById(originalId);
        QCOMPARE(QFileInfo(updated->gameRoot()).fileName(), QString(".minecraft"));
        QVERIFY(QFileInfo::exists(marker));
        QVERIFY(!QDir(updated->gameRoot() + "/mods").entryList({"*.jar"}, QDir::Files).isEmpty());
    }
};
static void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("Cannot write CurseForge test fixture");
}

#include "NativeTestAccounts.h"
int main(int argc, char** argv)
{
    const auto original = QDir::currentPath();
    QDir().mkpath(original + "/.validation");
    QTemporaryDir data(original + "/.validation/awake-curseforge-XXXXXX");
    if (!data.isValid()) return 1;
    seedStartupAccount(data.path());
    writeFile(data.path() + "/awakelauncher.cfg", "Language=en_US\nIgnoreJavaWizard=true\nAutomaticJavaDownload=true\nAutomaticJavaSwitch=true\nUserAskedAboutAutomaticJavaDownload=true\nProxyType=HTTP\nProxyAddr=127.0.0.1\nProxyPort=9\n");
    for (const auto& id : {QString("one"), QString("two")}) {
        const auto path = data.path() + "/instances/" + id;
        QDir().mkpath(path);
        writeFile(path + "/instance.cfg", "InstanceType=OneSix\nname=" + id.toUtf8() + "\niconKey=grass\n");
        writeFile(path + "/mmc-pack.json", R"({"formatVersion":1,"components":[{"uid":"net.minecraft","version":"1.21.8","important":true},{"uid":"net.fabricmc.fabric-loader","version":"0.16.14"}]})");
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
        QTimer::singleShot(0, &app, [&] { CurseForgeTest test; result = QTest::qExec(&test, argc, argv); app.quit(); });
        app.exec();
    }
    QDir::setCurrent(original);
    return result;
}
#include "CurseForge_test.moc"
