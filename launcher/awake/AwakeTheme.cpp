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
        QLabel { background: transparent; }
        QLabel#awakeHeading { font-size: 20px; font-weight: 600; }
        QLabel#awakeDetailName { font-size: 42px; font-weight: 700; }
        QLabel#awakeWordmark { font-size: 18px; font-weight: 700; letter-spacing: 3px; }
        QLabel#awakeMark { font-size: 25px; font-weight: 700; color: #102128; background: #a5e1ca; border-radius: 10px; }
        QLabel#awakeWorldDescription { font-size: 15px; color: #d2dee7; }
        QLabel#awakeRuntime, QLabel#awakeArtworkCaption { font-size: 11px; color: #c3d1de; }
        QLabel#awakeEmptyHint { font-size: 13px; color: #bac9d7; padding-top: 18px; }
        QWidget#awakeNavigation QAbstractItemView { background: transparent; border: none; }
        QLineEdit#awakeSearch { background: rgba(255,255,255,12); border: 1px solid #7d8ea4; border-radius: 10px; padding: 7px; }
        QWidget#awakeNavigation QComboBox { background: rgba(255,255,255,12); border: 1px solid #7d8ea4; border-radius: 8px; padding: 6px; }
        QWidget#awakeNavigation QCheckBox, QWidget#awakeLaunchDock QCheckBox { color: #c3d1de; font-size: 11px; }
        QScrollBar:vertical { background: transparent; width: 7px; }
        QScrollBar::handle:vertical { background: #657584; border-radius: 3px; min-height: 30px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
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
