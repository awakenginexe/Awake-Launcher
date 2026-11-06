// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QString>
namespace Awake::Integration {
inline constexpr quint32 MaxFrameBytes = 1024 * 1024;
struct Request {
    QString id, method, error;
    QJsonObject params;
};
QJsonObject version();
QJsonObject failure(const QString& code, const QString& message, bool retryable = false);
Request parseRequest(const QByteArray& bytes);
QByteArray frame(const QJsonObject& body);
QJsonObject capabilities();
}  // namespace Awake::Integration
