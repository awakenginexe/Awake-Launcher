// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>
#include "NativeTestAccounts.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QMessageBox>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTcpSocket>
#include <QDesktopServices>
#include <QSettings>
#include <QScopeGuard>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif
#include "minecraft/auth/AuthFlow.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/auth/OAuthCallback.h"
#include "minecraft/auth/steps/MSADeviceCodeStep.h"
#include "minecraft/auth/steps/LauncherLoginStep.h"
#include "minecraft/auth/steps/XboxAuthorizationStep.h"
#include "Application.h"
#include "BuildConfig.h"
#include "settings/SettingsObject.h"
#include "minecraft/auth/steps/MSAStep.h"
#include "ui/MainWindow.h"

class FakeReply : public QNetworkReply {
    QByteArray m_body;
    qint64 m_position = 0;
public:
    FakeReply(const QNetworkRequest& request, QByteArray body, int status, QObject* parent, bool errorFirst) : QNetworkReply(parent), m_body(body)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if (status == 403) setError(ContentAccessDenied, "HTTP 403");
        if (status == 401) setError(AuthenticationRequiredError, "HTTP 401");
        if (status == 400) setError(ContentOperationNotPermittedError, "HTTP 400");
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this, errorFirst] {
            setFinished(true);
            if (errorFirst && error() != NoError) emit errorOccurred(error());
            emit readyRead();
            if (!errorFirst && error() != NoError) emit errorOccurred(error());
            emit finished();
        });
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return m_body.size() - m_position + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char* destination, qint64 length) override
    {
        const auto count = qMin(length, m_body.size() - m_position);
        if (!count) return -1;
        memcpy(destination, m_body.constData() + m_position, count);
        m_position += count;
        return count;
    }
};

class AuthNetwork : public QNetworkAccessManager {
public:
    ~AuthNetwork() override
    {
        // Prism task pointers defer destruction; release their owned replies before this manager's children.
        for (int pass = 0; pass < 8; ++pass) QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    struct Call { QUrl url; QByteArray body; };
    QList<Call> calls;
    int minecraftStatus = 200;
    int tokenStatus = 200;
    bool errorFirst = false;
    bool emptyMinecraftError = false;
    qint64 xstsError = 0;
    QString deviceError;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice* outgoing) override
    {
        calls.append({request.url(), outgoing ? outgoing->readAll() : QByteArray()});
        const auto host = request.url().host();
        const auto path = request.url().path();
        QByteArray body;
        int status = 200;
        if (path.endsWith("/devicecode")) {
            body = R"({"device_code":"TEST-DEVICE-CREDENTIAL","user_code":"TEST-CODE","verification_uri":"https://www.microsoft.com/link","expires_in":600,"interval":1})";
        } else if (path.endsWith("/token")) {
            status = tokenStatus;
            if (!deviceError.isEmpty()) {
                status = 400;
                body = QJsonDocument(QJsonObject{{"error",deviceError}}).toJson();
            } else if (status != 200) {
                body = R"({"error":"invalid_grant","error_description":"TEST-REFRESH-CREDENTIAL","access_token":"TEST-MSA-CREDENTIAL"})";
            } else body = R"({"access_token":"TEST-MSA-CREDENTIAL","refresh_token":"TEST-REFRESH-CREDENTIAL","token_type":"Bearer","expires_in":3600})";
        } else if (host == "user.auth.xboxlive.com" || host == "xsts.auth.xboxlive.com") {
            if (host.startsWith("xsts") && xstsError) {
                status = 401;
                body = QJsonDocument(QJsonObject{{"XErr",xstsError},{"Token","TEST-XBOX-CREDENTIAL"}}).toJson();
            } else body = R"({"IssueInstant":"2026-10-03T00:00:00Z","NotAfter":"2026-10-04T00:00:00Z","Token":"TEST-XBOX-CREDENTIAL","DisplayClaims":{"xui":[{"uhs":"1234"}]}})";
        } else if (path == "/launcher/login") {
            status = minecraftStatus;
            body = status == 200 ? R"({"username":"1234","access_token":"TEST-MINECRAFT-CREDENTIAL","expires_in":86400})" :
                R"({"error":"ForbiddenOperationException","errorMessage":"Invalid app registration","access_token":"TEST-MINECRAFT-CREDENTIAL"})";
            if (status != 200 && emptyMinecraftError) body.clear();
        } else if (path.startsWith("/entitlements/")) body = R"({"items":[{"name":"game_minecraft"},{"name":"product_minecraft"}]})";
        else if (path == "/minecraft/profile") body = R"({"id":"0123456789abcdef0123456789abcdef","name":"TestPlayer","skins":[{"id":"skin","state":"ACTIVE","url":"https://textures.minecraft.net/texture/test","variant":"CLASSIC"}],"capes":[]})";
        else body = "test-skin";
        return new FakeReply(request, body, status, this, errorFirst);
    }
};

static QStringList* capturedMessages = nullptr;
static void captureMessage(QtMsgType, const QMessageLogContext&, const QString& text)
{
    if (capturedMessages) capturedMessages->append(text);
}
class AuthLogCapture {
    QtMessageHandler previous;
public:
    QStringList messages;
    AuthLogCapture() { capturedMessages = &messages; previous = qInstallMessageHandler(captureMessage); }
    ~AuthLogCapture() { qInstallMessageHandler(previous); capturedMessages = nullptr; }
    bool containsCredentials() const { return messages.join('\n').contains("CREDENTIAL"); }
};

static void sendLoopbackCallback(const QUrl& redirect, const QUrlQuery& query, const QByteArray& path = "/")
{
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", redirect.port());
    if (!socket.waitForConnected(2000)) return;
    socket.write("GET " + path + "?" + query.query(QUrl::FullyEncoded).toUtf8() + " HTTP/1.1\r\nHost: localhost\r\n\r\n");
    socket.flush();
    QTest::qWait(50);
}

class MicrosoftAuthTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        while (APPLICATION->accounts()->count()) APPLICATION->accounts()->removeAccount(APPLICATION->accounts()->index(0,0));
        QCOMPARE(APPLICATION->accounts()->count(), 0);
    }
    void awakeIdentity()
    {
        QCOMPARE(BuildConfig.MSA_CLIENT_ID, QString("9f3c5cb3-82af-4a3e-ad36-2970397c2395"));
        APPLICATION->settings()->reset("MSAClientIDOverride");
        QCOMPARE(APPLICATION->getMSAClientID(), BuildConfig.MSA_CLIENT_ID);
        APPLICATION->settings()->set("MSAClientIDOverride", "developer-override");
        QCOMPARE(APPLICATION->getMSAClientID(), QString("developer-override"));
        APPLICATION->settings()->reset("MSAClientIDOverride");
    }
    void malformedCallbacks()
    {
        AuthLogCapture logs;
        MainWindow window;
        QSignalSpy callbacks(APPLICATION, &Application::oauthReplyRecieved);
        window.processURLs({QUrl("awakelauncher://oauth/other?code=TEST-CODE-CREDENTIAL&state=test-state"),
                            QUrl("awakelauncher://oauth/microsoft?code=one&code=two&state=test-state"),
                            QUrl("awakelauncher://oauth/microsoft?code=TEST-CODE-CREDENTIAL"),
                            QUrl("awakelauncher://user@oauth/microsoft?code=TEST-CODE-CREDENTIAL&state=test-state"),
                            QUrl("awakelauncher://install?code=TEST-CODE-CREDENTIAL&state=test-state")});
        QCOMPARE(callbacks.count(), 0);
        QVERIFY(!logs.containsCredentials());
    }
    void loopbackAuthorization()
    {
        AccountData data;
        MSAStep step(&data);
        QSignalSpy browser(&step, &MSAStep::authorizeWithBrowser);
        step.perform();
        QCOMPARE(browser.count(), 1);
        QUrlQuery query(browser.first().first().toUrl());
        QCOMPARE(query.queryItemValue("client_id"), APPLICATION->getMSAClientID());
        QCOMPARE(query.queryItemValue("code_challenge_method"), QString("S256"));
        const QUrl redirect(query.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        QCOMPARE(redirect.host(), QString("localhost"));
        QVERIFY(redirect.port() > 0);
        QCOMPARE(redirect.path(), QString("/"));
        QCOMPARE(query.queryItemValue("scope", QUrl::FullyDecoded), QString("XboxLive.SignIn XboxLive.offline_access"));
        QVERIFY(!query.hasQueryItem("client_secret"));
        step.abort();
    }
    void callbackStateCancellationAndExpiry()
    {
        AuthLogCapture logs;
        AuthNetwork network;
        AccountData data;
        MSAStep step(&data);
        step.setNetwork(&network);
        QSignalSpy browser(&step, &MSAStep::authorizeWithBrowser);
        QSignalSpy finished(&step, &AuthStep::finished);
        step.perform();
        QUrlQuery query(browser.first().first().toUrl());
        const auto state = query.queryItemValue("state");
        const QUrl redirect(query.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        sendLoopbackCallback(redirect, QUrlQuery("state=wrong-state&code=TEST-CODE-CREDENTIAL"), "/wrong");
        sendLoopbackCallback(redirect, QUrlQuery("state=" + state + "&code=one&code=two"));
        sendLoopbackCallback(redirect, QUrlQuery("state=wrong-state&code=TEST-CODE-CREDENTIAL"));
        sendLoopbackCallback(redirect, QUrlQuery("state=wrong-state&error=access_denied"));
        QCOMPARE(finished.count(), 0);
        QVERIFY(network.calls.isEmpty());
        QUrlQuery canceledQuery;
        canceledQuery.addQueryItem("state", state);
        canceledQuery.addQueryItem("error", "access_denied");
        canceledQuery.addQueryItem("error_description", "TEST-CODE-CREDENTIAL");
        sendLoopbackCallback(redirect, canceledQuery);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(network.calls.isEmpty());
        QVERIFY(!finished.first()[1].toString().contains("CREDENTIAL"));

        MSAStep expired(&data);
        QSignalSpy expiredBrowser(&expired, &MSAStep::authorizeWithBrowser);
        QSignalSpy expiry(&expired, &AuthStep::finished);
        expired.perform();
        auto* timer = expired.findChild<QTimer*>("microsoftAuthorizationTimeout");
        QVERIFY(timer);
        timer->start(1);
        QTRY_COMPARE(expiry.count(), 1);
        const auto oldState = QUrlQuery(expiredBrowser.first().first().toUrl()).queryItemValue("state");
        emit APPLICATION->oauthReplyRecieved({{"state",oldState},{"code","TEST-CODE-CREDENTIAL"}});
        QCOMPARE(expiry.count(), 1);

        MSAStep canceled(&data);
        data.msaClientID = "previous-public-client";
        data.msaToken.refresh_token = "TEST-REFRESH-CREDENTIAL";
        QSignalSpy canceledBrowser(&canceled, &MSAStep::authorizeWithBrowser);
        QSignalSpy canceledFinished(&canceled, &AuthStep::finished);
        canceled.perform();
        canceled.abort();
        const auto canceledState = QUrlQuery(canceledBrowser.first().first().toUrl()).queryItemValue("state");
        emit APPLICATION->oauthReplyRecieved({{"state",canceledState},{"code","TEST-CODE-CREDENTIAL"}});
        QCOMPARE(canceledFinished.count(), 0);
        QCOMPARE(data.msaClientID, QString("previous-public-client"));
        QVERIFY(!data.msaToken.refresh_token.isEmpty());
        QVERIFY(!logs.containsCredentials());
    }
    void windowsCustomProtocolAndSecondInstance()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows protocol dispatch integration");
#else
        const auto scheme = BuildConfig.LAUNCHER_APP_BINARY_NAME;
        QSettings choice("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\" + scheme + "\\UserChoice",
                         QSettings::NativeFormat);
        if (choice.value("ProgId",scheme).toString() != scheme) QSKIP("A user-selected protocol application takes precedence");
        QSettings registry("HKEY_CURRENT_USER\\Software\\Classes\\" + scheme, QSettings::NativeFormat);
        QVariantMap original;
        for (const auto& key : registry.allKeys()) original.insert(key, registry.value(key));
        const QByteArray environmentName = (BuildConfig.LAUNCHER_NAME.toUpper() + "_DATA_DIR").toUtf8();
        const auto oldData = qgetenv(environmentName.constData());
        const auto oldHelper = qgetenv("AWAKE_AUTH_CALLBACK_HELPER");
        const auto restore = qScopeGuard([&] {
            registry.remove("");
            for (auto it = original.begin(); it != original.end(); ++it) registry.setValue(it.key(),it.value());
            registry.sync();
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            if (oldData.isNull()) qunsetenv(environmentName.constData()); else qputenv(environmentName.constData(),oldData);
            if (oldHelper.isNull()) qunsetenv("AWAKE_AUTH_CALLBACK_HELPER"); else qputenv("AWAKE_AUTH_CALLBACK_HELPER",oldHelper);
        });
        registry.setValue(".", "URL:Awake Launcher Test Protocol");
        registry.setValue("URL Protocol", "");
        registry.setValue("shell/open/command/.", QString("\"%1\" -d \"%2\" \"%3\"")
                          .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),APPLICATION->dataRoot(),"%1"));
        registry.sync();
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        qputenv(environmentName.constData(), APPLICATION->dataRoot().toUtf8());
        qputenv("AWAKE_AUTH_CALLBACK_HELPER", "1");
        AuthLogCapture logs;
        AuthNetwork network;
        AccountData data;
        MSAStep step(&data);
        step.setNetwork(&network);
        QSignalSpy browser(&step, &MSAStep::authorizeWithBrowser);
        QSignalSpy finished(&step, &AuthStep::finished);
        step.perform();
        const QUrlQuery query(browser.first().first().toUrl());
        const QUrl redirect(query.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        QCOMPARE(redirect.toString(), QString("awakelauncher://oauth/microsoft"));
        QUrl callback(redirect);
        QUrlQuery response;
        response.addQueryItem("state",query.queryItemValue("state"));
        response.addQueryItem("code","TEST-CODE-CREDENTIAL");
        callback.setQuery(response);
        // The offscreen Qt platform has no desktop-services plugin. Exercise Windows' real URI dispatcher.
        const auto dispatch = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(callback.toString().utf16()), nullptr, nullptr, SW_HIDE));
        QVERIFY2(dispatch > 32, qPrintable(QString("Windows protocol dispatch error %1").arg(dispatch)));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QCOMPARE(network.calls.count(), 1);
        QVERIFY(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(callback.toString().utf16()), nullptr, nullptr, SW_HIDE)) > 32);
        QTest::qWait(1000);
        QCOMPARE(network.calls.count(), 1);
        QVERIFY(!logs.containsCredentials());

        QFile portableMarker(APPLICATION->root() + "/portable.txt");
        QVERIFY(portableMarker.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        portableMarker.close();
        const auto removeMarker = qScopeGuard([&] { portableMarker.remove(); });
        MSAStep portable(&data);
        QSignalSpy portableBrowser(&portable, &MSAStep::authorizeWithBrowser);
        portable.perform();
        QCOMPARE(portableBrowser.count(), 1);
        const QUrl portableRedirect(QUrlQuery(portableBrowser.first().first().toUrl()).queryItemValue("redirect_uri", QUrl::FullyDecoded));
        QCOMPARE(portableRedirect.scheme(), QString("http"));
        QCOMPARE(portableRedirect.host(), QString("localhost"));
        QVERIFY(portableRedirect.port() > 0);
        portable.abort();
#endif
    }
    void loopbackTokenExchange()
    {
        AuthNetwork network;
        AccountData data;
        MSAStep step(&data);
        step.setNetwork(&network);
        QSignalSpy browser(&step, &MSAStep::authorizeWithBrowser);
        QSignalSpy finished(&step, &AuthStep::finished);
        step.perform();
        const QUrlQuery query(browser.first().first().toUrl());
        const QUrl redirect(query.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        QTcpSocket socket;
        socket.connectToHost("127.0.0.1", redirect.port());
        QVERIFY(socket.waitForConnected());
        QUrlQuery response;
        response.addQueryItem("state", query.queryItemValue("state"));
        response.addQueryItem("code", "TEST-CODE-CREDENTIAL");
        socket.write("GET /?" + response.query(QUrl::FullyEncoded).toUtf8() + " HTTP/1.1\r\nHost: localhost\r\n\r\n");
        socket.flush();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(network.calls.count(), 1);
        const QUrlQuery tokenRequest(QString::fromUtf8(network.calls.first().body));
        QVERIFY(tokenRequest.hasQueryItem("code_verifier"));
        QVERIFY(!tokenRequest.hasQueryItem("client_secret"));
        QCOMPARE(tokenRequest.queryItemValue("redirect_uri", QUrl::FullyDecoded), redirect.toString());
        QCOMPARE(data.msaClientID, APPLICATION->getMSAClientID());
        QVERIFY(!data.msaToken.refresh_token.isEmpty());
    }
    void refreshRestartAndFullPipeline()
    {
        AuthLogCapture logs;
        AuthNetwork network;
        AccountData old;
        old.msaClientID = APPLICATION->getMSAClientID();
        old.msaToken.refresh_token = "TEST-REFRESH-CREDENTIAL";
        AccountData data;
        QVERIFY(data.resumeStateFromV3(old.saveState()));
        AuthFlow flow(&data, AuthFlow::Action::Refresh, &network);
        QSignalSpy finished(&flow, &Task::finished);
        flow.start();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(flow.getState(), Task::State::Succeeded);
        QCOMPARE(data.accountState, AccountState::Online);
        QCOMPARE(data.minecraftProfile.name, QString("TestPlayer"));
        QVERIFY(data.minecraftEntitlement.ownsMinecraft);
        QCOMPARE(network.calls.size(), 7);
        const auto payload = QUrlQuery(QString::fromUtf8(network.calls.first().body));
        QCOMPARE(payload.queryItemValue("grant_type"), QString("refresh_token"));
        QVERIFY(!payload.hasQueryItem("client_secret"));
        QVERIFY(!logs.containsCredentials());
        for (const auto& stage : {"Microsoft token exchange succeeded", "Xbox Live authentication succeeded", "XSTS authorization succeeded",
                                  "Minecraft Services authentication succeeded", "Minecraft profile retrieved"})
            QVERIFY(logs.messages.join('\n').contains(stage));
    }
    void changedClientPreservesAccount()
    {
        AuthNetwork network;
        AccountData data;
        data.msaClientID = "previous-public-client";
        data.msaToken.refresh_token = "TEST-REFRESH-CREDENTIAL";
        data.minecraftProfile.name = "ExistingPlayer";
        AuthFlow flow(&data, AuthFlow::Action::Refresh, &network);
        flow.start();
        QCOMPARE(flow.taskState(), AccountTaskState::STATE_DISABLED);
        QCOMPARE(data.accountState, AccountState::Disabled);
        QVERIFY(network.calls.isEmpty());
        QCOMPARE(data.minecraftProfile.name, QString("ExistingPlayer"));
        QVERIFY(!data.msaToken.refresh_token.isEmpty());
    }
    void invalidAppRegistration_data()
    {
        QTest::addColumn<bool>("errorFirst");
        QTest::addColumn<bool>("emptyError");
        QTest::newRow("body-before-error") << false << false;
        QTest::newRow("error-before-body") << true << false;
        QTest::newRow("empty-403-body") << true << true;
    }
    void invalidAppRegistration()
    {
        QFETCH(bool, errorFirst);
        QFETCH(bool, emptyError);
        AuthLogCapture logs;
        AuthNetwork network;
        network.minecraftStatus = 403;
        network.errorFirst = errorFirst;
        network.emptyMinecraftError = emptyError;
        AccountData data;
        data.msaClientID = APPLICATION->getMSAClientID();
        data.msaToken.refresh_token = "TEST-REFRESH-CREDENTIAL";
        AuthFlow flow(&data, AuthFlow::Action::Refresh, &network);
        QSignalSpy finished(&flow, &Task::finished);
        flow.start();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(flow.taskState(), AccountTaskState::STATE_FAILED_SOFT);
        QVERIFY(data.errorString.contains("allowlisting"));
        QVERIFY(network.calls.size() >= 4);
        QVERIFY(network.calls.size() <= 6);
        QVERIFY(logs.messages.join('\n').contains(emptyError ? "Application access denied" : "Invalid app registration"));
        QVERIFY(!logs.containsCredentials());
    }
    void revokedAuthorization()
    {
        AuthLogCapture logs;
        AuthNetwork network;
        network.tokenStatus = 400;
        AccountData data;
        data.msaClientID = APPLICATION->getMSAClientID();
        data.msaToken.refresh_token = "TEST-REFRESH-CREDENTIAL";
        AuthFlow flow(&data, AuthFlow::Action::Refresh, &network);
        QSignalSpy finished(&flow, &Task::finished);
        flow.start();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(flow.getState() == Task::State::Failed);
        QCOMPARE(network.calls.size(), 1);
        QVERIFY(!logs.containsCredentials());
    }
    void xstsRestrictions_data()
    {
        QTest::addColumn<qint64>("code");
        QTest::addColumn<QString>("message");
        QTest::newRow("missing-profile") << qint64(2148916233) << QString("Xbox Live profile");
        QTest::newRow("region") << qint64(2148916235) << QString("country");
        QTest::newRow("child") << qint64(2148916238) << QString("family");
        QTest::newRow("guardian") << qint64(2148916229) << QString("guardian");
    }
    void xstsRestrictions()
    {
        QFETCH(qint64, code);
        QFETCH(QString, message);
        AuthLogCapture logs;
        AuthNetwork network;
        network.xstsError = code;
        AccountData data;
        XboxAuthorizationStep step(&data, &data.mojangservicesToken, "rp://api.minecraftservices.com/", "Mojang");
        step.setNetwork(&network);
        QSignalSpy finished(&step, &AuthStep::finished);
        step.perform();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.first()[1].toString().contains(message));
        QVERIFY(!logs.containsCredentials());
    }
    void deviceCodeFallback()
    {
        AuthLogCapture logs;
        AuthNetwork network;
        AccountData data;
        MSADeviceCodeStep step(&data);
        step.setNetwork(&network);
        QSignalSpy browser(&step, &MSADeviceCodeStep::authorizeWithBrowser);
        QSignalSpy finished(&step, &AuthStep::finished);
        step.perform();
        QTRY_COMPARE(browser.count(), 1);
        QVERIFY(QMetaObject::invokeMethod(&step, "authenticateUser", Qt::DirectConnection));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(data.msaClientID, APPLICATION->getMSAClientID());
        QVERIFY(!data.msaToken.refresh_token.isEmpty());
        QCOMPARE(QUrlQuery(QString::fromUtf8(network.calls.first().body)).queryItemValue("client_id"), APPLICATION->getMSAClientID());
        QVERIFY(!logs.containsCredentials());
        step.abort();
    }
    void accountsPersistSwitchReplaceAndRemove()
    {
        QTemporaryDir directory;
        AccountList list;
        list.setListFilePath(directory.filePath("accounts.json"));
        QCOMPARE(list.count(), 0);
        auto first = MinecraftAccount::createBlankMSA();
        first->accountData()->minecraftProfile.id = "first";
        first->accountData()->minecraftProfile.name = "First";
        first->accountData()->minecraftProfile.skin = {"skin", "https://textures.minecraft.net/texture/test", "classic", {}};
        first->accountData()->msaClientID = APPLICATION->getMSAClientID();
        first->accountData()->msaToken.refresh_token = "TEST-REFRESH-CREDENTIAL";
        auto second = MinecraftAccount::createBlankMSA();
        second->accountData()->minecraftProfile.id = "second";
        second->accountData()->minecraftProfile.name = "Second";
        list.addAccount(first);
        list.addAccount(second);
        list.setDefaultAccount(second);
        QCOMPARE(list.defaultAccount(), second);
        list.setDefaultAccount(first);
        auto reauthenticated = MinecraftAccount::loadFromJsonV3(first->saveToJson());
        QVERIFY(reauthenticated);
        list.addAccount(reauthenticated);
        QCOMPARE(list.count(), 2);
        QCOMPARE(list.defaultAccount(), reauthenticated);
        QVERIFY(list.saveList());
        AccountList restored;
        restored.setListFilePath(directory.filePath("accounts.json"));
        QVERIFY(restored.loadList());
        QCOMPARE(restored.count(), 2);
        QVERIFY(restored.defaultAccount());
        QVERIFY(!restored.defaultAccount()->accountData()->msaToken.refresh_token.isEmpty());
        restored.removeAccount(restored.index(0, 0));
        QCOMPARE(restored.count(), 1);
        QVERIFY(!restored.defaultAccount());
    }
    void liveMicrosoftAccount()
    {
        if (!qEnvironmentVariableIsSet("AWAKE_AUTH_LIVE")) QSKIP("Interactive Microsoft login was not requested");
        auto account = MinecraftAccount::createBlankMSA();
        auto flow = account->login(false);
        APPLICATION->network()->setProxy(QNetworkProxy::NoProxy);
        connect(flow.get(), &AuthFlow::authorizeWithBrowser, this, [](const QUrl& url) { QDesktopServices::openUrl(url); });
        QSignalSpy finished(flow.get(), &Task::finished);
        flow->start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 600000);
        QVERIFY2(flow->getState() == Task::State::Succeeded, qPrintable(account->accountData()->errorString));
        QVERIFY(!account->profileId().isEmpty());
        auto restored = MinecraftAccount::loadFromJsonV3(account->saveToJson());
        auto refresh = restored->refresh();
        QSignalSpy refreshed(refresh.get(), &Task::finished);
        refresh->start();
        QTRY_COMPARE_WITH_TIMEOUT(refreshed.count(), 1, 60000);
        QVERIFY2(refresh->getState() == Task::State::Succeeded, qPrintable(restored->accountData()->errorString));
    }
};

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsSet("AWAKE_AUTH_CALLBACK_HELPER")) {
        Application app(argc, argv);
        return app.status() == Application::Succeeded ? 0 : 1;
    }
    const auto original = QDir::currentPath();
    QDir().mkpath(original + "/.validation");
    QTemporaryDir data(original + "/.validation/awake-auth-XXXXXX");
    if (!data.isValid()) return 1;
    QFile config(data.path() + "/awakelauncher.cfg");
    if (!config.open(QIODevice::WriteOnly)) return 1;
    config.write("Language=en_US\nIgnoreJavaWizard=true\nAutomaticJavaDownload=true\nAutomaticJavaSwitch=true\nUserAskedAboutAutomaticJavaDownload=true\nProxyType=HTTP\nProxyAddr=127.0.0.1\nProxyPort=9\n");
    config.close();
    seedStartupAccount(data.path());
    qputenv("AWAKE_FRONTEND", "widgets");
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
        QTimer temporaryWarning;
        temporaryWarning.setInterval(20);
        QObject::connect(&temporaryWarning, &QTimer::timeout, &app, [&] {
            if (app.status() == Application::Initialized) temporaryWarning.stop();
            else if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                if (warning->text().startsWith("Your instance folder is in a temporary folder:")) warning->accept();
        });
        temporaryWarning.start();
        QTimer::singleShot(0, &app, [&] { MicrosoftAuthTest test; result = QTest::qExec(&test, argc, argv); app.quit(); });
        app.exec();
    }
    QDir::setCurrent(original);
    return result;
}
#include "MicrosoftAuth_test.moc"
