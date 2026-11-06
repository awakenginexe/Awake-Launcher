// SPDX-License-Identifier: GPL-3.0-only
#include "Protocol.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
namespace Awake::Integration {
QJsonObject version()
{
    return { { "major", 1 }, { "minor", 0 } };
}
QJsonObject failure(const QString& code, const QString& message, bool retryable)
{
    return { { "error", QJsonObject{ { "code", code }, { "message", message }, { "retryable", retryable } } } };
}
Request parseRequest(const QByteArray& bytes)
{
    Request out;
    out.error = "INVALID_REQUEST";
    if (bytes.isEmpty() || bytes.size() > MaxFrameBytes)
        return out;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return out;
    const auto body = document.object();
    static const QRegularExpression idPattern(QRegularExpression::anchoredPattern("[A-Za-z0-9_-]{1,64}"));
    static const QRegularExpression methodPattern(QRegularExpression::anchoredPattern("[A-Za-z0-9_.-]{1,64}"));
    if (body.size() != 4 || !body["requestId"].isString() || !idPattern.match(body["requestId"].toString()).hasMatch() ||
        !body["method"].isString() || !methodPattern.match(body["method"].toString()).hasMatch() || !body["params"].isObject() ||
        !body["protocol"].isObject())
        return out;
    const auto protocol = body["protocol"].toObject();
    if (protocol.size() != 2 || !protocol["major"].isDouble() || !protocol["minor"].isDouble() ||
        protocol["major"].toDouble() != protocol["major"].toInt(-1) || protocol["minor"].toDouble() != protocol["minor"].toInt(-1) ||
        protocol["minor"].toInt(-1) < 0)
        return out;
    out.id = body["requestId"].toString();
    if (protocol["major"].toInt() != 1 || protocol["minor"].toInt() != 0) {
        out.error = "UNSUPPORTED_PROTOCOL";
        return out;
    }
    out.method = body["method"].toString();
    out.params = body["params"].toObject();
    out.error.clear();
    return out;
}
QByteArray frame(const QJsonObject& body)
{
    const auto json = QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (json.size() > MaxFrameBytes)
        return {};
    QByteArray bytes(4, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(json.size()), bytes.data());
    bytes.append(json);
    return bytes;
}
QJsonObject capabilities()
{
    QJsonObject result;
    for (const auto* name : { "hello", "launcher.info", "instances.list", "mods.list" })
        result[name] = true;
    for (const auto* name : { "providers.search", "providers.versions", "instances.create", "plans.evaluate", "plans.prepare",
                              "transactions.apply", "operations.get", "operations.cancel", "events.subscribe", "launch.request",
                              "recovery.list", "recovery.run", "instances.enroll" })
        result[name] = false;
    return result;
}
}  // namespace Awake::Integration
