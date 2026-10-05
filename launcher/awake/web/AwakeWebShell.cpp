// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeWebShell.h"
#include "AwakeWebAssets.h"
#include "AwakeWebBridge.h"
#include "AwakeWebPolicy.h"
#include <QDesktopServices>
#include <QApplication>
#include <QEvent>
#include <QCoreApplication>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineCertificateError>
#include <QWebEngineDownloadRequest>
#include <QWebEngineFileSystemAccessRequest>
#include <QWebEnginePage>
#include <QWebEnginePermission>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>

namespace Awake::Web {
namespace {
class RequestBoundary final : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo& info) override
    {
        info.block(!internalUrl(info.requestUrl()) ||
                   (!info.initiator().isEmpty() && !internalUrl(info.initiator())) || info.requestMethod() != "GET");
    }
};

class Page final : public QWebEnginePage {
public:
    Page(QWebEngineProfile* profile, QObject* parent) : QWebEnginePage(profile, parent) {}
protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool mainFrame) override
    {
        if (mainFrame && (url == QUrl("awake://ui/") || url == QUrl("awake://ui/index.html"))) return true;
        if (type == NavigationTypeLinkClicked && mainFrame && externalUrl(url)) QDesktopServices::openUrl(url);
        return false;
    }
    QWebEnginePage* createWindow(WebWindowType) override { return nullptr; }
    QStringList chooseFiles(FileSelectionMode, const QStringList&, const QStringList&) override { return {}; }
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString& message, int line, const QString& source) override
    {
        if (level == ErrorMessageLevel) qWarning() << "Awake frontend error:" << source << line << message;
    }
};
}

Shell::Shell(QWidget* parent) : QWidget(parent)
{
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { updateArtworkFocus(); });
    setObjectName("awakeWebShell");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_loading = new QLabel(tr("Loading Awake…"), this);
    m_loading->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_loading);
    m_profile = new QWebEngineProfile(this);
    m_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    m_profile->setUrlRequestInterceptor(new RequestBoundary(m_profile));
    m_assets = new Assets(m_profile);
    m_profile->installUrlSchemeHandler("awake", m_assets);
    connect(m_profile, &QWebEngineProfile::downloadRequested, this, [](QWebEngineDownloadRequest* request) { request->cancel(); });
    m_view = new QWebEngineView(this);
    m_view->setObjectName("awakeWebView");
    auto* page = new Page(m_profile, m_view);
    m_view->setPage(page);
    page->setBackgroundColor(QColor("#101311"));
    auto* settings = page->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanPaste, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    settings->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, false);
    settings->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);
    layout->addWidget(m_view, 1);
    m_view->hide();
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] { emit failed(tr("The Awake web interface did not connect. The widget interface is available.")); });
    connect(page, &QWebEnginePage::certificateError, this, [](QWebEngineCertificateError error) { error.rejectCertificate(); });
    // Qt 6.11's permission metatype references an unexported move-helper destructor on MSVC.
    // Keep this reference-only slot connection free of typed metatype registration.
    connect(page, SIGNAL(permissionRequested(QWebEnginePermission)), this, SLOT(denyPermission(QWebEnginePermission)));
    connect(page, &QWebEnginePage::fileSystemAccessRequested, this, [](QWebEngineFileSystemAccessRequest request) { request.reject(); });
    connect(page, &QWebEnginePage::renderProcessTerminated, this, [this] {
        if (!m_stopped) emit failed(tr("The Awake web renderer stopped. The widget interface is available."));
    });
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok && !m_stopped) emit failed(tr("The bundled Awake interface could not be loaded. The widget interface is available."));
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &Shell::shutdown);
}

Shell::~Shell()
{
    shutdown();
}

void Shell::denyPermission(const QWebEnginePermission& permission)
{
    QWebEnginePermission denied(permission);
    denied.deny();
}

void Shell::start(Bridge* bridge)
{
    m_bridge = bridge;
    auto* channel = new QWebChannel(m_view->page());
    channel->registerObject("awake", bridge);
    m_view->page()->setWebChannel(channel);
    connect(bridge, &Bridge::ready, this, [this] {
        if (m_stopped || m_ready) return;
        m_ready = true;
        m_timeout->stop();
        qInfo() << "Awake frontend ready" << m_started.elapsed() << "ms";
        m_loading->hide();
        m_view->show();
        setSuspended(m_suspended);
        updateArtworkFocus();
    });
    m_started.start();
    m_timeout->start(20000);
    m_view->setUrl(QUrl("awake://ui/"));
}

void Shell::shutdown()
{
    if (m_stopped) return;
    m_stopped = true;
    m_timeout->stop();
    if (m_bridge) m_bridge->setActive(false);
    if (m_view) {
        m_view->stop();
        m_view->hide();
        m_view->page()->setVisible(false);
        m_view->page()->setWebChannel(nullptr);
    }
    // Legacy fallback can be requested from the bridge's current call; let that call return before deletion.
    if (m_bridge) m_bridge->deleteLater();
    m_bridge = nullptr;
    // Destroy the page/view while its profile and QApplication's Chromium/QtQuick integration are still alive.
    delete m_view;
    m_view = nullptr;
    delete m_profile;
    m_profile = nullptr;
    m_assets = nullptr;
}

void Shell::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    setSuspended(true);
    updateArtworkFocus();
}

void Shell::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    window()->installEventFilter(this);
    setSuspended(false);
    updateArtworkFocus();
}

bool Shell::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window() && (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::WindowStateChange))
        updateArtworkFocus();
    return QWidget::eventFilter(watched, event);
}

void Shell::updateArtworkFocus()
{
    if (!m_bridge) return;
    m_bridge->setArtworkFocused(!m_suspended && isVisible() && !window()->isMinimized() && window()->isActiveWindow() && qApp->applicationState() == Qt::ApplicationActive);
}

void Shell::setSuspended(bool suspended)
{
    if (m_stopped) return;
    m_suspended = suspended;
    if (m_bridge) m_bridge->setActive(!suspended);
    m_view->page()->setVisible(!suspended && m_ready);
    if (!suspended || m_ready)
        m_view->page()->setLifecycleState(suspended ? QWebEnginePage::LifecycleState::Frozen : QWebEnginePage::LifecycleState::Active);
}

void Shell::focusSearch()
{
    if (m_stopped || !m_ready) return;
    m_view->setFocus(Qt::ShortcutFocusReason);
    m_view->page()->runJavaScript("(() => { const search = document.getElementById('instance-search'); if (search && !search.disabled) { search.focus(); search.select(); } })()");
}
}  // namespace Awake::Web
