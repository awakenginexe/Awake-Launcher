// SPDX-License-Identifier: GPL-3.0-only
#include <QAction>
#include <QBuffer>
#include <QDialog>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QPushButton>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <memory>
#include "Application.h"
#include "BuildConfig.h"
#include "updater/AwakeUpdateChecker.h"
#include "HardwareInfo.h"
#include "LaunchController.h"
#include "minecraft/auth/AccountList.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "awake/web/AwakeWebAssets.h"
#include "awake/web/AwakeWebBridge.h"
#include "awake/web/AwakeWebShell.h"
#include "settings/SettingsObject.h"
#include "translations/TranslationsModel.h"
#include "ui/MainWindow.h"
#include "ui/InstanceWindow.h"
#ifdef Q_OS_WIN
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

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
    void launcherCompositorUsesSoftwareDevice()
    {
        auto* compositor = view->findChild<QQuickWidget*>();
        QVERIFY(compositor);
#ifdef Q_OS_WIN
        auto* renderer = compositor->quickWindow()->rendererInterface();
        QCOMPARE(renderer->graphicsApi(), QSGRendererInterface::Direct3D11);
        auto* device = static_cast<ID3D11Device*>(renderer->getResource(compositor->quickWindow(), QSGRendererInterface::DeviceResource));
        QVERIFY(device);
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        QVERIFY(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))));
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        QVERIFY(SUCCEEDED(dxgiDevice->GetAdapter(&adapter)));
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
        QVERIFY(SUCCEEDED(adapter.As(&adapter1)));
        DXGI_ADAPTER_DESC1 description{};
        QVERIFY(SUCCEEDED(adapter1->GetDesc1(&description)));
        QVERIFY(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE);
        QVERIFY(evaluate("Boolean(document.createElement('canvas').getContext('webgl'))").toBool());
#else
        QCOMPARE(compositor->quickWindow()->rendererInterface()->graphicsApi(), QSGRendererInterface::Software);
#endif
        QVERIFY(!view->grab().isNull());
    }
    void welcomeHeadOpensTheSkinsPanelWithSoftwarePreview()
    {
        auto accounts = APPLICATION->accounts();
        auto previous = accounts->defaultAccount();
        auto account = MinecraftAccount::createBlankMSA();
        account->accountData()->minecraftProfile.name = "Skin preview fixture";
        account->accountData()->minecraftProfile.id = "c0123456789abcdef0123456789abcdef";
        account->accountData()->yggdrasilToken.token = "private-skin-ui-fixture";
        Cape cape{"cape-fixture", {}, "Test cape", {}};
        QImage capeImage(64, 32, QImage::Format_ARGB32); capeImage.fill(Qt::cyan);
        for (int y = 1; y < 17; ++y)
            for (int x = 1; x < 11; ++x) capeImage.setPixelColor(x, y, QColor(255, 128, 0));
        QBuffer capeBytes(&cape.data); QVERIFY(capeBytes.open(QIODevice::WriteOnly)); QVERIFY(capeImage.save(&capeBytes, "PNG"));
        account->accountData()->minecraftProfile.capes.insert(cape.id, cape);
        account->accountData()->minecraftProfile.currentCape = cape.id;
        QFile texture(":/awake-skins/wide/steve.png");
        QVERIFY(texture.open(QIODevice::ReadOnly));
        account->accountData()->minecraftProfile.skin.data = texture.readAll();
        account->accountData()->minecraftProfile.skin.variant = "CLASSIC";
        accounts->addAccount(account);
        accounts->setDefaultAccount(account);
        const auto size = window->size();
        auto cleanup = qScopeGuard([&] {
            evaluate("document.querySelector('.skins-dialog .modal-close-btn')?.click()");
            for (int i = 0; i < accounts->count(); ++i)
                if (accounts->at(i) == account) { accounts->removeAccount(accounts->index(i, 0)); break; }
            accounts->setDefaultAccount(previous);
            window->resize(size);
        });
        window->resize(1320, 780);
        window->raise();
        window->activateWindow();
        bridge->scheduleState();
        QTRY_VERIFY(evaluate("Boolean(document.querySelector('.welcome-head')?.complete && document.querySelector('.welcome-head')?.naturalWidth === 8)").toBool());
        evaluate("document.querySelector('.welcome-name').click()");
        QTRY_COMPARE(evaluate("document.querySelectorAll('.skin-default').length").toInt(), 9);
        QCOMPARE(window->minimumSize(), QSize(1320, 780));
        QTRY_VERIFY(evaluate("Boolean(document.querySelector('.skin-canvas')?.dataset.yaw)").toBool());
        QTRY_VERIFY(evaluate("getComputedStyle(document.querySelector('.welcome-name')).borderRadius === '4px'").toBool());
        QCOMPARE(evaluate("[...document.querySelectorAll('.skins-dialog .modal-footer button')].map(button => button.textContent.trim()).join('|')").toString(), QString("Reset to default|Reset to Minecraft default|Save locally|Apply to account"));
        QSignalSpy skinRequests(bridge, &Awake::Web::Bridge::catalogFinished);
        const auto currentPixels = evaluate("document.querySelector('.skin-canvas').toDataURL()").toString();
        evaluate("document.querySelector('.skin-default').click(); document.querySelector('.skin-cape-choice').click()");
        QTRY_VERIFY(!evaluate("document.querySelector('.skins-dialog .btn-primary').disabled").toBool());
        evaluate("document.querySelector('.skin-reset-current').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.skins-dialog .btn-primary').disabled").toBool());
        QTRY_COMPARE(evaluate("document.querySelector('.skin-canvas').toDataURL()").toString(), currentPixels);
        QVERIFY(evaluate("document.querySelectorAll('.skin-cape-choice')[1].getAttribute('aria-pressed') === 'true'").toBool());
        evaluate("document.querySelector('.skin-reset-minecraft').click()");
        QTRY_VERIFY(!evaluate("document.querySelector('.skins-dialog .btn-primary').disabled").toBool());
        QVERIFY(evaluate("document.querySelector('.skin-feedback').textContent.includes('Apply skin')").toBool());
        QVERIFY(evaluate("document.querySelector('.skin-cape-image').style.backgroundImage.includes('awake://ui/images/')").toBool());
        evaluate("document.querySelector('.skin-reset-current').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.skins-dialog .btn-primary').disabled").toBool());
        QCOMPARE(skinRequests.count(), 0);
        if (qEnvironmentVariableIsSet("AWAKE_SKIN_MEASURE")) {
            QFile styles(qEnvironmentVariable("AWAKE_SKIN_STYLE"));
            if (styles.open(QIODevice::ReadOnly)) {
                const auto css = styles.read(4096).toBase64();
                evaluate(QString("(() => {const style=document.createElement('style');style.textContent=atob('%1');document.head.append(style)})()").arg(QString::fromLatin1(css)));
            }
            QTest::qWait(400);
            for (const auto& mode : {QString("idle"), QString("scroll")}) {
                evaluate(QString(R"JS((() => {
                    const scroller = document.querySelector('.skin-tools').scrollHeight > document.querySelector('.skin-tools').clientHeight ? document.querySelector('.skin-tools') : document.querySelector('.skins-body');
                    window.__skinFrames = { done:false, deltas:[] };
                    let start, last;
                    requestAnimationFrame(function frame(now) {
                        start ??= now;
                        if (last) window.__skinFrames.deltas.push(now - last);
                        last = now;
                        if ('%1' === 'scroll') scroller.scrollTop = (scroller.scrollHeight - scroller.clientHeight) * (0.5 - 0.5 * Math.cos((now - start) / 1000 * Math.PI));
                        if (now - start < 1600) requestAnimationFrame(frame);
                        else { scroller.scrollTop = 0; window.__skinFrames.done = true; }
                    });
                })())JS").arg(mode));
                QTRY_VERIFY_WITH_TIMEOUT(evaluate("window.__skinFrames.done").toBool(), 10000);
                qInfo().noquote() << "Skin scroll frames" << mode << evaluate("JSON.stringify((() => {const values=window.__skinFrames.deltas.sort((a,b)=>a-b);return {frames:values.length,p95:values[Math.floor(values.length*.95)],max:values.at(-1)}})())").toString();
            }
        }
        QVERIFY(evaluate("document.querySelector('.skins-dialog .btn-primary').disabled").toBool());
        evaluate("document.querySelector('.skin-default').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.skins-dialog .btn-primary').disabled").toBool());
        QVERIFY(evaluate("document.querySelector('.navigation').inert && document.querySelector('.stage').inert").toBool());
        const auto before = evaluate("document.querySelector('.skin-canvas').dataset.yaw").toString();
        const auto pixelsBefore = evaluate("document.querySelector('.skin-canvas').toDataURL()").toString();
        evaluate("document.querySelector('.skin-stage').dispatchEvent(new KeyboardEvent('keydown', {key:'ArrowRight', bubbles:true}))");
        QTRY_VERIFY(evaluate("document.querySelector('.skin-canvas').dataset.yaw").toString() != before);
        QTRY_VERIFY(evaluate("document.querySelector('.skin-canvas').toDataURL()").toString() != pixelsBefore);
        evaluate("document.querySelector('#skin-account').click()");
        QTRY_VERIFY(evaluate("[...document.querySelectorAll('.themed-select-menu [role=option] img')].every(image => image.complete && image.naturalWidth === 8)").toBool());
        QVERIFY(evaluate("document.querySelectorAll('.themed-select-menu [role=option] img').length === document.querySelectorAll('.themed-select-menu [role=option]').length").toBool());
        evaluate("document.querySelector('#skin-account').dispatchEvent(new KeyboardEvent('keydown', {key:'Escape',bubbles:true}))");
        QTRY_VERIFY(!evaluate("Boolean(document.querySelector('.themed-select-menu'))").toBool());
        const auto canvasBeforeScroll = evaluate("document.querySelector('.skin-canvas').getBoundingClientRect().top").toDouble();
        evaluate("document.querySelector('.skin-tools').scrollTop = document.querySelector('.skin-tools').scrollHeight");
        QCOMPARE(evaluate("document.querySelector('.skin-canvas').getBoundingClientRect().top").toDouble(), canvasBeforeScroll);
        QCOMPARE(evaluate("document.querySelectorAll('.skin-cape-choice').length").toInt(), 2);
        QVERIFY(evaluate("document.querySelector('.skin-cape-image').style.backgroundImage.startsWith('url(\"awake://ui/images/')").toBool());
        QCOMPARE(evaluate("getComputedStyle(document.querySelector('.skin-cape-image')).backgroundPosition").toString(), QString("-4px -4px"));
        evaluate("for (let i=0;i<13;i++) document.querySelector('.skin-stage').dispatchEvent(new KeyboardEvent('keydown', {key:'ArrowRight', bubbles:true}))");
        QTRY_VERIFY(evaluate(R"JS((() => {
            const canvas = document.querySelector('.skin-canvas');
            const pixels = canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data;
            let orange = 0;
            for (let i=0;i<pixels.length;i+=4) if (pixels[i]===255 && pixels[i+1]===128 && pixels[i+2]===0) orange++;
            return orange > 1000;
        })())JS").toBool());
        QCOMPARE(evaluate("document.querySelectorAll('[data-skin-part]').length").toInt(), 7);
        const auto capePixels = evaluate("document.querySelector('.skin-canvas').toDataURL()").toString();
        evaluate("document.querySelector('[data-skin-part=cape]').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.skin-canvas').toDataURL()").toString() != capePixels);
        QVERIFY(evaluate("document.querySelector('.skins-dialog .btn-primary').disabled").toBool());
        evaluate("document.querySelector('[data-skin-part=cape]').click()");
        QTRY_COMPARE(evaluate("document.querySelector('.skin-canvas').toDataURL()").toString(), capePixels);
        evaluate("document.querySelector('.skin-cape-choice').click()");
        QTRY_VERIFY(!evaluate("document.querySelector('.skins-dialog .btn-primary').disabled").toBool());
        evaluate("document.querySelectorAll('.skin-cape-choice')[1].click()");
        QTRY_VERIFY(evaluate("document.querySelector('.skins-dialog .btn-primary').disabled").toBool());
        QCOMPARE(evaluate("getComputedStyle(document.querySelector('.skins-dialog .btn-primary')).boxShadow").toString(), QString("none"));
        evaluate("document.querySelector('.skin-tools').scrollTop = 0");
        evaluate("[...document.querySelectorAll('.skin-model-options button')].find(button => button.textContent.trim() === 'Slim').click()");
        evaluate("document.querySelector('.skin-default').click()");
        QTRY_COMPARE(evaluate("document.querySelector('.skin-canvas').dataset.variant").toString(), QString("SLIM"));
        QTRY_VERIFY(!evaluate("document.querySelector('.skins-dialog .btn-primary').disabled").toBool());
        QTRY_VERIFY(evaluate("document.querySelectorAll('.skin-default .skin-thumbnail-canvas').length === 9 && [...document.querySelectorAll('.skin-default .skin-thumbnail-canvas')].every(canvas => canvas.width > 0 && canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data.some((value,index) => index % 4 === 3 && value > 0))").toBool());
        evaluate("document.querySelector('.skin-local-save button').click()");
        QTRY_VERIFY(evaluate("Boolean(document.querySelector('.skin-library-grid .skin-thumbnail-canvas'))").toBool());
        QTRY_VERIFY(evaluate("(() => { const canvas = document.querySelector('.skin-library-grid .skin-thumbnail-canvas'); return canvas.width > 0 && canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data.some((value,index) => index % 4 === 3 && value > 0); })()").toBool());
        evaluate("document.querySelector('.skin-stage').dispatchEvent(new KeyboardEvent('keydown', {key:'Home', bubbles:true})); document.querySelector('[data-skin-part=body-overlay]').click()");
        QTRY_COMPARE(evaluate("getComputedStyle(document.querySelector('.skins-overlay')).opacity").toString(), QString("1"));
        QTest::qWait(300);
        view->repaint();
        const auto output = QDir(QCoreApplication::applicationDirPath()).filePath(".validation");
        QVERIFY(QDir().mkpath(output));
        QVERIFY(view->grab().save(output + "/skins-native-preview.png"));
        evaluate("document.querySelector('.skins-dialog .modal-close-btn').click()");
        QTRY_VERIFY(!evaluate("Boolean(document.querySelector('.skins-dialog'))").toBool());
    }
    void startupKeepsSuspendedRendererFrozen()
    {
        QWidget host;
        auto* shell = new Awake::Web::Shell(&host);
        auto* native = new Awake::Web::Bridge(shell->assets(), [](const QString&) { return true; },
            [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; }, shell);
        host.show();
        shell->show();
        shell->start(native);
        shell->setSuspended(true);
        QTRY_VERIFY_WITH_TIMEOUT(shell->isReady(), 20000);
        auto* renderer = shell->findChild<QWebEngineView*>();
        QVERIFY(renderer);
        QTRY_COMPARE(renderer->page()->lifecycleState(), QWebEnginePage::LifecycleState::Frozen);
        QVERIFY(!renderer->page()->isVisible());
        shell->setSuspended(false);
        QTRY_COMPARE(renderer->page()->lifecycleState(), QWebEnginePage::LifecycleState::Active);
        host.close();
        window->activateWindow();
        bridge->setArtworkFocused(true);
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
    void bundledBackgroundLoadsWithoutReplacingImageOnRefresh()
    {
        bridge->setArtworkFocused(true);
        evaluate("document.querySelectorAll('.instance-select')[1].click()");
        QTRY_VERIFY_WITH_TIMEOUT(evaluate("(() => { const image = document.querySelector('.artwork-image'); return image && image.complete && image.naturalWidth === 1920 && image.src === 'awake://ui/assets/minecraft-background.png'; })()").toBool(), 5000);
        evaluate("window.__backgroundBeforeRefresh = document.querySelector('.artwork-image')");
        auto* timer = bridge->findChild<QTimer*>("awakeArtworkTimer");
        QVERIFY(timer);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QTest::qWait(500);
        QVERIFY(evaluate("window.__backgroundBeforeRefresh === document.querySelector('.artwork-image')").toBool());
        evaluate("document.querySelectorAll('.instance-select')[0].click()");
    }
    void startupReplaysCachedFallbackWithoutSelectingInstance_data()
    {
        QTest::addColumn<bool>("suspended");
        QTest::newRow("visible") << false;
        QTest::newRow("suspended") << true;
    }
    void startupReplaysCachedFallbackWithoutSelectingInstance()
    {
        QFETCH(bool, suspended);
        const auto previousSelection = APPLICATION->settings()->get("SelectedInstance");
        auto* previousView = view;
        auto restore = qScopeGuard([&] {
            view = previousView;
            APPLICATION->settings()->set("SelectedInstance", previousSelection);
            window->activateWindow();
            bridge->setArtworkFocused(true);
        });
        APPLICATION->settings()->set("SelectedInstance", "two");
        QWidget host;
        auto* shell = new Awake::Web::Shell(&host);
        auto* native = new Awake::Web::Bridge(shell->assets(), [](const QString&) { return true; },
            [](const QString&, const QString&) { return QVariantMap{{"ok", true}}; }, shell);
        host.show();
        shell->show();
        native->setArtworkFocused(true);
        QSignalSpy artwork(native, &Awake::Web::Bridge::artworkChanged);
        native->scheduleState();
        QTRY_VERIFY_WITH_TIMEOUT(!artwork.isEmpty(), 5000);
        QCOMPARE(artwork.last().at(1).toString(), QString("awake://ui/assets/minecraft-background.png"));
        shell->setSuspended(suspended);
        shell->start(native);
        view = shell->findChild<QWebEngineView*>();
        QTRY_VERIFY_WITH_TIMEOUT(shell->isReady(), 20000);
        if (suspended) shell->setSuspended(false);
        QTRY_VERIFY_WITH_TIMEOUT(evaluate("(() => { const image = document.querySelector('.artwork-image'); return image && image.complete && image.naturalWidth === 1920 && image.src === 'awake://ui/assets/minecraft-background.png'; })()").toBool(), 5000);
        QVERIFY(!evaluate("Boolean(document.querySelector('.artwork-caption[role=status]'))").toBool());
        host.close();
    }
    void updateCheckThroughNativeBridge()
    {
        auto* checker = APPLICATION->awakeUpdateChecker();
        QVERIFY(checker);
        QVERIFY(window->webFrontendActive());
        checker->setAutomaticallyChecksForUpdates(false);
        const auto response = bridge->invokeAction("checkForUpdates", "");
        QVERIFY(response.value("ok").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(checker->state().value("status").toString(), "error", 5000);
        QTRY_VERIFY(evaluate("document.querySelector('.update-dialog') !== null").toBool());
        QCOMPARE(evaluate("document.querySelector('.update-versions dd').textContent.trim()").toString(), checker->state().value("currentVersion").toString());
        QTRY_VERIFY(evaluate("document.querySelector('.library-panel')?.inert || document.querySelector('.instance-select').closest('[inert]') !== null").toBool());
        evaluate("document.querySelector('.update-dialog .modal-close-btn').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.update-dialog') === null").toBool());
    }
    void creationFailuresKeepTechnicalDetails()
    {
        {
            const QString loader = "Fabric";
            QSignalSpy failures(bridge, &Awake::Web::Bridge::operationFailed);
            const auto payload = QJsonDocument(QJsonObject{{"name", "Failure fixture"}, {"version", "1.21.8"}, {"loader", loader}}).toJson();
            QVERIFY(bridge->invokeAction("createQuick", QString::fromUtf8(payload)).value("ok").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(!failures.isEmpty(), 10000);
            const auto detail = failures.last().at(1).toString();
            QVERIFY(detail.contains("1.21.8"));
            QVERIFY(detail.contains('\n'));
            QVERIFY(detail.contains("Proxy connection refused"));
            QTRY_VERIFY(evaluate("document.querySelector('.operation-error pre')?.textContent.includes('1.21.8')").toBool());
            evaluate("document.querySelector('.operation-error .quiet').click()");
            QTRY_VERIFY(evaluate("document.querySelector('.operation-error') === null").toBool());
        }
    }
    void nativeSearchShortcut()
    {
        window->activateWindow();
        QTRY_VERIFY(window->isActiveWindow());
        view->setFocus();
        QTest::keyClick(view, Qt::Key_F, Qt::ControlModifier);
        QTRY_COMPARE(evaluate("document.activeElement.id").toString(), QString("instance-search"));
    }
    void javaPickerUsesNativeSettings()
    {
        evaluate("document.querySelector('.account-actions .settings-button').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.settings-dialog') !== null").toBool());
        evaluate("document.querySelectorAll('.settings-tab-btn')[1].click()");
        QTRY_COMPARE(evaluate("document.querySelectorAll('.java-option').length").toInt(), 8);
        evaluate("document.querySelector('[data-java-profile=zulu]').click()");
        QTRY_COMPARE(APPLICATION->settings()->get("AwakeJavaProfile").toString(), QString("zulu"));
        QVERIFY(APPLICATION->settings()->get("AutomaticJavaDownload").toBool());
        evaluate("document.querySelector('[data-java-profile=awake]').click()");
        QTRY_COMPARE(APPLICATION->settings()->get("AwakeJavaProfile").toString(), QString("awake"));
        evaluate("document.querySelector('.modal-close-btn').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.settings-dialog') === null").toBool());
    }
    void themedJvmMenuUsesNativePreferences()
    {
        const auto previousPreset = APPLICATION->settings()->get("AwakeJvmPreset");
        evaluate("document.querySelector('.account-actions .settings-button').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.settings-dialog') !== null").toBool());
        evaluate("document.querySelectorAll('.settings-tab-btn')[1].click()");
        QTRY_VERIFY(evaluate("document.querySelector('#global-jvm-preset')?.getAttribute('role') === 'combobox'").toBool());
        evaluate("document.querySelector('#global-jvm-preset').click()");
        QTRY_COMPARE(evaluate("document.querySelectorAll('#global-jvm-preset-listbox [role=option]').length").toInt(), 4);
        QCOMPARE(evaluate("getComputedStyle(document.querySelector('#global-jvm-preset-listbox')).position").toString(), QString("fixed"));
        evaluate("document.querySelector('#global-jvm-preset-listbox [data-value=performance]').click()");
        QTRY_COMPARE(APPLICATION->settings()->get("AwakeJvmPreset").toString(), QString("performance"));
        QTRY_VERIFY(evaluate("document.querySelector('#global-jvm-preset-listbox') === null").toBool());
        evaluate("document.querySelector('.settings-dialog .modal-close-btn').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.settings-dialog') === null").toBool());
        APPLICATION->settings()->set("AwakeJvmPreset", previousPreset);
        bridge->scheduleState();
    }
    void memoryPresetsAndBrandingUseNativeState()
    {
        QCOMPARE(bridge->snapshot().value("totalMemoryMb").toULongLong(), HardwareInfo::installedRamMiB());
        auto* title = window->findChild<QLabel*>("awakeBrandTitle");
        QVERIFY(title);
        QCOMPARE(title->font().pixelSize(), 28);
        QCOMPARE(QFontInfo(title->font()).family(), QString("Bayon"));
        QCOMPARE(title->text(), QString("AWAKE LAUNCHER %1").arg(BuildConfig.versionString()));
        QVERIFY(!APPLICATION->logo().isNull());
        auto* logo = window->findChild<QLabel*>("titleBarLogo");
        QVERIFY(logo);
        QVERIFY2(logo->contentsRect().width() + 1.0 / logo->pixmap().devicePixelRatio() >= logo->pixmap().deviceIndependentSize().width(),
            qPrintable(QString("Logo content %1, pixmap logical width %2, DPR %3")
                .arg(logo->contentsRect().width()).arg(logo->pixmap().deviceIndependentSize().width())
                .arg(logo->pixmap().devicePixelRatio())));
        evaluate("document.querySelector('.account-actions .settings-button').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.settings-dialog') !== null").toBool());
        evaluate("document.querySelectorAll('.settings-tab-btn')[1].click()");
        QTRY_COMPARE(evaluate("document.querySelectorAll('.ram-preset').length").toInt(), 9);
        const auto previous = APPLICATION->settings()->get("MaxMemAlloc");
        const auto overrideMb = HardwareInfo::installedRamMiB() + 1024;
        evaluate(QString("(() => { const input = document.querySelector('.settings-dialog input[type=number]'); input.value = '%1'; input.dispatchEvent(new Event('change', {bubbles:true})); })()")
                     .arg(overrideMb));
        QTRY_COMPARE(APPLICATION->settings()->get("MaxMemAlloc").toULongLong(), overrideMb);
        bridge->setPreference("maxMem", previous);
        evaluate("document.querySelectorAll('.settings-tab-btn')[3].click()");
        QTRY_VERIFY(evaluate("document.querySelector('.brand-logo')?.complete && document.querySelector('.brand-logo')?.naturalWidth > 0").toBool());
        QCOMPARE(evaluate("document.querySelector('.brand-version').textContent.trim()").toString(), BuildConfig.versionString());
        QTest::qWait(150);
        window->grab().save("W:/.validation/branding-020-native.png");
        evaluate("document.querySelector('.modal-close-btn').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.settings-dialog') === null").toBool());
    }
    void missingAccountPopupOpensWebAccounts()
    {
        auto* accounts = APPLICATION->accounts();
        QList<MinecraftAccountPtr> previousAccounts;
        while (accounts->count()) {
            previousAccounts.append(accounts->at(0));
            accounts->removeAccount(accounts->index(0, 0));
        }
        bool captured = false;
        QTimer clickAdd;
        clickAdd.setInterval(20);
        connect(&clickAdd, &QTimer::timeout, this, [&] {
            auto* popup = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!popup || popup->objectName() != "awakeAccountRequired") return;
            clickAdd.stop();
            captured = popup->windowFlags().testFlag(Qt::FramelessWindowHint);
            const auto output = QDir(QCoreApplication::applicationDirPath()).filePath(".validation");
            QDir().mkpath(output);
            popup->grab().save(output + "/account-required.png");
            popup->findChild<QPushButton*>("accountSetupAdd")->click();
        });
        clickAdd.start();
        LaunchController controller;
        controller.setInstance(APPLICATION->instances()->getInstanceById("one"));
        controller.setParentWidget(window);
        controller.start();
        QVERIFY(captured);
        QTRY_VERIFY(evaluate("document.querySelector('.empty-accounts') !== null").toBool());
        QTRY_VERIFY(evaluate("document.activeElement.matches('.modal-footer .btn-primary')").toBool());
        QVERIFY(!QApplication::activeModalWidget());
        evaluate("document.querySelector('.modal-close-btn').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.empty-accounts') === null").toBool());
        for (const auto& account : previousAccounts) accounts->addAccount(account);
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
        QVERIFY(bridge->invokeAction("accounts", {}).value("ok").toBool());
        QTRY_VERIFY(evaluate("document.querySelector('.account-card-list') !== null").toBool());
        evaluate("document.querySelector('.modal-close-btn').click()");
        QTRY_VERIFY(evaluate("document.querySelector('.account-card-list') === null").toBool());
        for (const auto& action : {QString("application"), QString("manage"), QString("launchOptions")}) {
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
    writeFixture(data.path() + "/awakelauncher.cfg", "Language=en_US\nAwakeGpuChoiceSeen=true\nIgnoreJavaWizard=true\nAutomaticJavaDownload=true\nAutomaticJavaSwitch=true\nUserAskedAboutAutomaticJavaDownload=true\nProxyType=HTTP\nProxyAddr=127.0.0.1\nProxyPort=9\n");
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
    Awake::Web::initialize();
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
