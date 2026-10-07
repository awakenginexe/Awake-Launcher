// SPDX-License-Identifier: GPL-3.0-only
#include "StartupTelemetry.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>

namespace Awake {
StartupTelemetry::StartupTelemetry(QNetworkAccessManager* network, QString token, QUrl host, QString version, QObject* parent)
    : QObject(parent), m_network(network), m_token(std::move(token)), m_version(std::move(version)), m_host(std::move(host)) {}

void StartupTelemetry::start()
{
    if (m_sent || m_token.isEmpty() || !m_network) return;
    m_sent = true;
    m_host.setPath("/i/v0/e/");
    QNetworkRequest request(m_host);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "AwakeLauncher");
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(5000);
    const QJsonObject payload{{"api_key", m_token}, {"event", "app_started"},
        {"distinct_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"properties", QJsonObject{{"app_version", m_version}, {"os", QSysInfo::kernelType()}, {"$process_person_profile", false}}}};
    auto* reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QTimer::singleShot(10000, reply, &QNetworkReply::abort);
}
}
