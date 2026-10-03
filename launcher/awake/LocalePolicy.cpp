// SPDX-License-Identifier: GPL-3.0-only
#include "LocalePolicy.h"

namespace Awake {
QStringList supportedLocales()
{
    return { "en_US", "th", "zh_CN", "zh_TW" };
}

bool isSupportedLocale(const QString& locale)
{
    return supportedLocales().contains(locale);
}

QString localeForSystem(QString locale)
{
    locale.replace('-', '_');
    const auto parts = locale.toLower().split('_');
    if (parts.first() == "th")
        return "th";
    if (parts.first() == "zh") {
        if (parts.contains("hans"))
            return "zh_CN";
        if (parts.contains("hant") || parts.contains("tw") || parts.contains("hk") || parts.contains("mo"))
            return "zh_TW";
        return "zh_CN";
    }
    return "en_US";
}
}  // namespace Awake
