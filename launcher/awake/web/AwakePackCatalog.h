// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QVariantMap>
#include <memory>

class InstanceTask;
class QWidget;

namespace Awake::Web {
class Assets;

class PackCatalog final : public QObject {
    Q_OBJECT
public:
    explicit PackCatalog(Assets* assets, QObject* parent = nullptr);
    ~PackCatalog() override;
    void search(QString requestId, QString provider, QString query, int offset);
    void versions(QString requestId, QString provider, QString packId);
    InstanceTask* createTask(QString provider, QString packId, QString versionId, QWidget* parent, QString* error);

signals:
    void finished(QString requestId, QVariantMap response);

private:
    struct State;
    std::unique_ptr<State> d;
};
}  // namespace Awake::Web
