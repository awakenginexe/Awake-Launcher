// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QUrl>
#include <QVariantMap>
#include <optional>

namespace OAuthCallback {
void protectReplyHandlerLogs();
std::optional<QVariantMap> parse(const QUrl& url);
bool matchesState(const QVariantMap& data, const QString& state);
QString errorCode(const QString& error);
bool invalidMinecraftRegistration(int status, const QByteArray& response);
}
