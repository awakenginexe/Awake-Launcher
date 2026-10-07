// SPDX-License-Identifier: GPL-3.0-only
#include "AwakeTheme.h"
#include <QCoreApplication>
#include <QFontDatabase>
#include <QLocale>
#include "Application.h"
#include "settings/SettingsObject.h"
#include "DesignTokens.h"

namespace Awake {
bool useRegularUiFont()
{
    auto language = APPLICATION->settings()->get("Language").toString();
    if (language.isEmpty()) language = QLocale().name();
    return language.startsWith("en", Qt::CaseInsensitive) || language.startsWith("th", Qt::CaseInsensitive);
}
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
    static const auto fonts = [] {
        for (const auto& file : {"K2D-Regular.ttf", "K2D-Medium.ttf", "K2D-SemiBold.ttf", "K2D-Bold.ttf"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/awake/fonts/") + file);
        return true;
    }();
    Q_UNUSED(fonts);
#ifdef Q_OS_WIN
    const auto traditional = APPLICATION->settings()->get("Language").toString() == "zh_TW";
    QFontDatabase::setApplicationFallbackFontFamilies(QChar::Script_Han,
        traditional ? QStringList{"Microsoft JhengHei UI", "Microsoft JhengHei", "Microsoft YaHei UI", "Microsoft YaHei"}
                    : QStringList{"Microsoft YaHei UI", "Microsoft YaHei", "Microsoft JhengHei UI", "Microsoft JhengHei"});
#endif
    auto stylesheet = QString(R"(
        /* Global & Dialog Base */
        QWidget {
            font-family: "K2D", "Segoe UI", -apple-system, BlinkMacSystemFont, "Segoe UI Variable Text", sans-serif;
            font-size: 12px;
            color: #f8fafc;
        }
        QDialog, QMainWindow {
            background-color: #060b13;
            color: #f8fafc;
        }
        QDialog#PageDialog, QDialog#NewInstanceDialog, QDialog#SetupWizard, QWizard#SetupWizard {
            background-color: #080f1d;
        }
        PageContainer, QWidget#PageContainer, QWidget#pageContainer {
            background-color: #080f1d;
        }
        QWidget#modalHeaderBar {
            background: rgba(14, 25, 42, 0.70);
            border-bottom: 1px solid rgba(96, 165, 250, 0.16);
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
        }
        QLabel#modalTitleLabel {
            font-size: 13px;
            font-weight: 600;
            color: #f8fafc;
            letter-spacing: 0.3px;
        }
        QPushButton#modalCloseButton {
            background: transparent;
            color: #94a3b8;
            border: none;
            border-radius: 6px;
            font-size: 13px;
            font-weight: bold;
            min-width: 28px;
            max-width: 28px;
            min-height: 28px;
            max-height: 28px;
            padding: 0;
        }
        QPushButton#modalCloseButton:hover {
            background: rgba(239, 68, 68, 0.22);
            color: #f87171;
        }
        QPushButton#modalCloseButton:pressed {
            background: rgba(239, 68, 68, 0.38);
            color: #ffffff;
        }
        QFrame#instanceInfoCard {
            background: rgba(14, 25, 42, 0.65);
            border: 1px solid rgba(96, 165, 250, 0.16);
            border-radius: 10px;
        }
        QFrame#versionFilterCard, QFrame#loaderFilterCard {
            background: rgba(14, 25, 42, 0.50);
            border: 1px solid rgba(96, 165, 250, 0.14);
            border-radius: 8px;
        }
        QLineEdit#versionSearch {
            background: #08101d;
            border: 1px solid rgba(96, 165, 250, 0.20);
            border-radius: 6px;
            padding: 6px 10px;
            color: #f8fafc;
            margin-bottom: 4px;
        }
        QLineEdit#versionSearch:focus {
            border-color: #3b82f6;
            background: #0c182c;
        }
        QToolButton#iconButton {
            background: rgba(18, 32, 54, 0.75);
            border: 1.5px solid rgba(96, 165, 250, 0.25);
            border-radius: 12px;
            padding: 4px;
            min-width: 64px;
            max-width: 64px;
            min-height: 64px;
            max-height: 64px;
        }
        QToolButton#iconButton:hover {
            background: rgba(59, 130, 246, 0.20);
            border-color: rgba(96, 165, 250, 0.55);
        }
        /* Login Hero & Setup Wizard */
        QWizard QPushButton {
            min-width: 84px;
            min-height: 30px;
            padding: 6px 16px;
            border-radius: 6px;
            font-weight: 500;
        }
        QFrame#loginHeroCard {
            background: transparent;
            border: none;
        }
        QLabel#loginBadge {
            background: transparent;
            border: none;
        }
        QLabel#loginTitle {
            font-size: 24px;
            font-weight: 700;
            color: #f8fafc;
        }
        QLabel#loginSubtitle {
            font-size: 13px;
            color: #94a3b8;
            line-height: 1.4;
        }
        QWidget#LoginWizardPage QPushButton#pushButton {
            background: #2563eb;
            color: #ffffff;
            border: 1px solid rgba(255, 255, 255, 0.20);
            border-radius: 8px;
            font-size: 13px;
            font-weight: 600;
            min-height: 44px;
            padding: 0 20px;
        }
        QWidget#LoginWizardPage QPushButton#pushButton:hover {
            background: #1d4ed8;
            border-color: rgba(255, 255, 255, 0.35);
        }
        QWidget#LoginWizardPage QPushButton#pushButton:pressed {
            background: #1e40af;
        }
        QWidget#LoginWizardPage QPushButton#pushButton:focus {
            border: 2px solid #f8fafc;
        }
        QLabel#loginSkipHint {
            color: #94a3b8;
            font-size: 12px;
        }
        Line#line {
            border: none;
            background: rgba(96, 165, 250, 0.14);
            max-height: 1px;
            min-height: 1px;
            margin: 4px 0;
        }
        QToolBar {
            border: none;
            spacing: 6px;
            padding: 8px;
            background: transparent;
        }
        QMenu, QToolTip {
            background: #0b1424;
            color: #f8fafc;
            border: 1px solid rgba(96, 165, 250, 0.16);
            border-radius: 6px;
            padding: 4px;
        }
        QMenu::item {
            padding: 7px 20px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background: rgba(59, 130, 246, 0.20);
            color: #60a5fa;
        }

        /* Settings Page Navigation Sidebar */
        PageView, QListView#pageList {
            background: #080f1c;
            border: none;
            border-right: 1px solid rgba(96, 165, 250, 0.10);
            padding: 8px 6px;
            outline: none;
        }
        PageView::item, QListView#pageList::item {
            padding: 7px 12px;
            border-radius: 6px;
            margin: 2px 2px;
            color: #94a3b8;
            border: 1px solid transparent;
        }
        PageView::item:hover, QListView#pageList::item:hover {
            background: rgba(255, 255, 255, 0.06);
            color: #f8fafc;
        }
        PageView::item:selected, QListView#pageList::item:selected {
            background: rgba(59, 130, 246, 0.18);
            color: #60a5fa;
            font-weight: 600;
            border-left: 3px solid #3b82f6;
        }

        /* Page Headers & Headings */
        QLabel#pageHeader {
            font-size: 16px;
            font-weight: 600;
            color: #f8fafc;
            padding-bottom: 4px;
        }

        /* Group Boxes - Modern Card Appearance */
        QGroupBox {
            font-size: 12px;
            font-weight: 600;
            color: #60a5fa;
            border: 1px solid rgba(96, 165, 250, 0.14);
            border-radius: 8px;
            margin-top: 18px;
            padding-top: 20px;
            padding-bottom: 12px;
            padding-left: 14px;
            padding-right: 14px;
            background: rgba(14, 25, 42, 0.50);
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 12px;
            top: 2px;
            padding: 0 6px;
            color: #60a5fa;
            font-weight: 600;
            background: transparent;
        }
    )") + QString(R"(
        /* Inputs & Controls */
        QLineEdit, QSpinBox, QDoubleSpinBox {
            background: #08101d;
            color: #f8fafc;
            border: 1px solid rgba(96, 165, 250, 0.16);
            border-radius: 6px;
            padding: 6px 10px;
            selection-background-color: #3b82f6;
            selection-color: #ffffff;
        }
        QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover {
            border-color: rgba(96, 165, 250, 0.35);
        }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
            border: 1px solid #3b82f6;
            background: #0c182c;
        }
        QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
            background: rgba(255, 255, 255, 0.02);
            color: #64748b;
            border-color: rgba(255, 255, 255, 0.04);
        }

        /* Combo Boxes */
        QComboBox {
            background: #08101d;
            color: #f8fafc;
            border: 1px solid rgba(96, 165, 250, 0.16);
            border-radius: 6px;
            padding: 5px 10px;
            min-height: 22px;
        }
        QComboBox:hover {
            border-color: rgba(96, 165, 250, 0.35);
        }
        QComboBox:focus {
            border: 1px solid #3b82f6;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 22px;
            border-left: none;
        }
        QComboBox QAbstractItemView {
            background: #0b1424;
            color: #f8fafc;
            border: 1px solid rgba(96, 165, 250, 0.20);
            selection-background-color: rgba(59, 130, 246, 0.22);
            selection-color: #60a5fa;
            border-radius: 6px;
            padding: 4px;
            outline: none;
        }

        /* Buttons */
        QPushButton {
            background: rgba(255, 255, 255, 0.07);
            color: #f8fafc;
            border: 1px solid rgba(255, 255, 255, 0.10);
            border-radius: 6px;
            padding: 6px 14px;
            font-weight: 500;
        }
        QPushButton:hover {
            background: rgba(255, 255, 255, 0.13);
            border-color: rgba(96, 165, 250, 0.30);
            color: #ffffff;
        }
        QPushButton:pressed {
            background: rgba(255, 255, 255, 0.05);
        }
        QPushButton:default {
            background: #2563eb;
            color: #ffffff;
            font-weight: 600;
            border: 1px solid #3b82f6;
        }
        QPushButton:default:hover {
            background: #3b82f6;
            border-color: #60a5fa;
        }
        QPushButton:disabled {
            background: rgba(255, 255, 255, 0.02);
            color: #64748b;
            border-color: transparent;
        }
        QToolButton {
            background: transparent;
            color: #f8fafc;
            border: 1px solid transparent;
            border-radius: 6px;
            padding: 5px;
        }
        QToolButton:hover {
            background: rgba(255, 255, 255, 0.08);
        }

        /* Checkboxes & Radio Buttons */
        QCheckBox, QRadioButton {
            spacing: 8px;
            color: #cbd5e1;
        }
        QCheckBox:hover, QRadioButton:hover {
            color: #f8fafc;
        }
        QCheckBox:disabled, QRadioButton:disabled {
            color: #64748b;
        }
        QCheckBox::indicator {
            width: 15px;
            height: 15px;
            border-radius: 4px;
            border: 1px solid rgba(96, 165, 250, 0.35);
            background: rgba(14, 25, 42, 0.70);
        }
        QCheckBox::indicator:hover {
            border-color: #3b82f6;
            background: rgba(59, 130, 246, 0.15);
        }
        QCheckBox::indicator:checked {
            background: #2563eb;
            border-color: #60a5fa;
            image: url(:/icons/flat/scalable/status-good.svg);
        }
        QRadioButton::indicator {
            width: 15px;
            height: 15px;
            border-radius: 8px;
            border: 1px solid rgba(96, 165, 250, 0.35);
            background: rgba(14, 25, 42, 0.70);
        }
        QRadioButton::indicator:hover {
            border-color: #3b82f6;
            background: rgba(59, 130, 246, 0.15);
        }
        QRadioButton::indicator:checked {
            background: #60a5fa;
            border: 4px solid #08101d;
            border-radius: 8px;
        }

        /* Tab Widgets */
        QTabWidget::pane {
            border: 1px solid rgba(96, 165, 250, 0.14);
            border-radius: 8px;
            background: rgba(14, 25, 42, 0.38);
            padding: 8px;
        }
        QTabBar::tab {
            background: transparent;
            color: #94a3b8;
            padding: 7px 16px;
            border-bottom: 2px solid transparent;
            font-weight: 500;
        }
        QTabBar::tab:hover {
            color: #f8fafc;
        }
        QTabBar::tab:selected {
            color: #60a5fa;
            border-bottom: 2px solid #3b82f6;
            font-weight: 600;
        }

        /* Trees & Tables */
        QHeaderView::section {
            background: #080f1c;
            color: #94a3b8;
            padding: 6px 10px;
            border: none;
            border-right: 1px solid rgba(96, 165, 250, 0.08);
            border-bottom: 1px solid rgba(96, 165, 250, 0.12);
            font-weight: 600;
            font-size: 11px;
        }
        QTreeView, QTableView {
            background: #060b13;
            color: #f8fafc;
            border: 1px solid rgba(96, 165, 250, 0.12);
            border-radius: 6px;
            gridline-color: rgba(255, 255, 255, 0.04);
            selection-background-color: rgba(59, 130, 246, 0.20);
            selection-color: #60a5fa;
            outline: none;
        }
        QTreeView::item, QTableView::item {
            padding: 5px 8px;
        }
        QTreeView::item:hover, QTableView::item:hover {
            background: rgba(255, 255, 255, 0.06);
        }
        QTreeView::item:selected, QTableView::item:selected {
            background: rgba(59, 130, 246, 0.20);
            color: #60a5fa;
        }

        /* Scrollbars */
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: rgba(255, 255, 255, 0.16);
            border-radius: 4px;
            min-height: 28px;
        }
        QScrollBar::handle:vertical:hover {
            background: rgba(59, 130, 246, 0.50);
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollBar:horizontal {
            background: transparent;
            height: 8px;
            margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: rgba(255, 255, 255, 0.16);
            border-radius: 4px;
            min-width: 28px;
        }
        QScrollBar::handle:horizontal:hover {
            background: rgba(59, 130, 246, 0.50);
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: transparent;
        }

        /* Dialog Button Box */
        QDialogButtonBox {
            dialogbuttonbox-buttons-have-icons: 0;
        }
        QDialogButtonBox QPushButton {
            min-width: 78px;
            padding: 6px 16px;
        }

        /* Splitters */
        QSplitter::handle {
            background: rgba(255, 255, 255, 0.06);
            width: 1px;
        }

        /* Retain existing labels / fallback widget definitions */
        QLabel { background: transparent; }
        QLabel#awakeHeading { font-size: 13px; font-weight: 600; color: #f8fafc; }
        QLabel#awakeDetailName { font-size: 40px; font-weight: 700; }
        QLabel#awakeWordmark { font-size: 17px; font-weight: 700; letter-spacing: 2px; }
        QLabel#awakeMark { font-size: 25px; font-weight: 700; color: #ffffff; background: #2563eb; border-radius: 10px; }
        QLabel#awakeWorldDescription { font-size: 14px; color: #cbd5e1; }
        QLabel#awakeRuntime, QLabel#awakeArtworkCaption { font-size: 11px; color: #94a3b8; }
        QLabel#awakeEmptyHint { font-size: 12px; color: #94a3b8; padding-top: 6px; }
        QWidget#awakeNavigation QAbstractItemView { background: transparent; border: none; }
        QLineEdit#awakeSearch { background: rgba(255,255,255,14); border: 1px solid rgba(255,255,255,28); border-radius: 10px; padding: 7px 9px; }
        QLineEdit#awakeSearch:hover { background: rgba(255,255,255,19); border-color: rgba(96,165,250,45); }
        QLineEdit#awakeSearch:focus { background: rgba(255,255,255,22); border: 1px solid #3b82f6; }
        QWidget#awakeNavigation QComboBox { background: rgba(255,255,255,10); border: 1px solid rgba(255,255,255,24); border-radius: 8px; padding: 6px 8px; }
        QWidget#awakeNavigation QComboBox:hover { background: rgba(255,255,255,16); border-color: rgba(96,165,250,40); }
        QWidget#awakeNavigation QCheckBox, QWidget#awakeLaunchDock QCheckBox { color: #94a3b8; font-size: 11px; }
        QLabel#awakeMuted { color: #94a3b8; }
        QWidget#awakeDetails { background: #0b1424; border-radius: 7px; }
        QToolButton#awakeLaunch { background: #2563eb; color: #ffffff; font-weight: 600; padding: 11px; }
        QToolButton#awakeLaunch:hover { background: #3b82f6; }
        QToolButton#awakeLaunch:disabled { background: #111f38; color: #64748b; }
        QToolButton#awakeLaunch:focus { border: 2px solid #f8fafc; }
        QToolButton#btnPrimaryLaunch {
            background: #2563eb;
            color: #ffffff;
            font-weight: 600;
            border: 1px solid rgba(255, 255, 255, 0.20);
            border-radius: 6px;
            padding: 5px 14px;
        }
        QToolButton#btnPrimaryLaunch:hover {
            background: #3b82f6;
            border-color: rgba(255, 255, 255, 0.35);
        }
        QToolButton#btnPrimaryLaunch:disabled {
            background: #111f38;
            color: #64748b;
            border-color: transparent;
        }
        QToolButton#btnPrimaryLaunch:focus {
            border: 2px solid #f8fafc;
        }
        QWidget#awakeOuterContainer {
            background-color: #060b13;
        }
    )") + QString(R"(
        /* AwakeTitleBar - Discord-style custom title bar */
        QWidget#awakeTitleBar {
            background-color: #060b13;
            border-bottom: 1px solid rgba(96, 165, 250, 0.14);
        }
        QLabel#titleBarLogo {
              margin: 0;
        }
        QLabel#titleBarText {
            font-size: 11px;
            font-weight: 600;
            color: #94a3b8;
            letter-spacing: 0.4px;
        }
        /* MacOS Traffic Light Window Controls */
        QPushButton#macMinBtn, QPushButton#macMaxBtn, QPushButton#macCloseBtn {
            min-width: 11px;
            max-width: 11px;
            min-height: 11px;
            max-height: 11px;
            margin: 0;
            padding: 0;
        }
        QPushButton#macMinBtn {
            background-color: #f59e0b;
            border: 1px solid rgba(0, 0, 0, 0.25);
            border-radius: 6px;
            font-size: 9px;
            font-weight: bold;
            color: transparent;
            text-align: center;
            padding: 0;
        }
        QPushButton#macMinBtn:hover {
            background-color: #fbbf24;
            color: rgba(0, 0, 0, 0.70);
        }
        QPushButton#macMaxBtn {
            background-color: #10b981;
            border: 1px solid rgba(0, 0, 0, 0.25);
            border-radius: 6px;
            font-size: 9px;
            font-weight: bold;
            color: transparent;
            text-align: center;
            padding: 0;
        }
        QPushButton#macMaxBtn:hover {
            background-color: #34d399;
            color: rgba(0, 0, 0, 0.70);
        }
        QPushButton#macCloseBtn {
            background-color: #ef4444;
            border: 1px solid rgba(0, 0, 0, 0.25);
            border-radius: 6px;
            font-size: 9px;
            font-weight: bold;
            color: transparent;
            text-align: center;
            padding: 0;
        }
        QPushButton#macCloseBtn:hover {
            background-color: #f87171;
            color: rgba(0, 0, 0, 0.70);
        }

        /* ProgressDialog - Modern Download Status Popup */
        QDialog#ProgressDialog {
            background-color: #080f1d;
            border: 1px solid rgba(96, 165, 250, 0.22);
            border-radius: 12px;
        }
        QLabel#globalStatusLabel {
            font-size: 13px;
            font-weight: 600;
            color: #f8fafc;
        }
        QLabel#globalStatusDetailsLabel {
            font-size: 11px;
            color: #94a3b8;
        }
        QProgressBar#globalProgressBar, SubTaskProgressBar QProgressBar {
            background-color: rgba(15, 23, 42, 0.85);
            border: 1px solid rgba(96, 165, 250, 0.18);
            border-radius: 8px;
            text-align: center;
            color: #f8fafc;
            font-size: 11px;
            font-weight: 600;
            min-height: 18px;
            max-height: 18px;
        }
        QProgressBar#globalProgressBar::chunk, SubTaskProgressBar QProgressBar::chunk {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0,
                stop:0 #06b6d4, stop:0.5 #3b82f6, stop:1 #6366f1);
            border-radius: 7px;
        }
        QPushButton#skipButton {
            background-color: rgba(239, 68, 68, 0.14);
            border: 1px solid rgba(239, 68, 68, 0.35);
            border-radius: 8px;
            color: #fca5a5;
            font-size: 12px;
            font-weight: 600;
            padding: 7px 18px;
        }
        QPushButton#skipButton:hover {
            background-color: rgba(239, 68, 68, 0.28);
            border-color: rgba(239, 68, 68, 0.60);
            color: #ffffff;
        }
        QPushButton#skipButton:pressed {
            background-color: rgba(239, 68, 68, 0.42);
        }
        QScrollArea#taskProgressScrollArea {
            background: transparent;
            border: 1px solid rgba(96, 165, 250, 0.10);
            border-radius: 8px;
        }
        QWidget#taskProgressContainer {
            background: transparent;
        }
        SubTaskProgressBar {
            background: rgba(14, 25, 42, 0.50);
            border-radius: 6px;
            padding: 4px;
        }
        SubTaskProgressBar QLabel {
            font-size: 11px;
            color: #cbd5e1;
        }

        /* Modern Dark Glass Menus */
        QMenu {
            background-color: #0b1424;
            border: 1px solid rgba(96, 165, 250, 0.20);
            border-radius: 8px;
            padding: 6px;
            color: #f8fafc;
        }
        QMenu::item {
            padding: 6px 20px 6px 12px;
            border-radius: 5px;
            color: #cbd5e1;
            font-size: 12px;
        }
        QMenu::item:selected {
            background-color: rgba(59, 130, 246, 0.20);
            color: #ffffff;
        }
        QMenu::item:disabled {
            color: #475569;
        }
        QMenu::separator {
            height: 1px;
            background: rgba(96, 165, 250, 0.12);
            margin: 4px 6px;
        }
    )");
    if (useRegularUiFont()) {
        stylesheet += "QWidget { font-family: K2D; font-weight: 400; }";
    }
    return stylesheet;
}
}  // namespace Awake
