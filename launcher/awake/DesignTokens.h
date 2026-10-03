// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QColor>
#include <QPalette>

namespace Awake {
enum class Color {
    Background,
    Elevated,
    Secondary,
    Text,
    SecondaryText,
    DisabledText,
    Border,
    Accent,
    AccentHover,
    Success,
    Warning,
    Error,
    Selection,
    Focus
};
QColor color(Color token);
QPalette palette();
}  // namespace Awake
