// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantMap>

class QAbstractItemModel;
class MinecraftInstance;
class ResourceFolderModel;
namespace Awake::Web {
class Assets;
class InstanceEditor final : public QObject {
    Q_OBJECT
public:
    explicit InstanceEditor(Assets* assets, QObject* parent = nullptr);
    QVariantMap details(const QString& instanceId, const QString& section);
    QVariantMap command(const QString& instanceId, const QString& command, const QVariant& payload);
signals:
    void changed(QString instanceId, QString section);
    void failed(QString detail);
    void modalChanged(bool active);
private:
    void observe(QAbstractItemModel* model, const QString& id, const QString& section);
    QPointer<Assets> m_assets;
    QSet<QAbstractItemModel*> m_observed;
    bool m_contentDialogActive = false;
};
}
