// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ui/themes/DarkTheme.h"

namespace Awake {
bool useRegularUiFont();
class Theme : public DarkTheme {
   public:
    QString id() override;
    QString name() override;
    QPalette colorScheme() override;
    QString appStyleSheet() override;
    QColor fadeColor() override;
};
}  // namespace Awake
