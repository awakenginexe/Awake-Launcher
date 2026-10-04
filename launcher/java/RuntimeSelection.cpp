// SPDX-License-Identifier: GPL-3.0-only
#include "RuntimeSelection.h"
#include "Application.h"
#include "JavaChecker.h"
#include "JavaUtils.h"
#include "download/ArchiveDownloadTask.h"
#include "net/NetJob.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QUuid>

namespace Java {
namespace {
bool compatibleJava(const JavaChecker::Result& result, int major, const QString& architecture)
{
    if (result.validity != JavaChecker::Result::Validity::Valid || result.javaVersion.major() != major) return false;
    if (architecture == "aarch64") return QStringList{"aarch64", "arm64"}.contains(result.realPlatform);
    if (architecture == "x64") return result.is_64bit && QStringList{"amd64", "x86_64"}.contains(result.realPlatform);
    return !result.is_64bit && QStringList{"x86", "i386", "i686"}.contains(result.realPlatform);
}
}
bool runtimeProfileAllowed(const QString& profile)
{
    return QStringList{"awake", "minecraft", "microsoft", "graalvm", "temurin", "zulu", "oracle", "custom"}.contains(profile);
}
QString runtimeDistribution(const QString& profile)
{
    if (profile == "awake" || profile == "temurin") return "temurin";
    if (profile == "graalvm") return "graalvm_community";
    if (profile == "oracle") return "oracle_open_jdk";
    if (profile == "microsoft" || profile == "zulu") return profile;
    return {};
}
QJsonObject selectRuntimePackage(const QJsonArray& packages, const QString& distribution, int major,
                                const QString& os, const QString& architecture)
{
    for (const auto& value : packages) {
        const auto p = value.toObject();
        if (p["distribution"].toString() != distribution || p["major_version"].toInt() != major ||
            p["operating_system"].toString() != os || p["architecture"].toString() != architecture ||
            p["archive_type"].toString() != (os == "windows" ? "zip" : "tar.gz") ||
            p["release_status"].toString() != "ga" || p["package_type"].toString() != "jdk" ||
            !p["directly_downloadable"].toBool() || p["javafx_bundled"].toBool() ||
            !QRegularExpression("^[a-f0-9]{32}$").match(p["id"].toString()).hasMatch()) continue;
        return p;
    }
    return {};
}
bool runtimeDownloadUrlAllowed(const QUrl& url, const QString& distribution)
{
    if (!url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty() || url.port(-1) != -1) return false;
    if (distribution == "microsoft") return url.host() == "aka.ms" && url.path().startsWith("/download-jdk/");
    if (distribution == "zulu") return url.host() == "cdn.azul.com" && url.path().startsWith("/zulu/bin/");
    if (distribution == "oracle_open_jdk") return url.host() == "download.java.net" && url.path().startsWith("/java/");
    if (distribution == "temurin") return url.host() == "github.com" && url.path().startsWith("/adoptium/temurin");
    if (distribution == "graalvm_community") return url.host() == "github.com" && url.path().startsWith("/graalvm/graalvm-ce-builds/");
    return false;
}
QString runtimeChecksum(const QByteArray& text)
{
    const auto match = QRegularExpression("(?:^|\\s)([a-fA-F0-9]{64})(?:\\s|$)").match(QString::fromLatin1(text));
    return match.hasMatch() ? match.captured(1).toLower() : QString{};
}

RuntimeDownloadTask::RuntimeDownloadTask(QString profile, QList<int> majors, QString platform, QString directory)
    : m_distribution(runtimeDistribution(profile)), m_platform(platform), m_directory(directory), m_majors(majors)
{}
void RuntimeDownloadTask::executeTask()
{
    m_os = m_platform.startsWith("mac-os-") ? "macos" : m_platform.section('-', 0, 0);
    m_architecture = m_platform.section('-', -1);
    if (m_architecture == "arm64") m_architecture = "aarch64";
    m_archive = m_os == "windows" ? "zip" : "tar.gz";
    if (m_distribution.isEmpty() || !QStringList{"windows", "linux", "macos"}.contains(m_os) ||
        !QStringList{"x64", "x86", "aarch64"}.contains(m_architecture) || m_majors.isEmpty()) {
        emitFailed(tr("This Java provider is unavailable for this system. Choose Minecraft Default or Custom Java."));
        return;
    }
    nextMajor();
}
void RuntimeDownloadTask::nextMajor()
{
    if (!isRunning()) return;
    if (m_index >= m_majors.size()) {
        emitFailed(tr("This provider has no compatible Java build for this instance. Choose Awake Optimized, Minecraft Default or Custom Java."));
        return;
    }
    m_major = m_majors[m_index++];
    m_cachedPaths.clear();
    const QDir dir(m_directory);
    for (const auto& folder : dir.entryInfoList({"awake-" + m_distribution + "-" + QString::number(m_major) + "-*"}, QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot, QDir::Time)) {
        QFile marker(QDir(folder.absoluteFilePath()).filePath("awake-runtime.json"));
        if (!marker.open(QIODevice::ReadOnly)) continue;
        const auto data = QJsonDocument::fromJson(marker.read(4096)).object();
        const auto path = QDir(folder.absoluteFilePath()).filePath("bin/" + JavaUtils::javaExecutable);
        if (data["distribution"].toString() == m_distribution && data["major"].toInt() == m_major &&
            data["platform"].toString() == m_platform && QFileInfo(path).isFile()) {
            m_cachedPaths.append(path);
        }
    }
    checkCachedRuntime();
}
void RuntimeDownloadTask::checkCachedRuntime()
{
    if (!isRunning()) return;
    if (m_cachedPaths.isEmpty()) { findPackage(); return; }
    const auto path = m_cachedPaths.takeFirst();
    if (JavaUtils::getJavaCheckPath().isEmpty()) {
        emitFailed(tr("The downloaded Java runtime could not be checked. Choose another provider.")); return;
    }
    auto checker = makeShared<JavaChecker>(path, "");
    connect(checker.get(), &JavaChecker::checkFinished, this, [this, path](const JavaChecker::Result& result) {
        const auto keepAlive = m_task;
        if (!isRunning()) return;
        if (compatibleJava(result, m_major, m_architecture)) { m_javaPath = path; emitSucceeded(); }
        else checkCachedRuntime();
    });
    m_task = checker;
    checker->start();
}
void RuntimeDownloadTask::findPackage()
{
    setStatus(tr("Finding a compatible Java runtime"));
    QUrl url("https://api.foojay.io/disco/v3.0/packages");
    QUrlQuery query;
    const QList<QPair<QString, QString>> filters{{"distribution", m_distribution}, {"version", QString::number(m_major)},
        {"operating_system", m_os}, {"architecture", m_architecture}, {"archive_type", m_archive}, {"package_type", "jdk"},
        {"release_status", "ga"}, {"directly_downloadable", "true"}, {"javafx_bundled", "false"}, {"latest", "available"}};
    for (const auto& filter : filters) query.addQueryItem(filter.first, filter.second);
    url.setQuery(query);
    request(url, [this](QByteArray bytes) {
        const auto package = selectRuntimePackage(QJsonDocument::fromJson(bytes).object()["result"].toArray(), m_distribution, m_major, m_os, m_architecture);
        if (package.isEmpty()) { nextMajor(); return; }
        m_id = package["id"].toString();
        m_finalPath = QDir(m_directory).filePath("awake-" + m_distribution + "-" + QString::number(m_major) + "-" + m_id);
        request(QUrl("https://api.foojay.io/disco/v3.0/ids/" + m_id), [this](QByteArray data) {
            const auto results = QJsonDocument::fromJson(data).object()["result"].toArray();
            if (results.isEmpty()) { emitFailed(tr("Java download information could not be read. Please try again.")); return; }
            download(results.first().toObject());
        });
    });
}
void RuntimeDownloadTask::request(const QUrl& url, std::function<void(QByteArray)> receive)
{
    auto job = makeShared<NetJob>("Awake::JavaMetadata", APPLICATION->network());
    job->setAskRetry(false);
    const auto [action, bytes] = Net::Request::makeByteArray(url);
    job->addNetAction(action);
    connect(job.get(), &Task::failed, this, [this](const QString&) { emitFailed(tr("The Java provider could not be reached. Check your connection and try again.")); });
    connect(job.get(), &Task::aborted, this, &RuntimeDownloadTask::emitAborted);
    connect(job.get(), &Task::succeeded, this, [this, bytes, receive] {
        const auto keepAlive = m_task;
        if (bytes->size() > 4 * 1024 * 1024) { emitFailed(tr("The Java provider response was too large.")); return; }
        receive(*bytes);
    });
    m_task = job;
    propagateFromOther(job.get());
    job->start();
}
void RuntimeDownloadTask::download(const QJsonObject& info)
{
    const auto url = QUrl(info["direct_download_uri"].toString());
    if (!runtimeDownloadUrlAllowed(url, m_distribution) || info["checksum_type"].toString() != "sha256") {
        emitFailed(tr("This Java download could not be verified. Choose another provider."));
        return;
    }
    const auto hash = runtimeChecksum(info["checksum"].toString().toLatin1());
    if (!hash.isEmpty()) { install(url, hash); return; }
    const auto checksumUrl = QUrl(info["checksum_uri"].toString());
    if (!runtimeDownloadUrlAllowed(checksumUrl, m_distribution)) {
        emitFailed(tr("This Java download does not include a trusted checksum. Choose another provider."));
        return;
    }
    request(checksumUrl, [this, url](QByteArray bytes) {
        const auto hash = runtimeChecksum(bytes);
        if (hash.isEmpty()) { emitFailed(tr("The Java download checksum could not be read.")); return; }
        install(url, hash);
    });
}
void RuntimeDownloadTask::install(const QUrl& url, const QString& checksum)
{
    if (!QDir().mkpath(m_directory)) { emitFailed(tr("The Java runtime folder could not be created.")); return; }
    m_stage = std::make_unique<QTemporaryDir>(QDir(m_directory).filePath("awake-stage-XXXXXX"));
    if (!m_stage->isValid()) { emitFailed(tr("The Java runtime folder could not be created.")); return; }
    m_task = makeShared<ArchiveDownloadTask>(url, m_stage->path(), "sha256", checksum, false);
    connect(m_task.get(), &Task::failed, this, &RuntimeDownloadTask::emitFailed);
    connect(m_task.get(), &Task::aborted, this, &RuntimeDownloadTask::emitAborted);
    connect(m_task.get(), &Task::succeeded, this, [this] { const auto keepAlive = m_task; verify(); });
    propagateFromOther(m_task.get());
    m_task->start();
}
void RuntimeDownloadTask::verify()
{
    setStatus(tr("Checking the Java runtime"));
    auto root = m_stage->path();
#if defined(Q_OS_MACOS)
    if (QDir(root + "/Contents/Home").exists()) root += "/Contents/Home";
#endif
    const auto path = QDir(root).filePath("bin/" + JavaUtils::javaExecutable);
    if (!QFileInfo(path).isFile() || JavaUtils::getJavaCheckPath().isEmpty()) {
        emitFailed(tr("The downloaded Java runtime could not be checked. Choose another provider.")); return;
    }
    auto checker = makeShared<JavaChecker>(path, "");
    connect(checker.get(), &JavaChecker::checkFinished, this, [this, root](const JavaChecker::Result& result) {
        if (!isRunning()) return;
        if (!compatibleJava(result, m_major, m_architecture)) {
            emitFailed(tr("The downloaded Java runtime is not compatible. Choose another provider.")); return;
        }
        QFile marker(QDir(root).filePath("awake-runtime.json"));
        if (!marker.open(QIODevice::WriteOnly) || marker.write(QJsonDocument(QJsonObject{{"distribution", m_distribution}, {"major", m_major}, {"packageId", m_id}, {"platform", m_platform}}).toJson()) < 0) {
            emitFailed(tr("The Java runtime settings could not be saved.")); return;
        }
        marker.close();
        if (QFileInfo::exists(m_finalPath)) m_finalPath += "-" + QUuid::createUuid().toString(QUuid::Id128);
        if (!QDir().rename(root, m_finalPath)) {
            emitFailed(tr("The Java runtime could not be installed. Please try again.")); return;
        }
        m_javaPath = QDir(m_finalPath).filePath("bin/" + JavaUtils::javaExecutable);
        emitSucceeded();
    });
    m_task = checker;
    checker->start();
}
bool RuntimeDownloadTask::abort()
{
    if (!canAbort()) return false;
    return m_task->abort();
}
}
