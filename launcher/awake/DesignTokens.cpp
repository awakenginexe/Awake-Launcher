// SPDX-License-Identifier: GPL-3.0-only
#include "DesignTokens.h"

namespace Awake {
QColor color(Color token)
{
    switch (token) {
        case Color::Background:
            return QColor("#060b13");
        case Color::Elevated:
            return QColor("#0b1424");
        case Color::Secondary:
            return QColor("#111f38");
        case Color::Text:
            return QColor("#f8fafc");
        case Color::SecondaryText:
            return QColor("#94a3b8");
        case Color::DisabledText:
            return QColor("#64748b");
        case Color::Border:
            return QColor("#64748b");
        case Color::Accent:
            return QColor("#3b82f6");
        case Color::AccentHover:
            return QColor("#60a5fa");
        case Color::Success:
            return QColor("#38bdf8");
        case Color::Warning:
            return QColor("#f59e0b");
        case Color::Error:
            return QColor("#ef4444");
        case Color::Selection:
            return QColor("#172554");
        case Color::Focus:
            return QColor("#60a5fa");
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
