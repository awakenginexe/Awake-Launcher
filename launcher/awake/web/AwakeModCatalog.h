// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QVariantMap>
#include <memory>
#include "modplatform/ModIndex.h"

namespace Awake::Web {
class Assets;
class ModCatalog final : public QObject {
    Q_OBJECT
public:
    explicit ModCatalog(Assets* assets, QObject* parent = nullptr);
    ~ModCatalog() override;
    void search(QString requestId, QString instanceId, QString provider, QString query, QString sort, int offset);
    void versions(QString requestId, QString instanceId, QString provider, QString projectId);
    void prepare(QString requestId, QString instanceId, QVariantList selections);
    void install(QString requestId, QString instanceId, QString reviewId);
    bool cancel(const QString& requestId);
    bool busy(const QString& instanceId = {}) const;
    bool hasRequest(const QString& requestId) const;
    static bool safeVersion(const ModPlatform::IndexedVersion& version, const QString& minecraft,
                            ModPlatform::ModLoaderTypes loaders);
signals:
    void finished(QString requestId, QVariantMap response);
    void progress(QString requestId, QVariantMap state);
    void changed(QString instanceId, QString section);
private:
    struct State;
    std::unique_ptr<State> d;
};
}
