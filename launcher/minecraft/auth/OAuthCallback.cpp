// SPDX-License-Identifier: GPL-3.0-only
#include "OAuthCallback.h"
#include "BuildConfig.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QLoggingCategory>
#include <mutex>

namespace OAuthCallback {
namespace {
QLoggingCategory::CategoryFilter previousFilter = nullptr;
void authLogFilter(QLoggingCategory* category)
{
    if (previousFilter) previousFilter(category);
    // Qt's HTTP reply handler can log complete callback URLs before our validation runs.
    if (qstrcmp(category->categoryName(), "qt.networkauth.replyhandler") == 0) {
        for (auto type : {QtDebugMsg, QtInfoMsg, QtWarningMsg, QtCriticalMsg}) category->setEnabled(type, false);
    }
}
}
void protectReplyHandlerLogs()
{
    static std::once_flag installed;
    std::call_once(installed, [] { previousFilter = QLoggingCategory::installFilter(authLogFilter); });
}

std::optional<QVariantMap> parse(const QUrl& url)
{
    if (!url.isValid() || url.scheme() != BuildConfig.LAUNCHER_APP_BINARY_NAME || url.host() != "oauth" ||
        url.path() != "/microsoft" || !url.userInfo().isEmpty() || url.port() != -1 || url.hasFragment() ||
        url.toEncoded().size() > 16384)
        return std::nullopt;
    QVariantMap result;
    const auto items = QUrlQuery(url).queryItems(QUrl::FullyDecoded);
    for (const auto& [key, value] : items) {
        if (result.contains(key)) return std::nullopt;
        result.insert(key, value);
    }
    if (result.value("state").toString().isEmpty() ||
        (result.value("code").toString().isEmpty() == result.value("error").toString().isEmpty()) ||
        result.contains("access_token") || result.contains("refresh_token"))
        return std::nullopt;
    result.remove("error_description");
    result.remove("error_uri");
    return result;
}

bool matchesState(const QVariantMap& data, const QString& state)
{
    auto encoded = data.value("state").toString().toUtf8();
    const auto received = QString::fromUtf8(QByteArray::fromPercentEncoding(encoded.replace('+', ' ')));
    return !state.isEmpty() && received == state &&
        (data.value("code").toString().isEmpty() != data.value("error").toString().isEmpty());
}

QString errorCode(const QString& error)
{
    const QStringList known{ "access_denied", "invalid_request", "invalid_client", "invalid_grant", "unauthorized_client",
        "invalid_scope", "server_error", "temporarily_unavailable", "interaction_required", "consent_required",
        "authorization_pending", "authorization_declined", "expired_token", "slow_down" };
    return known.contains(error) ? error : QStringLiteral("unknown_error");
}

bool invalidMinecraftRegistration(int status, const QByteArray& response)
{
    if (status != 403) return false;
    const auto object = QJsonDocument::fromJson(response).object();
    for (const auto& key : { "error", "errorMessage", "message", "developerMessage" }) {
        const auto value = object.value(key).toString();
        if (value.contains("Invalid app registration", Qt::CaseInsensitive) ||
            value.contains("InvalidAppRegistration", Qt::CaseInsensitive)) return true;
    }
    return false;
}
}
