// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QFutureWatcher>
#include <QPointer>
#include <QTemporaryDir>
#include <QUrl>
#include "InstanceTask.h"
#include "LocalInstanceImport.h"

namespace Awake::LocalImport {
class ImportTask final : public InstanceTask {
public:
    explicit ImportTask(Instance instance) : m_source(std::move(instance)) {}
    ~ImportTask() override;
protected:
    void executeTask() override;
private:
    Instance m_source;
    QFutureWatcher<QString> m_copy;
    std::unique_ptr<MinecraftInstance> m_instance;
};
class PackLinkTask final : public Task {
public:
    PackLinkTask(MinecraftInstance* instance, Pack pack, QUrl archive);
    ~PackLinkTask() override;
    bool abort() override;
protected:
    void executeTask() override;
private:
    void commitLink();
    QPointer<MinecraftInstance> m_instance;
    Pack m_pack;
    QUrl m_archive;
    Instance m_snapshot;
    QTemporaryDir m_work;
    Task::Ptr m_download;
    QFutureWatcher<QString> m_extract;
};
}
