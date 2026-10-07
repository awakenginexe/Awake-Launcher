// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeUpdateChecker.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QProcess>
#include <QCoreApplication>
#include <algorithm>

namespace Awake {
namespace {
constexpr qint64 MaxResponse = 1024 * 1024;
constexpr qint64 MaxInstallerSize = 512 * 1024 * 1024;
const QString Repository = "https://github.com/awakenginexe/Awake-Launcher";
QVariantMap errorState(const QString& detail) { return {{"status", "error"}, {"error", detail}}; }
}

QVariantMap parseUpdateRelease(const QByteArray& body, const QString& currentVersion)
{
    if (body.size() > MaxResponse) return errorState("The release response is too large.");
    const auto document = QJsonDocument::fromJson(body);
    const auto release = document.object();
    const auto tag = release.value("tag_name").toString();
    static const QRegularExpression stableTag("^v(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$");
    if (!document.isObject() || !stableTag.match(tag).hasMatch() ||
        !release.value("draft").isBool() || release.value("draft").toBool() ||
        !release.value("prerelease").isBool() || release.value("prerelease").toBool() ||
        !release.value("assets").isArray())
        return errorState("GitHub did not return a valid stable release.");
    const auto version = QVersionNumber::fromString(tag.mid(1));
    const auto current = QVersionNumber::fromString(currentVersion);
    if (version.segmentCount() != 3 || current.segmentCount() != 3)
        return errorState("The release version could not be compared.");
    const QString releaseUrl = Repository + "/releases/tag/" + tag;
    if (release.value("html_url").toString() != releaseUrl)
        return errorState("The release does not belong to Awake Launcher.");
    QVariantMap result{{"status", version > current ? "available" : "upToDate"},
                       {"latestVersion", tag.mid(1)}, {"notes", release.value("body").toString().left(100'000)},
                       {"releaseUrl", releaseUrl}, {"setupUrl", ""}, {"portableUrl", ""}, {"error", ""}};
    const QString assetBase = "Awake-Launcher-" + tag + "-Windows-x64";
    for (const auto& item : release.value("assets").toArray()) {
        const auto asset = item.toObject();
        const auto name = asset.value("name").toString();
        const auto url = asset.value("browser_download_url").toString();
        if (url != Repository + "/releases/download/" + tag + "/" + name) continue;
        if (name == assetBase + "-Setup.exe") {
            result["setupUrl"] = url;
            const auto digest = asset.value("digest").toString();
            const auto size = asset.value("size").toInteger();
            static const QRegularExpression sha256("^sha256:[0-9a-f]{64}$");
            if (sha256.match(digest).hasMatch() && size > 0 && size <= MaxInstallerSize) {
                result["setupDigest"] = digest.mid(7);
                result["setupSize"] = size;
            }
        }
        if (name == assetBase + ".zip") result["portableUrl"] = url;
    }
    return result;
}

UpdateChecker::UpdateChecker(QNetworkAccessManager* network, const QString& currentVersion, const QString& dataDir,
                             bool portable, QObject* parent, QUrl endpoint)
    : m_network(network), m_settings(QDir(dataDir).filePath("awake_update.cfg"), QSettings::IniFormat),
      m_endpoint(std::move(endpoint)), m_currentVersion(currentVersion), m_state{{"status", "idle"}}, m_portable(portable)
{
    setParent(parent);
    m_timer.setSingleShot(true);
    m_deadline.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { check(false); });
    connect(&m_deadline, &QTimer::timeout, this, [this] { if (m_reply) m_reply->abort(); });
    armTimer();
    if (getAutomaticallyChecksForUpdates()) m_timer.start(0);
}

UpdateChecker::~UpdateChecker()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); }
}

QVariantMap UpdateChecker::state() const
{
    auto result = m_state;
    result.insert("currentVersion", m_currentVersion);
    result.insert("automatic", m_settings.value("automatic", true).toBool());
    result.insert("portable", m_portable);
    result.insert("presentation", m_presentation);
    result.insert("canInstall", !m_portable && !m_state.value("setupDigest").toString().isEmpty());
    return result;
}

bool UpdateChecker::getAutomaticallyChecksForUpdates() { return m_settings.value("automatic", true).toBool(); }
void UpdateChecker::setAutomaticallyChecksForUpdates(bool enabled)
{
    m_settings.setValue("automatic", enabled);
    m_settings.sync();
    armTimer();
    emit stateChanged();
}

void UpdateChecker::armTimer()
{
    m_timer.stop();
    if (!getAutomaticallyChecksForUpdates() || m_reply) return;
    const auto last = m_settings.value("last_check", 0).toLongLong();
    const auto remaining = std::clamp<qint64>(last + 86400 - QDateTime::currentSecsSinceEpoch(), 0, 86400);
    m_timer.start(static_cast<int>(remaining * 1000));
}

void UpdateChecker::check(bool manual)
{
    if (m_state.value("status") == "downloading" || m_state.value("status") == "installing") return;
    if (manual) { ++m_presentation; m_manual = true; }
    if (m_reply) { emit stateChanged(); return; }
    m_manual = manual;
    m_timer.stop();
    m_body.clear();
    m_tooLarge = false;
    m_state = {{"status", "checking"}};
    QNetworkRequest request(m_endpoint);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "AwakeLauncher/" + m_currentVersion.toUtf8());
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    auto* reply = m_network->get(request);
    m_reply = reply;
    reply->setReadBufferSize(MaxResponse + 1);
    const auto consume = [this, reply] {
        m_body += reply->readAll();
        if (m_body.size() > MaxResponse) { m_tooLarge = true; reply->abort(); }
    };
    connect(reply, &QIODevice::readyRead, this, consume);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_deadline.stop();
        m_body += reply->readAll();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (m_tooLarge || m_body.size() > MaxResponse) m_state = errorState("The release response is too large.");
        else if (status == 403 || status == 429) m_state = errorState("GitHub's request limit was reached. Try again later.");
        else if (reply->error() != QNetworkReply::NoError || status != 200)
            m_state = errorState("Unable to contact GitHub. Check your connection and try again.");
        else m_state = parseUpdateRelease(m_body, m_currentVersion);
        m_body.clear();
        m_reply = nullptr;
        reply->deleteLater();
        m_settings.setValue("last_check", QDateTime::currentSecsSinceEpoch());
        if (!m_manual && m_state.value("status") == "available") {
            ++m_presentation;
        }
        m_settings.sync();
        emit canCheckForUpdatesChanged(true);
        emit stateChanged();
        armTimer();
    });
    m_deadline.start(15000);
    emit canCheckForUpdatesChanged(false);
    emit stateChanged();
}

bool UpdateChecker::openDownload(const QString& kind)
{
    if (m_state.value("status") != "available") return false;
    if (kind == "setup" && !m_portable) return downloadInstaller();
    const QString key = kind == "setup" ? "setupUrl" : kind == "portable" ? "portableUrl" : kind == "release" ? "releaseUrl" : "";
    const auto url = m_state.value(key).toString();
    return !key.isEmpty() && !url.isEmpty() && QDesktopServices::openUrl(QUrl(url));
}

bool UpdateChecker::downloadInstaller()
{
    if (m_reply || !state().value("canInstall").toBool()) return false;
    m_timer.stop();
    m_downloadDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/AwakeLauncher-update-XXXXXX");
    m_installerPath = m_downloadDir->filePath(QUrl(m_state.value("setupUrl").toString()).fileName());
    m_downloadFile = std::make_unique<QSaveFile>(m_installerPath);
    if (!m_downloadDir->isValid() || !m_downloadFile->open(QIODevice::WriteOnly)) {
        installationFailed("Unable to create the update download. Check available disk space.");
        return false;
    }
    m_downloadHash.reset();
    m_downloaded = 0;
    m_state["status"] = "downloading";
    m_state["progress"] = 0;
    m_state["error"] = "";
    const auto size = m_state.value("setupSize").toLongLong();
    QNetworkRequest request(QUrl(m_state.value("setupUrl").toString()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    auto* reply = m_network->get(request);
    m_reply = reply;
    reply->setReadBufferSize(1024 * 1024);
    const auto consume = [this, reply, size] {
        const auto chunk = reply->readAll();
        m_downloaded += chunk.size();
        if (m_downloaded > size || m_downloadFile->write(chunk) != chunk.size()) {
            m_state["error"] = "The update download exceeded its expected size or could not be saved.";
            reply->abort();
            return;
        }
        m_downloadHash.addData(chunk);
        m_deadline.start(30000);
        const int progress = static_cast<int>(m_downloaded * 100 / size);
        if (m_state.value("progress").toInt() != progress) { m_state["progress"] = progress; emit stateChanged(); }
    };
    connect(reply, &QIODevice::readyRead, this, consume);
    connect(reply, &QNetworkReply::finished, this, [this, reply, size, consume] {
        // A failed response must never leave an executable ready for installation.
        if (reply->bytesAvailable()) consume();
        m_deadline.stop();
        m_reply = nullptr;
        const bool verified = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
            m_downloaded == size && m_downloadHash.result().toHex() == m_state.value("setupDigest").toByteArray();
        reply->deleteLater();
        if (!verified || !m_downloadFile->commit()) {
            installationFailed(m_state.value("error").toString().isEmpty()
                ? "The installer download failed verification. Check your connection and retry the update."
                : m_state.value("error").toString());
            return;
        }
        m_downloadFile.reset();
        m_state["status"] = "installing";
        emit stateChanged();
        emit installerReady();
    });
    m_deadline.start(30000);
    emit stateChanged();
    return true;
}

void UpdateChecker::installationFailed(const QString& error)
{
    m_downloadFile.reset();
    m_downloadDir.reset();
    m_installerPath.clear();
    m_state["status"] = "available";
    m_state["error"] = error;
    armTimer();
    emit stateChanged();
}

bool UpdateChecker::installUpdate(const QString& installDir)
{
    if (m_state.value("status") != "installing" || m_installerPath.isEmpty() || !m_downloadDir) return false;
    // NSIS waits for this process to exit before replacing files, then restarts the launcher.
    QProcess installer;
    installer.setProgram(m_installerPath);
#ifdef Q_OS_WIN
    // NSIS requires /D last and unquoted, including when the directory contains spaces.
    installer.setNativeArguments(QString("/S /AWAKEUPDATE=%1 /AWAKEDATA=\"%2\" /D=%3")
                                    .arg(QCoreApplication::applicationPid())
                                    .arg(QFileInfo(m_settings.fileName()).absolutePath(), QDir::toNativeSeparators(installDir)));
#else
    return false;
#endif
    if (!installer.startDetached()) {
        installationFailed("Unable to start the installer. Retry the update.");
        return false;
    }
    m_downloadDir->setAutoRemove(false);
    return true;
}

void UpdateChecker::acknowledgeNotification()
{
    if (m_state.value("status") != "available") return;
    m_settings.setValue("notified_version", m_state.value("latestVersion"));
    m_settings.sync();
}
}
