// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include "awake/integration/ProfileAccess.h"
#if defined(Q_OS_WIN) && !defined(Q_MOC_RUN)
#include <aclapi.h>
#include <qt_windows.h>
#endif
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include "awake/integration/InstanceDiscovery.h"
#include "awake/integration/IntegrationServer.h"
#include "awake/integration/Protocol.h"
using namespace Awake::Integration;
class AwakeIntegrationTest : public QObject {
    Q_OBJECT
    static void put(const QString& path, const QByteArray& data = "fixture")
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(data), data.size());
    }
    static QByteArray request(QJsonObject changes = {})
    {
        QJsonObject body{ { "protocol", QJsonObject{ { "major", 1 }, { "minor", 0 } } },
                          { "requestId", "r1" },
                          { "method", "instances.list" },
                          { "params", QJsonObject{} } };
        for (auto it = changes.begin(); it != changes.end(); ++it)
            body[it.key()] = it.value();
        return QJsonDocument(body).toJson(QJsonDocument::Compact);
    }
   private slots:
    void validEnvelope()
    {
        auto r = parseRequest(request());
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.id, QString("r1"));
        QCOMPARE(r.method, QString("instances.list"));
        auto bytes = frame(QJsonObject{ { "result", QJsonObject{} } });
        QCOMPARE(qFromBigEndian<quint32>(bytes.constData()), quint32(bytes.size() - 4));
    }
    void invalidEnvelope()
    {
        const QList<QByteArray> invalid{ "[]",
                                         "null",
                                         "{",
                                         request({ { "extra", true } }),
                                         request({ { "requestId", "../bad" } }),
                                         request({ { "requestId", "valid\n" } }),
                                         request({ { "method", "valid\n" } }),
                                         request({ { "params", QJsonArray{} } }),
                                         request({ { "protocol", QJsonObject{ { "major", 1 }, { "minor", 0.5 } } } }),
                                         request({ { "method", QString(65, 'a') } }),
                                         QByteArray(MaxFrameBytes + 1, ' ') };
        for (const auto& bytes : invalid)
            QVERIFY2(!parseRequest(bytes).error.isEmpty(), bytes.left(150).constData());
        QCOMPARE(parseRequest(request({ { "protocol", QJsonObject{ { "major", 2 }, { "minor", 0 } } } })).error,
                 QString("UNSUPPORTED_PROTOCOL"));
    }
    void capabilitiesNeverAdvertiseWrites()
    {
        QVERIFY(capabilities().value("instances.list").toBool());
        QVERIFY(capabilities().value("mods.list").toBool());
        for (const auto* name : { "transactions.apply", "launch.request", "instances.create", "recovery.run" })
            QVERIFY(!capabilities().value(name).toBool());
    }
    void physicalIdentityAndCollisions()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString a = tmp.path() + "/a/same", b = tmp.path() + "/b/same";
        put(a + "/instance.cfg");
        put(b + "/instance.cfg");
        QVERIFY(!physicalIdentity(a).isEmpty());
        QCOMPARE(physicalIdentity(a), physicalIdentity(a + "/../same"));
        QVERIFY(physicalIdentity(a) != physicalIdentity(b));
        QVERIFY(physicalIdentity(tmp.path() + "/missing").isEmpty());
        QList<InstanceSeed> loaded{ { "same", "duplicate-uuid", "One", a, a + "/minecraft/mods" },
                                    { "same", "duplicate-uuid", "Two", b, b + "/minecraft/mods" } };
        auto result = discover({ tmp.path() + "/a", tmp.path() + "/b", tmp.path() + "/a/." }, loaded);
        QCOMPARE(result["instances"].toArray().size(), 2);
        QVERIFY(result["warnings"].toArray().size() >= 2);
        for (const auto& value : result["instances"].toArray())
            QCOMPARE(value.toObject()["identityStatus"].toString(), QString("conflict"));
    }
    void unloadedDuplicatesAreVisible()
    {
        QTemporaryDir tmp;
        const QString a = tmp.path() + "/a/same", b = tmp.path() + "/b/same";
        put(a + "/instance.cfg");
        put(b + "/instance.cfg");
        auto result = discover({ tmp.path() + "/a", tmp.path() + "/b" }, { { "same", "u1", "Native", a, a + "/minecraft/mods" } });
        auto instances = result["instances"].toArray();
        QCOMPARE(instances.size(), 2);
        int loadedCount = 0;
        for (const auto& item : instances)
            loadedCount += item.toObject()["loaded"].toBool() ? 1 : 0;
        QCOMPARE(loadedCount, 1);
    }
    void largeInventoryAndStalePage()
    {
        QTemporaryDir tmp;
        const auto root = tmp.path() + "/instance";
        put(root + "/instance.cfg", "secret-account-setting");
        put(root + "/mmc-pack.json", "{}");
        const auto mods = root + "/minecraft/mods";
        for (int i = 0; i < 305; ++i)
            put(mods + QString("/mod-%1.jar%2").arg(i, 3, 10, QChar('0')).arg(i == 304 ? ".disabled" : ""));
        put(mods + "/.index/mod.pw.toml", "metadata");
        InstanceSeed seed{ "fixture", "uuid", "Fixture", root, mods };
        auto first = inventory(seed, 0, 200);
        QVERIFY(!first.contains("error"));
        auto page = first["result"].toObject();
        QCOMPARE(page["total"].toInt(), 305);
        QCOMPARE(page["items"].toArray().size(), 200);
        const auto revision = page["instanceRevision"].toString();
        QVERIFY(!revision.isEmpty());
        auto second = inventory(seed, 200, 200, revision)["result"].toObject();
        QCOMPARE(second["items"].toArray().size(), 105);
        QVERIFY(second["nextOffset"].isNull());
        QVERIFY(!second["items"].toArray().last().toObject()["enabled"].toBool());
        QVERIFY(!QJsonDocument(first).toJson().contains("secret-account-setting"));
        put(mods + "/.index/mod.pw.toml", "new-metadata");
        QCOMPARE(inventory(seed, 200, 200, revision)["error"].toObject()["code"].toString(), QString("STALE_INSTANCE"));
    }
    void aliasesDoNotDuplicatePhysicalInstance()
    {
        QTemporaryDir tmp;
        const auto path = tmp.path() + "/instances/one";
        put(path + "/instance.cfg");
        auto result = discover({ tmp.path() + "/instances", tmp.path() + "/instances/." },
                               { { "one", "u1", "One", path, path + "/minecraft/mods" } });
        QCOMPARE(result["instances"].toArray().size(), 1);
        QVERIFY(!result["warnings"].toArray().isEmpty());
    }

    static QJsonObject exchange(QLocalSocket& socket, const QJsonObject& body)
    {
        socket.write(frame(body));
        QByteArray bytes;
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 3000) {
            QCoreApplication::processEvents();
            bytes += socket.readAll();
            if (bytes.size() >= 4 && quint32(bytes.size() - 4) >= qFromBigEndian<quint32>(bytes.constData()))
                return QJsonDocument::fromJson(bytes.mid(4)).object();
            QTest::qWait(1);
        }
        return {};
    }
    static QJsonObject body(const QString& method, QJsonObject params = {})
    {
        return { { "protocol", version() }, { "requestId", "r1" }, { "method", method }, { "params", params } };
    }
    static QJsonObject hello()
    {
        return body("hello",
                    { { "versions", QJsonArray{ version() } }, { "client", QJsonObject{ { "name", "fixture" }, { "version", "dev" } } } });
    }
    void transportNegotiationReconnectAndRefusal()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows-only IPC endpoint.");
#endif
        QTemporaryDir tmp;
        IntegrationServer server;
        QString error;
        const auto handler = [](const Request& r, IntegrationServer::Reply done) {
            if (r.method == "launcher.info")
                done({ { "result", QJsonObject{ { "fixture", true } } } });
            else
                done(failure("UNSUPPORTED_CAPABILITY", "This capability is not enabled."));
        };
        QVERIFY2(server.start(tmp.path(), { { "installationId", "fixture-install" }, { "profileId", "fixture-profile" } }, handler, &error),
                 qPrintable(error));
        QVERIFY(QFile::exists(server.descriptorPath()));
        const auto descriptor = server.descriptorPath();
        IntegrationServer duplicate;
        QVERIFY(!duplicate.start(tmp.path(), {}, handler, &error));
        QVERIFY(QFile::exists(descriptor));
        QLocalSocket socket;
        socket.connectToServer(server.serverName());
        QVERIFY(socket.waitForConnected());
        QCOMPARE(exchange(socket, body("launcher.info"))["error"].toObject()["code"].toString(), QString("HANDSHAKE_REQUIRED"));
        auto reply = exchange(socket, hello())["result"].toObject();
        QCOMPARE(reply["installationId"].toString(), QString("fixture-install"));
        QVERIFY(!reply["capabilities"].toObject()["transactions.apply"].toBool());
        for (int i = 0; i < 2; ++i)
            QVERIFY(exchange(socket, body("launcher.info"))["result"].toObject()["fixture"].toBool());
        QCOMPARE(exchange(socket, body("transactions.apply"))["error"].toObject()["code"].toString(), QString("UNSUPPORTED_CAPABILITY"));
        socket.abort();
        QCoreApplication::processEvents();
        QLocalSocket reconnect;
        reconnect.connectToServer(server.serverName());
        QVERIFY(reconnect.waitForConnected());
        QCOMPARE(exchange(reconnect, hello())["result"].toObject()["sessionId"], reply["sessionId"]);
        server.stop();
        QVERIFY(!QFile::exists(descriptor));
    }
    void invalidFramesAndNegotiation()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows-only IPC endpoint.");
#endif
        QTemporaryDir tmp;
        IntegrationServer server;
        QVERIFY(server.start(tmp.path(), {}, [](const Request&, IntegrationServer::Reply done) { done({ { "result", QJsonObject{} } }); }));
        QLocalSocket socket;
        socket.connectToServer(server.serverName());
        QVERIFY(socket.waitForConnected());
        auto unsupported = hello();
        auto params = unsupported["params"].toObject();
        params["versions"] = QJsonArray{ QJsonObject{ { "major", 99 }, { "minor", 0 } } };
        unsupported["params"] = params;
        QCOMPARE(exchange(socket, unsupported)["error"].toObject()["code"].toString(), QString("UNSUPPORTED_PROTOCOL"));
        QVERIFY(exchange(socket, hello()).contains("result"));
        QByteArray header(4, Qt::Uninitialized);
        qToBigEndian<quint32>(MaxFrameBytes + 1, header.data());
        socket.write(header);
        QTRY_VERIFY_WITH_TIMEOUT(socket.state() == QLocalSocket::UnconnectedState, 3000);
    }
    void fragmentedFrames()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows-only IPC endpoint.");
#endif
        QTemporaryDir tmp;
        IntegrationServer server;
        QVERIFY(server.start(tmp.path(), {}, [](const Request&, IntegrationServer::Reply done) { done({ { "result", QJsonObject{} } }); }));
        QLocalSocket socket;
        socket.connectToServer(server.serverName());
        QVERIFY(socket.waitForConnected());
        const auto bytes = frame(hello());
        for (char byte : bytes) {
            socket.write(QByteArray(1, byte));
            QCoreApplication::processEvents();
        }
        QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 4, 3000);
        auto result = socket.readAll();
        QVERIFY(QJsonDocument::fromJson(result.mid(4)).object().contains("result"));
    }

    void rejectsInvalidUtf8()
    {
        auto bytes = request({ { "params", QJsonObject{ { "label", "value" } } } });
        bytes.replace("value", QByteArray::fromHex("ff"));
        QCOMPARE(parseRequest(bytes).error, QString("INVALID_REQUEST"));
    }
    void unsafeEndpointDirectory()
    {
        QTemporaryDir tmp;
        put(tmp.path() + "/integration", "leave-this-file");
        IntegrationServer server;
        QString reason;
        QVERIFY(!server.start(tmp.path(), {}, {}, &reason));
        QVERIFY(!reason.isEmpty());
        QFile file(tmp.path() + "/integration");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("leave-this-file"));
    }
    void realDirectoryAlias()
    {
        QTemporaryDir tmp;
        const auto root = tmp.path() + "/instances";
        const auto target = root + "/one";
        put(target + "/instance.cfg");
        const auto link = root + "/alias";
#ifdef Q_OS_WIN
        const auto targetNative = QDir::toNativeSeparators(target);
        const auto linkNative = QDir::toNativeSeparators(link);
        if (!CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(linkNative.utf16()), reinterpret_cast<LPCWSTR>(targetNative.utf16()),
                                 SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
            QSKIP("Creating a directory symlink requires Developer Mode or privilege.");
#else
        QVERIFY(QFile::link(target, link));
#endif
        auto cleanup = qScopeGuard([&] { QDir().rmdir(link); });
        QCOMPARE(physicalIdentity(link), physicalIdentity(target));
        QCOMPARE(discover({ root }, {})["instances"].toArray().size(), 1);
        QVERIFY(!discover({ root }, {})["warnings"].toArray().isEmpty());
    }
#ifdef Q_OS_WIN
    void rejectsPublicWriteProfile()
    {
        QTemporaryDir tmp;
        QVERIFY(profileAccessIsPrivate(tmp.path()));
        const auto path = QDir::toNativeSeparators(tmp.path());
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        PACL oldAcl = nullptr, newAcl = nullptr;
        QCOMPARE(GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(path.utf16())), SE_FILE_OBJECT,
                                       DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl, nullptr, &descriptor),
                 DWORD(ERROR_SUCCESS));
        auto freeDescriptor = qScopeGuard([&] { LocalFree(descriptor); });
        BYTE everyone[SECURITY_MAX_SID_SIZE];
        DWORD size = sizeof(everyone);
        QVERIFY(CreateWellKnownSid(WinWorldSid, nullptr, everyone, &size));
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_ADD_FILE;
        access.grfAccessMode = GRANT_ACCESS;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(everyone);
        QCOMPARE(SetEntriesInAclW(1, &access, oldAcl, &newAcl), DWORD(ERROR_SUCCESS));
        auto freeAcl = qScopeGuard([&] { LocalFree(newAcl); });
        QCOMPARE(SetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(path.utf16())), SE_FILE_OBJECT,
                                       DACL_SECURITY_INFORMATION, nullptr, nullptr, newAcl, nullptr),
                 DWORD(ERROR_SUCCESS));
        QVERIFY(!profileAccessIsPrivate(tmp.path()));
        IntegrationServer server;
        QVERIFY(!server.start(tmp.path(), {}, {}));
        QVERIFY(!QFile::exists(tmp.path() + "/integration/v1/endpoint.json"));
    }
#endif

    void sharedGameDirectoriesAreConflicts()
    {
        QTemporaryDir tmp;
        const auto a = tmp.path() + "/a/one", b = tmp.path() + "/b/two";
        const auto shared = tmp.path() + "/shared/mods";
        put(a + "/instance.cfg");
        put(b + "/instance.cfg");
        QVERIFY(QDir().mkpath(shared));
        auto found =
            discover({ tmp.path() + "/a", tmp.path() + "/b" }, { { "one", "u1", "One", a, shared }, { "two", "u2", "Two", b, shared } });
        QCOMPARE(found["instances"].toArray().size(), 2);
        for (const auto& entry : found["instances"].toArray())
            QCOMPARE(entry.toObject()["identityStatus"].toString(), QString("conflict"));
    }

#ifdef Q_OS_WIN
    void immediateSequentialReadsAcrossProcesses()
    {
        QTemporaryDir tmp;
        IntegrationServer server;
        QVERIFY(server.start(tmp.path(), {}, [](const Request&, IntegrationServer::Reply reply) {
            reply({ { "result", QJsonObject{ { "payload", QString(65536, 'x') } } } });
        }));
        QProcess client;
        client.start(QCoreApplication::applicationFilePath(), { "--sequential-client", server.serverName() });
        auto stop = qScopeGuard([&] {
            if (client.state() != QProcess::NotRunning) {
                client.kill();
                client.waitForFinished(5000);
            }
        });
        QVERIFY(client.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(client.state() == QProcess::NotRunning, 30000);
        QCOMPARE(client.exitStatus(), QProcess::NormalExit);
        QVERIFY2(client.exitCode() == 0, client.readAllStandardError().constData());
    }

    void coordinatorOwnershipAcrossProcesses()
    {
        QTemporaryDir tmp;
        IntegrationServer server;
        QVERIFY(server.start(tmp.path(), {}, {}));
        const auto descriptor = server.descriptorPath();
        const auto probe = [&] {
            QProcess process;
            process.start(QCoreApplication::applicationFilePath(), { "--probe-profile", tmp.path() });
            if (!process.waitForFinished(5000))
                return -1;
            return process.exitCode();
        };
        QCOMPARE(probe(), 23);
        QVERIFY(QFile::exists(descriptor));
        server.stop();
        QCOMPARE(probe(), 0);
    }
    void directoryJunctionIdentity()
    {
        QTemporaryDir tmp;
        const auto root = tmp.path() + "/instances";
        const auto target = root + "/one", link = root + "/junction";
        put(target + "/instance.cfg");
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("AWAKE_TEST_JUNCTION", QDir::toNativeSeparators(link));
        environment.insert("AWAKE_TEST_TARGET", QDir::toNativeSeparators(target));
        process.setProcessEnvironment(environment);
        process.start(QDir(qEnvironmentVariable("WINDIR")).filePath("System32/WindowsPowerShell/v1.0/powershell.exe"),
                      { "-NoProfile", "-NonInteractive", "-Command",
                        "New-Item -ItemType Junction -Path $env:AWAKE_TEST_JUNCTION -Target $env:AWAKE_TEST_TARGET | Out-Null" });
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitCode(), 0);
        QVERIFY(QFileInfo(link).isJunction());
        auto cleanup = qScopeGuard([&] { QDir().rmdir(link); });
        QCOMPARE(physicalIdentity(link), physicalIdentity(target));
        QCOMPARE(discover({ root }, {})["instances"].toArray().size(), 1);
    }
#endif

    void removedInstanceMetadataIsStale()
    {
        QTemporaryDir tmp;
        const auto root = tmp.path() + "/instance";
        put(root + "/instance.cfg");
        InstanceSeed seed{ "one", "u", "One", root, root + "/minecraft/mods" };
        QFile::remove(root + "/instance.cfg");
        QCOMPARE(inventory(seed, 0, 200)["error"].toObject()["code"].toString(), QString("STALE_INSTANCE"));
    }
};
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--probe-profile") {
        IntegrationServer server;
        return server.start(args[2], {}, {}) ? 0 : 23;
    }
#ifdef Q_OS_WIN
    if (args.size() == 3 && args[1] == "--sequential-client") {
        const auto name = QString("\\\\.\\pipe\\") + args[2];
        const auto pipe = CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) return 30;
        auto close = qScopeGuard([&] { CloseHandle(pipe); });
        const auto read = [pipe](quint32 size) {
            QByteArray bytes(size, Qt::Uninitialized);
            DWORD count = 0;
            quint32 offset = 0;
            while (offset < size) {
                if (!ReadFile(pipe, bytes.data() + offset, size - offset, &count, nullptr) || !count) return QByteArray{};
                offset += count;
            }
            return bytes;
        };
        for (int i = 0; i < 501; ++i) {
            QJsonObject body{ { "protocol", version() }, { "requestId", "serial" },
                              { "method", i ? "launcher.info" : "hello" }, { "params", QJsonObject{} } };
            if (!i) body["params"] = QJsonObject{ { "versions", QJsonArray{ version() } },
                { "client", QJsonObject{ { "name", "fixture" }, { "version", "1" } } } };
            const auto bytes = frame(body);
            DWORD count = 0;
            if (!WriteFile(pipe, bytes.constData(), DWORD(bytes.size()), &count, nullptr) || count != DWORD(bytes.size())) return 31;
            const auto header = read(4);
            if (header.size() != 4) {
                qWarning() << "Disconnected after completed response" << i << GetLastError();
                return 32;
            }
            const auto size = qFromBigEndian<quint32>(header.constData());
            if (!size || size > MaxFrameBytes) return 33;
            const auto response = QJsonDocument::fromJson(read(size)).object();
            if (!response.contains("result") || response["requestId"] != "serial") return 34;
        }
        return 0;
    }
#endif
    AwakeIntegrationTest test;
    auto testArgs = app.arguments();
    if (!testArgs.contains("-o"))
        testArgs << "-o" << "AwakeIntegration-results.txt,txt";
    return QTest::qExec(&test, testArgs);
}
#include "AwakeIntegration_test.moc"
