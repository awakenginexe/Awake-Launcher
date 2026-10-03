#include "LauncherLoginStep.h"

#include <QNetworkRequest>
#include <QUrl>

#include "Application.h"
#include "minecraft/auth/OAuthCallback.h"
#include "Logging.h"
#include "minecraft/auth/Parsers.h"
#include "net/NetUtils.h"
#include "net/RawHeaderProxy.h"
#include "net/Request.h"

LauncherLoginStep::LauncherLoginStep(AccountData* data) : AuthStep(data) {}

QString LauncherLoginStep::describe()
{
    return tr("Fetching Minecraft access token");
}

void LauncherLoginStep::perform()
{
    QUrl url("https://api.minecraftservices.com/launcher/login");
    auto uhs = m_data->mojangservicesToken.extra["uhs"].toString();
    auto xToken = m_data->mojangservicesToken.token;

    QString mc_auth_template = R"XXX(
{
    "xtoken": "XBL3.0 x=%1;%2",
    "platform": "PC_LAUNCHER"
}
)XXX";
    auto requestBody = mc_auth_template.arg(uhs, xToken);

    auto headers = QList<Net::HeaderPair>{
        { "Content-Type", "application/json" },
        { "Accept", "application/json" },
    };

    auto [request, response] = Net::Request::makeByteArray(url, requestBody.toUtf8(), Net::Request::Option::Sensitive);
    m_request = request;
    m_request->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(headers));
    m_request->enableAutoRetry(true);

    m_task.reset(new NetJob("LauncherLoginStep", network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);

    connect(m_task.get(), &Task::finished, this, [this, response] { onRequestDone(response); });

    m_task->start();
    qDebug() << "Getting Minecraft access token...";
}

void LauncherLoginStep::onRequestDone(QByteArray* response)
{
    if (m_request->error() != QNetworkReply::NoError) {
        const auto status = m_request->replyStatusCode();
        if (status == 403) {
            const auto error = OAuthCallback::invalidMinecraftRegistration(status, *response)
                                   ? QStringLiteral("Invalid app registration") : QStringLiteral("Application access denied");
            qWarning() << "[Auth] Stage: Minecraft Services; HTTP: 403; Error:" << error;
            emit finished(AccountTaskState::STATE_FAILED_SOFT,
                          tr("Minecraft Services rejected Awake Launcher's application (HTTP 403: %1). "
                             "Microsoft OAuth, Xbox Live and XSTS completed. Awake's Microsoft application may require "
                             "Minecraft Services approval/allowlisting. This is separate from Microsoft sign-in configuration.").arg(error));
            return;
        }
        qWarning() << "[Auth] Stage: Minecraft Services; HTTP:" << status << "Network error:" << int(m_request->error());
        if (Net::isApplicationError(m_request->error()) && !Net::isServerError(m_request->error())) {
            emit finished(AccountTaskState::STATE_FAILED_SOFT,
                          tr("Failed to get Minecraft access token: %1").arg(m_request->errorString()));
        } else {
            m_data->networkError = m_request->error();
            emit finished(AccountTaskState::STATE_OFFLINE, tr("Failed to get Minecraft access token: %1").arg(m_request->errorString()));
        }
        return;
    }

    if (!Parsers::parseMojangResponse(*response, m_data->yggdrasilToken)) {
        qWarning() << "Could not parse login_with_xbox response...";
        emit finished(AccountTaskState::STATE_FAILED_SOFT, tr("Failed to parse the Minecraft access token response."));
        return;
    }
    qInfo() << "[Auth] Minecraft Services authentication succeeded";
    emit finished(AccountTaskState::STATE_WORKING, tr("Got Minecraft access token"));
}
