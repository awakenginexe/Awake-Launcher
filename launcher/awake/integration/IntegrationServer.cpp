// SPDX-License-Identifier: GPL-3.0-only
#include "IntegrationServer.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include "InstanceDiscovery.h"
#include "ProfileAccess.h"
namespace Awake::Integration {
IntegrationServer::IntegrationServer(QObject* parent) : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, &IntegrationServer::acceptClients);
}
IntegrationServer::~IntegrationServer()
{
    stop();
}
bool IntegrationServer::start(const QString& dataRoot, QJsonObject info, Handler handler, QString* reason)
{
    const auto reject = [reason](const QString& message) {
        if (reason)
            *reason = message;
        return false;
    };
    if (m_owner)
        return reject("The coordinator is already started.");
#ifndef Q_OS_WIN
    // shortcut: only Windows IPC access controls are validated, enable other platforms after peer/ACL tests.
    Q_UNUSED(dataRoot)
    Q_UNUSED(info)
    Q_UNUSED(handler)
    return reject("Modpack Manager IPC is currently supported on Windows only.");
#else
    const auto root = QFileInfo(dataRoot).canonicalFilePath();
    if (!profileAccessIsPrivate(root))
        return reject("Integration requires a current-user-owned local profile without public write access.");
    QString directory = root;
    for (const auto* part : { "integration", "v1" }) {
        directory = QDir(directory).filePath(part);
        const QFileInfo before(directory);
        if (before.isSymLink() || before.isJunction())
            return reject("Integration directories must not be filesystem links.");
        // Set the current user as owner even when the Windows token defaults to a group.
        const bool available = before.exists() ? before.isDir() :
            QDir().mkdir(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        if (!available || !profileAccessIsPrivate(directory))
            return reject("The integration directory is unavailable or not private.");
    }
    auto owner = std::make_unique<QLockFile>(QDir(directory).filePath("coordinator.lock"));
    owner->setStaleLockTime(0);
    if (!owner->tryLock())
        return reject("Another coordinator owns this data profile.");
    m_descriptor = QDir(directory).filePath("endpoint.json");
    if (QFileInfo(m_descriptor).isSymLink()) {
        m_descriptor.clear();
        return reject("The endpoint descriptor must not be a link.");
    }
    const auto session = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto serverName = "awake-manager-v1-" + session;
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(8);
    if (!m_server.listen(serverName)) {
        m_descriptor.clear();
        return reject("Cannot listen on the integration endpoint.");
    }
    info["protocol"] = version();
    info["sessionId"] = session;
    info["serverName"] = serverName;
    info["pid"] = QString::number(QCoreApplication::applicationPid());
    QSaveFile descriptor(m_descriptor);
    descriptor.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(info).toJson(QJsonDocument::Compact);
    if (!descriptor.open(QIODevice::WriteOnly) || !descriptor.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        descriptor.write(bytes) != bytes.size() || !descriptor.commit()) {
        m_server.close();
        m_descriptor.clear();
        return reject("Cannot publish the integration endpoint descriptor.");
    }
    m_info = std::move(info);
    m_handler = std::move(handler);
    m_owner = std::move(owner);
    return true;
#endif
}
void IntegrationServer::stop()
{
    for (auto* socket : std::as_const(m_clients)) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    m_clients.clear();
    m_server.close();
    if (m_owner && !m_descriptor.isEmpty()) {
        QFile current(m_descriptor);
        if (current.open(QIODevice::ReadOnly)) {
            const auto session = QJsonDocument::fromJson(current.read(16384)).object().value("sessionId");
            current.close();
            if (session == m_info["sessionId"])
                QFile::remove(m_descriptor);
        }
    }
    m_descriptor.clear();
    m_owner.reset();
}
void IntegrationServer::acceptClients()
{
    while (m_server.hasPendingConnections()) {
        auto* socket = m_server.nextPendingConnection();
        if (!socket)
            break;
        socket->setParent(this);
        if (m_clients.size() >= 8) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_clients.insert(socket);
        socket->setReadBufferSize(MaxFrameBytes + 4);
        auto* timer = new QTimer(socket);
        timer->setSingleShot(true);
        timer->start(30000);
        connect(timer, &QTimer::timeout, socket, &QLocalSocket::abort);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_clients.remove(socket);
            socket->deleteLater();
        });
        struct Client {
            QByteArray buffer;
            quint32 expected = 0;
            bool negotiated = false, busy = false;
        };
        auto state = std::make_shared<Client>();
        const auto send = [socket = QPointer<QLocalSocket>(socket), state, timer](const QString& id, QJsonObject body, bool close = false) {
            if (!socket)
                return;
            if (socket->state() != QLocalSocket::ConnectedState || socket->bytesToWrite() > 0) {
                socket->abort();
                return;
            }
            body["protocol"] = version();
            body["requestId"] = id.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(id);
            auto bytes = frame(body);
            if (bytes.isEmpty()) {
                auto small = failure("LIMIT_EXCEEDED", "Response exceeds the frame limit; use a smaller page.");
                small["protocol"] = version();
                small["requestId"] = body["requestId"];
                bytes = frame(small);
            }
            socket->write(bytes);
            state->busy = false;
            timer->start(30000);
            if (close)
                socket->disconnectFromServer();
        };
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, state, timer, send] {
            if (state->busy) {
                send({}, failure("BUSY", "Wait for the previous response.", true), true);
                return;
            }
            if (!state->expected) {
                state->buffer += socket->read(4 - state->buffer.size());
                if (state->buffer.size() < 4)
                    return;
                state->expected = qFromBigEndian<quint32>(state->buffer.constData());
                state->buffer.clear();
                if (!state->expected || state->expected > MaxFrameBytes) {
                    send({}, failure("LIMIT_EXCEEDED", "Invalid frame length."), true);
                    return;
                }
            }
            state->buffer += socket->read(state->expected - state->buffer.size());
            if (quint32(state->buffer.size()) < state->expected)
                return;
            const auto request = parseRequest(state->buffer);
            state->buffer.clear();
            state->expected = 0;
            timer->start(30000);
            if (!request.error.isEmpty()) {
                send(request.id, failure(request.error, "Invalid protocol request."), true);
                return;
            }
            if (socket->bytesAvailable()) {
                send(request.id, failure("BUSY", "Pipelining is unsupported.", true), true);
                return;
            }
            if (request.method == "hello") {
                const auto params = request.params;
                const auto versions = params["versions"].toArray();
                const auto client = params["client"].toObject();
                bool supported = false;
                bool valid = params.size() == 2 && params["versions"].isArray() && !versions.isEmpty() && versions.size() <= 16 &&
                             params["client"].isObject() && client.size() == 2 && client["name"].isString() &&
                             !client["name"].toString().isEmpty() && client["name"].toString().size() <= 128 &&
                             client["version"].isString() && client["version"].toString().size() <= 64;
                for (const auto& value : versions) {
                    auto candidate = value.toObject();
                    valid = valid && candidate.size() == 2 && candidate["major"].isDouble() && candidate["minor"].isDouble() &&
                            candidate["major"].toDouble() == candidate["major"].toInt(-1) &&
                            candidate["minor"].toDouble() == candidate["minor"].toInt(-1) && candidate["minor"].toInt(-1) >= 0;
                    supported = supported || (candidate["major"].toInt() == 1 && candidate["minor"].toInt(-1) >= 0);
                }
                if (!valid) {
                    send(request.id, failure("INVALID_REQUEST", "Invalid hello parameters."));
                    return;
                }
                if (!supported) {
                    send(request.id, failure("UNSUPPORTED_PROTOCOL", "No mutually supported protocol version."));
                    return;
                }
                state->negotiated = true;
                send(request.id, { { "result", QJsonObject{ { "protocol", version() },
                                                            { "installationId", m_info["installationId"] },
                                                            { "profileId", m_info["profileId"] },
                                                            { "sessionId", m_info["sessionId"] },
                                                            { "capabilities", capabilities() },
                                                            { "limits", QJsonObject{ { "frameBytes", int(MaxFrameBytes) },
                                                                                     { "clients", 8 },
                                                                                     { "pageItems", 500 } } } } } });
                return;
            }
            if (!state->negotiated) {
                send(request.id, failure("HANDSHAKE_REQUIRED", "Send hello first."));
                return;
            }
            if (!capabilities().value(request.method).toBool()) {
                send(request.id, failure("UNSUPPORTED_CAPABILITY", "This capability is not enabled."));
                return;
            }
            state->busy = true;
            auto reply = [guard = QPointer<QLocalSocket>(socket), state, send, id = request.id](QJsonObject result) {
                if (!guard)
                    return;
                QMetaObject::invokeMethod(
                    guard,
                    [guard, state, send, id, result = std::move(result)]() mutable {
                        if (guard && state->busy)
                            send(id, std::move(result));
                    },
                    Qt::QueuedConnection);
            };
            try {
                if (m_handler)
                    m_handler(request, reply);
                else
                    reply(failure("INTERNAL_ERROR", "The native coordinator is unavailable."));
            } catch (...) {
                reply(failure("INTERNAL_ERROR", "Native request failed."));
            }
        });
    }
}
}  // namespace Awake::Integration
