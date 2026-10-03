// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryDelegate.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStringList>
#include "BaseInstance.h"
#include "InstanceList.h"

namespace Awake {
void LibraryDelegate::bindMetadata(QAbstractItemModel* model)
{
    refreshMetadata(model, 0, model->rowCount() - 1);
    connect(model, &QAbstractItemModel::rowsInserted, this,
            [this, model](const QModelIndex&, int first, int last) { refreshMetadata(model, first, last); });
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this, model](const QModelIndex& first, const QModelIndex& last) { refreshMetadata(model, first.row(), last.row()); });
    connect(model, &QAbstractItemModel::modelReset, this, [this, model] {
        m_metadata.clear();
        refreshMetadata(model, 0, model->rowCount() - 1);
    });
}

void LibraryDelegate::refreshMetadata(QAbstractItemModel* model, int first, int last)
{
    for (int row = first; row <= last; ++row) {
        const auto index = model->index(row, 0);
        auto* instance = static_cast<BaseInstance*>(index.data(InstanceList::InstancePointerRole).value<void*>());
        if (!instance)
            continue;
        QFile file(instance->instanceRoot() + "/mmc-pack.json");
        QStringList parts;
        if (file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024) {
            const auto components = QJsonDocument::fromJson(file.readAll()).object().value("components").toArray();
            const QHash<QString, QString> names{ { "net.minecraft", "Minecraft" },
                                                 { "net.fabricmc.fabric-loader", "Fabric" },
                                                 { "org.quiltmc.quilt-loader", "Quilt" },
                                                 { "net.minecraftforge", "Forge" },
                                                 { "net.neoforged", "NeoForge" } };
            for (const auto& entry : components) {
                const auto component = entry.toObject();
                const auto name = names.value(component.value("uid").toString());
                const auto version = component.value("version").toString();
                if (!name.isEmpty() && !version.isEmpty())
                    parts.append(name + " " + version);
            }
        }
        m_metadata.insert(instance->id(), parts.join(" · "));
    }
}

QString LibraryDelegate::metadata(const QModelIndex& index) const
{
    return m_metadata.value(index.data(InstanceList::InstanceIDRole).toString());
}

QSize LibraryDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    const auto height = QFontMetrics(option.font).height();
    if (m_compact) {
        auto* view = qobject_cast<const QAbstractItemView*>(option.widget);
        return { view ? qMax(1, view->viewport()->width() - 20) : 400, qMax(74, height * 2 + 26) };
    }
    return { 176, 90 + height * 3 };
}

void LibraryDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    auto opt = option;
    initStyleOption(&opt, index);
    painter->save();
    painter->setClipRect(opt.rect);
    const bool selected = opt.state.testFlag(QStyle::State_Selected);
    const bool enabled = opt.state.testFlag(QStyle::State_Enabled);
    const auto group = enabled ? QPalette::Active : QPalette::Disabled;
    const auto foreground = selected ? QPalette::HighlightedText : QPalette::Text;
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, selected ? 25 : opt.state.testFlag(QStyle::State_MouseOver) ? 14 : 5));
    painter->drawRoundedRect(opt.rect.adjusted(2, 3, -2, -3), 12, 12);
    if (selected || opt.state.testFlag(QStyle::State_HasFocus)) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(opt.palette.color(QPalette::Link), 2));
        painter->drawRoundedRect(opt.rect.adjusted(3, 4, -3, -4), 11, 11);
    }
    const auto iconRect = m_compact ? QRect(opt.rect.left() + 12, opt.rect.center().y() - 20, 40, 40)
                                    : QRect(opt.rect.center().x() - 28, opt.rect.top() + 15, 56, 56);
    opt.icon.paint(painter, iconRect, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
    auto textRect = m_compact ? opt.rect.adjusted(66, 8, -12, -8) : opt.rect.adjusted(12, 78, -12, -8);
    auto font = opt.font;
    font.setWeight(QFont::DemiBold);
    painter->setFont(font);
    painter->setPen(opt.palette.color(group, foreground));
    const auto metrics = painter->fontMetrics();
    const auto title = metrics.elidedText(opt.text, Qt::ElideRight, textRect.width());
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignTop, title);
    textRect.setTop(textRect.top() + metrics.height() + 5);
    painter->setFont(opt.font);
    auto* instance = static_cast<BaseInstance*>(index.data(InstanceList::InstancePointerRole).value<void*>());
    if (instance) {
        const auto summary = metadata(index);
        const auto description = summary.isEmpty() ? QObject::tr("Version not loaded") : summary;
        painter->setPen(opt.palette.color(group, QPalette::PlaceholderText));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignTop,
                          painter->fontMetrics().elidedText(description, Qt::ElideRight, textRect.width()));
        if (instance->isRunning() || instance->hasCrashed() || instance->hasVersionBroken()) {
            auto badge = QIcon::fromTheme(instance->isRunning() ? "status-running" : "status-bad");
            badge.paint(painter, QRect(iconRect.right() - 12, iconRect.bottom() - 12, 20, 20));
        }
    }
    painter->restore();
}
void LibraryDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const
{
    editor->setGeometry(m_compact ? option.rect.adjusted(62, 5, -8, -5) : option.rect.adjusted(8, 74, -8, -5));
}
}  // namespace Awake
