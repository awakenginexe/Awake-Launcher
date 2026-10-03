// SPDX-License-Identifier: GPL-3.0-only
#include "DesignTokens.h"

namespace Awake {
QColor color(Color token)
{
    switch (token) {
        case Color::Background:
            return QColor("#12161c");
        case Color::Elevated:
            return QColor("#1a2028");
        case Color::Secondary:
            return QColor("#222b35");
        case Color::Text:
            return QColor("#e7ecef");
        case Color::SecondaryText:
            return QColor("#aab6c3");
        case Color::DisabledText:
            return QColor("#87929f");
        case Color::Border:
            return QColor("#6b7c90");
        case Color::Accent:
            return QColor("#92cfb2");
        case Color::AccentHover:
            return QColor("#a5e0c3");
        case Color::Success:
            return QColor("#92cfb2");
        case Color::Warning:
            return QColor("#efc17e");
        case Color::Error:
            return QColor("#f59e9e");
        case Color::Selection:
            return QColor("#29483f");
        case Color::Focus:
            return QColor("#a5e0c3");
    }
    return {};
}

QPalette palette()
{
    QPalette result;
    result.setColor(QPalette::Window, color(Color::Background));
    result.setColor(QPalette::WindowText, color(Color::Text));
    result.setColor(QPalette::Base, color(Color::Elevated));
    result.setColor(QPalette::AlternateBase, color(Color::Secondary));
    result.setColor(QPalette::Text, color(Color::Text));
    result.setColor(QPalette::Button, color(Color::Secondary));
    result.setColor(QPalette::ButtonText, color(Color::Text));
    result.setColor(QPalette::Light, color(Color::Border));
    result.setColor(QPalette::Mid, color(Color::Border));
    result.setColor(QPalette::Dark, color(Color::Border));
    result.setColor(QPalette::Shadow, color(Color::Background));
    result.setColor(QPalette::ToolTipBase, color(Color::Secondary));
    result.setColor(QPalette::ToolTipText, color(Color::Text));
    result.setColor(QPalette::PlaceholderText, color(Color::SecondaryText));
    result.setColor(QPalette::Highlight, color(Color::Selection));
    result.setColor(QPalette::HighlightedText, color(Color::Text));
    result.setColor(QPalette::Link, color(Color::Accent));
    result.setColor(QPalette::BrightText, color(Color::Error));
    for (auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText })
        result.setColor(QPalette::Disabled, role, color(Color::DisabledText));
    return result;
}
}  // namespace Awake
