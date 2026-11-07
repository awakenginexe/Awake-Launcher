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
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <QScopeGuard>
#endif
#include "InstanceDiscovery.h"
#include "ProfileAccess.h"
namespace Awake::Integration {
namespace {
bool trustedPeer(QLocalSocket* socket)
{
#ifdef Q_OS_WIN
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(reinterpret_cast<HANDLE>(socket->socketDescriptor()), &pid) || !pid) return false;
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    auto closeProcess = qScopeGuard([&] { CloseHandle(process); });
    HANDLE own = nullptr, peer = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own)) return false;
    auto closeOwn = qScopeGuard([&] { CloseHandle(own); });
    if (!OpenProcessToken(process, TOKEN_QUERY, &peer)) return false;
    auto closePeer = qScopeGuard([&] { CloseHandle(peer); });
    const auto user = [](HANDLE token) { DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        QByteArray result(int(size), Qt::Uninitialized);
        if (!size || !GetTokenInformation(token, TokenUser, result.data(), size, &size)) result.clear(); return result; };
    const auto a = user(own), b = user(peer);
    return !a.isEmpty() && !b.isEmpty() && EqualSid(reinterpret_cast<const TOKEN_USER*>(a.constData())->User.Sid,
        reinterpret_cast<const TOKEN_USER*>(b.constData())->User.Sid);
#else
    Q_UNUSED(socket)
    return false;
#endif
}
}
IntegrationServer::IntegrationServer(QObject* parent) : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, &IntegrationServer::acceptClients);
}
IntegrationServer::~IntegrationServer()
{
    stop();
}
bool IntegrationServer::start(const QString& dataRoot, QJsonObject info, Handler handler, QString* reason)
{ return start(dataRoot, std::move(info), std::move(handler), Options{}, reason); }
bool IntegrationServer::start(const QString& dataRoot, QJsonObject info, Handler handler, Options options, QString* reason)
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
    for (const auto& part : QStringList{ "integration", options.directory }) {
        directory = QDir(directory).filePath(part);
        const QFileInfo before(directory);
        if (before.isSymLink() || before.isJunction())
            return reject("Integration directories must not be filesystem links.");
        if (!QDir().mkpath(directory) || !profileAccessIsPrivate(directory))
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
    const auto serverName = options.prefix + session;
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(8);
    if (!m_server.listen(serverName)) {
        m_descriptor.clear();
        return reject("Cannot listen on the integration endpoint.");
    }
    info["protocol"] = QJsonObject{{"major", options.major}, {"minor", 0}};
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
    m_capabilities = options.capabilities.isEmpty() ? capabilities() : options.capabilities;
    m_options = std::move(options);
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
        if (m_clients.size() >= 8 || !trustedPeer(socket)) {
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
            bool managerExtensions = false;
            QJsonObject capabilities;
        };
        auto state = std::make_shared<Client>();
        const auto send = [socket = QPointer<QLocalSocket>(socket), state, timer, protocol = m_info["protocol"]](const QString& id, QJsonObject body, bool close = false) {
            if (!socket)
                return;
            if (socket->state() != QLocalSocket::ConnectedState || socket->bytesToWrite() > 0) {
                socket->abort();
                return;
            }
            body["protocol"] = protocol;
            body["requestId"] = id.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(id);
            auto bytes = frame(body);
            if (bytes.isEmpty()) {
                auto bounded = failure("LIMIT_EXCEEDED", "Response exceeds the frame limit; use a smaller page.");
                bounded["protocol"] = protocol;
                bounded["requestId"] = body["requestId"];
                bytes = frame(bounded);
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
            auto request = parseRequest(state->buffer, m_options.major);
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
                const bool hasExtensions = params.contains("extensions");
                const auto extensions = params["extensions"].toArray();
                const bool optedIn = hasExtensions && !m_options.extension.isEmpty() && params["extensions"].isArray() &&
                    extensions.size() == 1 && extensions.first().isString() && extensions.first().toString() == m_options.extension;
                const bool emptyExtensions = hasExtensions && !m_options.extension.isEmpty() && params["extensions"].isArray() && extensions.isEmpty();
                bool valid = params.size() == (hasExtensions ? 3 : 2) && (!hasExtensions || optedIn || emptyExtensions) &&
                             params["versions"].isArray() && !versions.isEmpty() && versions.size() <= 16 &&
                             params["client"].isObject() && client.size() == 2 && client["name"].isString() &&
                             !client["name"].toString().isEmpty() && client["name"].toString().size() <= 128 &&
                             client["version"].isString() && client["version"].toString().size() <= 64;
                for (const auto& value : versions) {
                    auto candidate = value.toObject();
                    valid = valid && candidate.size() == 2 && candidate["major"].isDouble() && candidate["minor"].isDouble() &&
                            candidate["major"].toDouble() == candidate["major"].toInt(-1) &&
                            candidate["minor"].toDouble() == candidate["minor"].toInt(-1) && candidate["minor"].toInt(-1) >= 0;
                    supported = supported || (candidate["major"].toInt() == m_options.major && candidate["minor"].toInt(-1) >= 0);
                }
                if (!valid || (m_options.validate && !m_options.validate("hello", params))) {
                    send(request.id, failure("INVALID_REQUEST", "Invalid hello parameters."));
                    return;
                }
                if (!supported) {
                    send(request.id, failure("UNSUPPORTED_PROTOCOL", "No mutually supported protocol version."));
                    return;
                }
                state->negotiated = true;
                state->managerExtensions = optedIn;
                state->capabilities = !optedIn && !m_options.legacyCapabilities.isEmpty() ? m_options.legacyCapabilities : m_capabilities;
                if (m_options.major != 1) {
                    auto result = m_options.hello;
                    result["protocol"] = m_info["protocol"]; result["profileId"] = m_info["profileId"];
                    result["sessionId"] = m_info["sessionId"]; result["capabilities"] = state->capabilities;
                    result["limits"] = QJsonObject{{"frameBytes", int(MaxFrameBytes)}, {"clients", 8}, {"pageItems", 200}};
                    if (hasExtensions) result["extensions"] = optedIn ? QJsonArray{m_options.extension} : QJsonArray{};
                    send(request.id, {{"result", result}}); return;
                }
                send(request.id, { { "result", QJsonObject{ { "protocol", version() },
                                                            { "installationId", m_info["installationId"] },
                                                            { "profileId", m_info["profileId"] },
                                                            { "sessionId", m_info["sessionId"] },
                                                            { "capabilities", state->capabilities },
                                                            { "limits", QJsonObject{ { "frameBytes", int(MaxFrameBytes) },
                                                                                     { "clients", 8 },
                                                                                     { "pageItems", 500 } } } } } });
                return;
            }
            if (!state->negotiated) {
                send(request.id, failure("HANDSHAKE_REQUIRED", "Send hello first."));
                return;
            }
            if (!state->capabilities.value(request.method).toBool()) {
                send(request.id, failure("UNSUPPORTED_CAPABILITY", "This capability is not enabled."));
                return;
            }
            if (m_options.validate && !m_options.validate(request.method, request.params)) {
                send(request.id, failure("INVALID_REQUEST", "Parameters do not match the method schema.")); return;
            }
            state->busy = true;
            request.managerExtensions = state->managerExtensions;
            timer->stop();
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
