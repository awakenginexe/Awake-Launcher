// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QHash>
#include "ui/instanceview/InstanceDelegate.h"

namespace Awake {
class LibraryDelegate : public ListViewDelegate {
   public:
    explicit LibraryDelegate(QObject* parent = nullptr) : ListViewDelegate(parent) {}
    void setCompact(bool compact) { m_compact = compact; }
    void bindMetadata(QAbstractItemModel* model);
    QString metadata(const QModelIndex& index) const;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

   private:
    bool m_compact = false;
    QHash<QString, QString> m_metadata;
    void refreshMetadata(QAbstractItemModel* model, int first, int last);
};
}  // namespace Awake
