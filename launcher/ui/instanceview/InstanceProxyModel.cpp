/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "InstanceProxyModel.h"

#include <BaseInstance.h>
#include <icons/IconList.h>
#include "Application.h"
#include "InstanceList.h"
#include "InstanceView.h"
#include "awake/InstanceSearch.h"

#include <QDebug>

InstanceProxyModel::InstanceProxyModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    m_naturalSort.setNumericMode(true);
    m_naturalSort.setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
    // FIXME: use loaded translation as source of locale instead, hook this up to translation changes
    m_naturalSort.setLocale(QLocale::system());
}

QVariant InstanceProxyModel::data(const QModelIndex& index, int role) const
{
    QVariant data = QSortFilterProxyModel::data(index, role);
    if (role == Qt::DecorationRole) {
        return QVariant(APPLICATION->icons()->getIcon(data.toString()));
    }
    return data;
}

bool InstanceProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const QString leftCategory = left.data(InstanceViewRoles::GroupRole).toString();
    const QString rightCategory = right.data(InstanceViewRoles::GroupRole).toString();
    if (leftCategory == rightCategory) {
        return subSortLessThan(left, right);
    } else {
        // FIXME: real group sorting happens in InstanceView::updateGeometries(), see LocaleString
        auto result = leftCategory.localeAwareCompare(rightCategory);
        if (result == 0) {
            return subSortLessThan(left, right);
        }
        return result < 0;
    }
}

bool InstanceProxyModel::subSortLessThan(const QModelIndex& left, const QModelIndex& right) const
{
    BaseInstance* pdataLeft = static_cast<BaseInstance*>(left.internalPointer());
    BaseInstance* pdataRight = static_cast<BaseInstance*>(right.internalPointer());
    const auto pinned = APPLICATION->settings()->get("AwakePinnedInstances").toStringList();
    const bool leftPinned = pinned.contains(pdataLeft->id());
    const bool rightPinned = pinned.contains(pdataRight->id());
    if (leftPinned != rightPinned)
        return leftPinned;
    QString sortMode = APPLICATION->settings()->get("InstSortMode").toString();
    if (sortMode == "LastLaunch") {
        return pdataLeft->lastLaunch() > pdataRight->lastLaunch();
    } else if (sortMode == "Playtime") {
        if (pdataLeft->totalTimePlayed() == pdataRight->totalTimePlayed()) {
            return m_naturalSort.compare(pdataLeft->name(), pdataRight->name()) < 0;
        }
        return pdataLeft->totalTimePlayed() > pdataRight->totalTimePlayed();
    } else {
        return m_naturalSort.compare(pdataLeft->name(), pdataRight->name()) < 0;
    }
}

void InstanceProxyModel::setSearchQuery(const QString& query)
{
    m_searchQuery = query;
    invalidate();
}

void InstanceProxyModel::setPinnedOnly(bool pinnedOnly)
{
    m_pinnedOnly = pinnedOnly;
    invalidate();
}

bool InstanceProxyModel::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    const auto item = sourceModel()->index(row, 0, parent);
    if (m_pinnedOnly &&
        !APPLICATION->settings()->get("AwakePinnedInstances").toStringList().contains(item.data(InstanceList::InstanceIDRole).toString()))
        return false;
    return Awake::matchesInstance(item.data(Qt::DisplayRole).toString(), item.data(InstanceViewRoles::GroupRole).toString(), m_searchQuery);
}
