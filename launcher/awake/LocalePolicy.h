// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>
#include <QStringList>

namespace Awake {
QStringList supportedLocales();
bool isSupportedLocale(const QString& locale);
QString localeForSystem(QString locale);
}  // namespace Awake
