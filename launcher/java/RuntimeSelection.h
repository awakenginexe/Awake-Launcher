// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QStringList>
#include "tasks/Task.h"
#include <QTemporaryDir>
#include <functional>

namespace Java {
bool runtimeProfileAllowed(const QString& profile);
QString runtimeDistribution(const QString& profile);
bool jvmPresetAllowed(const QString& preset);
QStringList jvmPresetArguments(const QString& preset, int major, const QString& architecture, const QString& vmName, const QStringList& existing);
QJsonObject selectRuntimePackage(const QJsonArray& packages, const QString& distribution, int major,
                                const QString& os, const QString& architecture);
bool runtimeDownloadUrlAllowed(const QUrl& url, const QString& distribution);
QString runtimeChecksum(const QByteArray& text);

class RuntimeDownloadTask final : public Task {
    Q_OBJECT
public:
    RuntimeDownloadTask(QString profile, QList<int> majors, QString platform, QString directory);
    QString javaPath() const { return m_javaPath; }
    bool canAbort() const override { return m_task && m_task->canAbort(); }
    bool abort() override;
protected:
    void executeTask() override;
private:
    void nextMajor();
    void findPackage();
    void checkCachedRuntime();
    void request(const QUrl& url, std::function<void(QByteArray)> receive);
    void download(const QJsonObject& info);
    void install(const QUrl& url, const QString& checksum);
    void verify();
    QString m_distribution, m_platform, m_directory, m_os, m_architecture, m_archive;
    QList<int> m_majors;
    int m_index = 0, m_major = 0;
    QString m_id, m_javaPath, m_finalPath;
    QStringList m_cachedPaths;
    std::unique_ptr<QTemporaryDir> m_stage;
    Task::Ptr m_task;
};
}
