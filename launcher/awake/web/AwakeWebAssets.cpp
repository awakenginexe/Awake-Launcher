// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeWebAssets.h"
#include "AwakeWebPolicy.h"
#include <QBuffer>
#include <QFile>
#include <QMimeDatabase>
#ifdef Q_OS_WIN
#include <QQuickWindow>
#endif
#include <QUuid>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

namespace Awake::Web {
void initialize()
{
#ifdef Q_OS_WIN
    // WARP keeps Chromium's compositor active without opening the game's GPU adapter.
    qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER", "1");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    auto flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (!flags.split(' ').contains("--ignore-gpu-blocklist"))
        flags += " --ignore-gpu-blocklist";
    // Avoid reusing partially rasterized tiles across textbox focus/caret repaints on WARP.
    if (!flags.split(' ').contains("--disable-partial-raster"))
        flags += " --disable-partial-raster";
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
#endif
    QWebEngineUrlScheme scheme("awake");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(scheme);
}

Assets::Assets(QObject* parent) : QWebEngineUrlSchemeHandler(parent) {}

QString Assets::putImage(const QByteArray& png)
{
    if (png.isEmpty() || png.size() > 16 * 1024 * 1024)
        return {};
    const auto path = "/images/" + QUuid::createUuid().toString(QUuid::Id128) + ".png";
    m_images.insert(path, png);
    return "awake://ui" + path;
}

void Assets::removeImage(const QString& url) { m_images.remove(QUrl(url).path()); }

void Assets::requestStarted(QWebEngineUrlRequestJob* request)
{
    const auto url = request->requestUrl();
    if (request->requestMethod() != "GET" || !internalUrl(url)) {
        request->fail(QWebEngineUrlRequestJob::RequestDenied);
        return;
    }
    QByteArray bytes;
    QByteArray mime;
    const auto resource = resourcePath(url);
    if (!resource.isEmpty()) {
        QFile file(resource);
        if (!file.open(QIODevice::ReadOnly)) {
            request->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        bytes = file.readAll();
        mime = QMimeDatabase().mimeTypeForFile(resource, QMimeDatabase::MatchExtension).name().toUtf8();
        if (resource.endsWith(".js"))
            mime = "text/javascript";
        if (resource == ":/backgrounds/awake-minecraft")
            mime = "image/png";
    } else {
        const auto found = m_images.constFind(url.path());
        if (found == m_images.cend()) {
            request->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        bytes = *found;
        mime = "image/png";
    }
    auto* buffer = new QBuffer(request);
    buffer->setData(bytes);
    buffer->open(QIODevice::ReadOnly);
    request->setAdditionalResponseHeaders({
        {"Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; connect-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"},
        {"X-Content-Type-Options", "nosniff"}
    });
    request->reply(mime, buffer);
}
}  // namespace Awake::Web
