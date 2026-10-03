// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeTheme.h"
#include <QCoreApplication>
#include "DesignTokens.h"

namespace Awake {
QString Theme::id()
{
    return "awake-dark";
}
QString Theme::name()
{
    return QCoreApplication::translate("AwakeTheme", "Awake Dark");
}
QPalette Theme::colorScheme()
{
    return palette();
}
QColor Theme::fadeColor()
{
    return color(Color::Background);
}
QString Theme::appStyleSheet()
{
    return QString(R"(
        QToolBar { border: none; spacing: 6px; padding: 8px; }
        QMenu, QToolTip { background: %1; color: %2; border: 1px solid %3; }
        QMenu::item { padding: 7px 18px; }
        QMenu::item:selected { background: %4; }
        QLineEdit, QComboBox, QSpinBox { padding: 7px; border: 1px solid %3; border-radius: 5px; }
        QPushButton, QToolButton { padding: 7px 10px; border: 1px solid transparent; border-radius: 5px; }
        QPushButton:hover, QToolButton:hover { background: %5; }
        QPushButton:focus, QToolButton:focus, QLineEdit:focus, QComboBox:focus { border: 1px solid %6; }
        QLabel#awakeHeading { font-size: 23px; font-weight: 600; }
        QLabel#awakeDetailName { font-size: 20px; font-weight: 600; }
        QLabel#awakeMuted { color: %7; }
        QWidget#awakeDetails { background: %1; border-radius: 7px; }
        QToolButton#awakeLaunch { background: %6; color: %8; font-weight: 600; padding: 11px; }
        QToolButton#awakeLaunch:hover { background: %9; }
        QToolButton#awakeLaunch:disabled { background: %5; color: %7; }
        QToolButton#awakeLaunch:focus { border: 2px solid %2; }
        QSplitter::handle { background: transparent; width: 14px; }
    )")
        .arg(color(Color::Elevated).name(), color(Color::Text).name(), color(Color::Border).name(), color(Color::Selection).name(),
             color(Color::Secondary).name(), color(Color::Accent).name(), color(Color::SecondaryText).name(),
             color(Color::Background).name(), color(Color::AccentHover).name());
}
}  // namespace Awake
