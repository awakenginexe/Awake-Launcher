// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "MSAStep.h"

#include <QAbstractOAuth2>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QSet>
#include <QOAuthHttpServerReplyHandler>
#include <QOAuthOobReplyHandler>

#include "Application.h"
#include "BuildConfig.h"
#include "FileSystem.h"
#include "minecraft/auth/OAuthCallback.h"

#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

bool isSchemeHandlerRegistered()
{
#ifdef Q_OS_LINUX
    QProcess process;
    process.start("xdg-mime", { "query", "default", "x-scheme-handler/" + BuildConfig.LAUNCHER_APP_BINARY_NAME });
    process.waitForFinished();
    QString output = process.readAllStandardOutput().trimmed();

    return output.contains(APPLICATION->desktopFileName());

#elif defined(Q_OS_WIN)
    QString regPath = QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(BuildConfig.LAUNCHER_APP_BINARY_NAME);
    QSettings settings(regPath, QSettings::NativeFormat);

    const auto scheme = BuildConfig.LAUNCHER_APP_BINARY_NAME;
    QSettings choice("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\" + scheme + "\\UserChoice",
                     QSettings::NativeFormat);
    const auto progId = choice.value("ProgId", scheme).toString();
    QSettings effective("HKEY_CLASSES_ROOT\\" + progId, QSettings::NativeFormat);
    const auto command = effective.value("shell/open/command/.").toString().replace("\\", "/");
    const auto expected = QString("\"%1\" \"%2\"").arg(QCoreApplication::applicationFilePath(), "%1");
    const auto dataPath = QDir(APPLICATION->dataRoot()).absolutePath();
    const auto defaultPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/..").absolutePath();
    const auto withDataPath = QString("\"%1\" -d \"%2\" \"%3\"").arg(QCoreApplication::applicationFilePath(), dataPath, "%1");
    return settings.contains("URL Protocol") &&
        ((dataPath.compare(defaultPath, Qt::CaseInsensitive) == 0 && command.compare(expected, Qt::CaseInsensitive) == 0) ||
         command.compare(withDataPath, Qt::CaseInsensitive) == 0);
#endif
    return true;
}

class TokenReplyHandler : public QOAuthOobReplyHandler {
public:
    using QOAuthOobReplyHandler::networkReplyFinished;
};

class GuardedOAuthReplyHandler : public QOAuthOobReplyHandler {
    Q_OBJECT

   public:
    GuardedOAuthReplyHandler(QOAuth2AuthorizationCodeFlow* flow, bool loopback, QObject* parent) : QOAuthOobReplyHandler(parent), m_flow(flow)
    {
        if (loopback) {
            m_http = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, 0, this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            m_http->setCallbackHost("localhost");
#endif
            m_http->setCallbackPath("/");
            m_http->setCallbackText(tr("Awake Launcher received the sign-in response. You can return to the launcher."));
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            connect(m_http, &QOAuthHttpServerReplyHandler::callbackDataReceived, this, [this](const QByteArray& bytes) {
                const QUrl url = QUrl::fromEncoded(bytes, QUrl::StrictMode);
                QSet<QString> keys;
                m_loopbackValid = url.isValid() && bytes.size() <= 16384 && !url.hasFragment() && url.userInfo().isEmpty();
                for (const auto& [key, value] : QUrlQuery(url).queryItems(QUrl::FullyDecoded)) {
                    if (keys.contains(key) || key == "access_token" || key == "refresh_token") m_loopbackValid = false;
                    keys.insert(key);
                }
            });
#endif
            connect(m_http, &QOAuthHttpServerReplyHandler::callbackReceived, this, &GuardedOAuthReplyHandler::receiveCallback);
        } else {
            connect(APPLICATION, &Application::oauthReplyRecieved, this, &GuardedOAuthReplyHandler::receiveCallback);
        }
        connect(&m_tokens, &QOAuthOobReplyHandler::tokensReceived, this, [this](QVariantMap tokens) {
            if (!active) return;
            if (tokens.contains("error")) {
                tokens["error"] = OAuthCallback::errorCode(tokens.value("error").toString());
                tokens.remove("error_description");
                tokens.remove("error_uri");
            }
            emit tokensReceived(tokens);
        });
        connect(&m_tokens, &QOAuthOobReplyHandler::tokenRequestErrorOccurred, this,
                [this](QAbstractOAuth::Error error, const QString&) {
                    if (active) emit tokenRequestErrorOccurred(error, tr("Microsoft token exchange failed."));
                });
    }
    QString callback() const override
    {
        if (!m_http) return BuildConfig.LAUNCHER_APP_BINARY_NAME + "://oauth/microsoft";
        // Qt 6.8 has no callbackHost setter. Bind to loopback but advertise the registered hostname.
        QUrl url(m_http->callback());
        url.setHost("localhost");
        return url.toString();
    }
    bool isListening() const { return !m_http || m_http->isListening(); }
    void stop()
    {
        active = false;
        if (m_http) m_http->close();
    }
    bool active = false;

   protected:
    void networkReplyFinished(QNetworkReply* reply) override
    {
        if (!active) return;
        if (reply->error() != QNetworkReply::NoError) {
            const auto object = QJsonDocument::fromJson(reply->peek(16384)).object();
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto code = OAuthCallback::errorCode(object.value("error").toString());
            qWarning() << "[Auth] Stage: Microsoft token exchange; HTTP:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "Error:" << code;
            if (status >= 400 && status < 500 && object.contains("error")) {
                emit tokensReceived({{"error", code}});
                return;
            }
        }
        m_tokens.networkReplyFinished(reply);
    }

   private:
    void receiveCallback(QVariantMap data)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        const bool validSource = !m_http || m_loopbackValid;
#else
        const bool validSource = true;
#endif
        m_loopbackValid = false;
        if (!active || !validSource || !OAuthCallback::matchesState(data, m_flow->state()) ||
            m_flow->status() != QAbstractOAuth::Status::NotAuthenticated) {
            qWarning() << "[Auth] Ignored inactive or invalid OAuth callback";
            return;
        }
        data.remove("error_description");
        data.remove("error_uri");
        if (data.contains("error")) data["error"] = OAuthCallback::errorCode(data.value("error").toString());
        qInfo() << "[Auth] Callback received";
        emit callbackReceived(data);
    }
    QOAuth2AuthorizationCodeFlow* m_flow;
    QOAuthHttpServerReplyHandler* m_http = nullptr;
    bool m_loopbackValid = false;
    TokenReplyHandler m_tokens;
};

MSAStep::MSAStep(AccountData* data, bool silent) : AuthStep(data), m_silent(silent)
{
    OAuthCallback::protectReplyHandlerLogs();
    m_clientId = APPLICATION->getMSAClientID();
    const bool loopback = QCoreApplication::applicationFilePath().startsWith("/tmp/.mount_") || APPLICATION->isPortable() ||
                          QFile::exists(FS::PathCombine(APPLICATION->root(), "portable.txt")) ||
                          !isSchemeHandlerRegistered();
    m_oauth2.setReplyHandler(new GuardedOAuthReplyHandler(&m_oauth2, loopback, this));
    m_oauth2.setAuthorizationUrl(QUrl("https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize"));
    m_oauth2.setAccessTokenUrl(QUrl("https://login.microsoftonline.com/consumers/oauth2/v2.0/token"));
    m_oauth2.setScope("XboxLive.SignIn XboxLive.offline_access");
    m_oauth2.setClientIdentifier(m_clientId);
    m_oauth2.setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    m_oauth2.setModifyParametersFunction([this](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant>* map) {
        map->remove("client_secret");
        if (!m_silent && stage == QAbstractOAuth::Stage::RequestingAuthorization) map->insert("prompt", "select_account");
    });
    m_oauth2.setNetworkAccessManager(APPLICATION->network());

    connect(&m_oauth2, &QOAuth2AuthorizationCodeFlow::granted, this, [this] {
        if (m_complete) return;
        m_data->msaClientID = m_oauth2.clientIdentifier();
        m_data->msaToken.issueInstant = QDateTime::currentDateTimeUtc();
        m_data->msaToken.notAfter = m_oauth2.expirationAt();
        m_data->msaToken.extra = m_oauth2.extraTokens();
        m_data->msaToken.refresh_token = m_oauth2.refreshToken();
        m_data->msaToken.token = m_oauth2.token();
        qInfo() << "[Auth] Microsoft token exchange succeeded";
        complete(AccountTaskState::STATE_WORKING, tr("Got MSA token"));
    });
    connect(&m_oauth2, &QOAuth2AuthorizationCodeFlow::authorizeWithBrowser, this, &MSAStep::authorizeWithBrowser);
    connect(&m_oauth2, &QOAuth2AuthorizationCodeFlow::requestFailed, this, [this, silent](const QAbstractOAuth2::Error err) {
        auto state = AccountTaskState::STATE_FAILED_HARD;
        if (m_oauth2.status() == QAbstractOAuth::Status::Granted || silent) {
            if (err == QAbstractOAuth2::Error::NetworkError) {
                state = AccountTaskState::STATE_OFFLINE;
            } else {
                state = AccountTaskState::STATE_FAILED_SOFT;
            }
        }
        auto message = tr("Microsoft user authentication failed.");
        if (silent) {
            message = tr("Failed to refresh token.");
        }
        qWarning() << "[Auth] Microsoft token exchange failed; OAuth error:" << int(err);
        complete(state, message);
    });
    connect(&m_oauth2, &QOAuth2AuthorizationCodeFlow::error, this,
            [this](const QString& error, const QString&, const QUrl&) {
                const auto code = OAuthCallback::errorCode(error);
                qWarning() << "[Auth] Stage: Microsoft OAuth; Error:" << code;
                complete(AccountTaskState::STATE_FAILED_HARD, tr("Microsoft authorization failed: %1").arg(code));
            });
    m_timeout.setObjectName("microsoftAuthorizationTimeout");
    m_timeout.setParent(this);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(10 * 60 * 1000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        complete(AccountTaskState::STATE_FAILED_HARD, tr("Microsoft authorization request expired. Please try again."));
    });
}

QString MSAStep::describe()
{
    return tr("Logging in with Microsoft account.");
}

void MSAStep::perform()
{
    if (m_complete) return;
    if (m_clientId.isEmpty()) {
        complete(AccountTaskState::STATE_DISABLED, tr("Microsoft sign-in needs an Awake Launcher application ID."));
        return;
    }
    m_oauth2.setNetworkAccessManager(network());
    auto* handler = static_cast<GuardedOAuthReplyHandler*>(m_oauth2.replyHandler());
    handler->active = true;
    if (m_silent) {
        if (m_data->msaClientID != m_clientId) {
            complete(AccountTaskState::STATE_DISABLED,
                          tr("Microsoft user authentication failed - client identification has changed."));
            return;
        }
        if (m_data->msaToken.refresh_token.isEmpty()) {
            complete(AccountTaskState::STATE_DISABLED, tr("Microsoft user authentication failed - refresh token is empty."));
            return;
        }
        m_oauth2.setRefreshToken(m_data->msaToken.refresh_token);
        qInfo() << "[Auth] Microsoft token refresh started";
        m_oauth2.refreshAccessToken();
    } else {
        if (!handler->isListening()) {
            complete(AccountTaskState::STATE_FAILED_HARD, tr("Cannot listen for the Microsoft localhost callback. Try Device Code login."));
            return;
        }
        qInfo() << "[Auth] Microsoft authorization started";
        m_timeout.start();
        m_oauth2.grant();
    }
}

void MSAStep::complete(AccountTaskState state, const QString& message)
{
    if (m_complete) return;
    m_complete = true;
    m_timeout.stop();
    static_cast<GuardedOAuthReplyHandler*>(m_oauth2.replyHandler())->stop();
    emit finished(state, message);
}

void MSAStep::abort()
{
    m_complete = true;
    m_timeout.stop();
    static_cast<GuardedOAuthReplyHandler*>(m_oauth2.replyHandler())->stop();
}

#include "MSAStep.moc"
