// SPDX-License-Identifier: GPL-3.0-only
#include "InstanceSearch.h"
#include <QStringList>

namespace Awake {
bool matchesInstance(const QString& name, const QString& group, const QString& query)
{
    const auto searchable = name + ' ' + group;
    for (const auto& word : query.simplified().split(' ', Qt::SkipEmptyParts)) {
        if (!searchable.contains(word, Qt::CaseInsensitive))
            return false;
    }
    return true;
}
}  // namespace Awake
