// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Authors: Andrew Okin
 *               Peterix
 *               Orochimarufan <orochimarufan.x3@gmail.com>
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "Application.h"
#include "BuildConfig.h"
#include "minecraft/auth/OAuthCallback.h"
#include "FileSystem.h"

#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QPushButton>
#include <QVBoxLayout>
#include "ui/dialogs/AwakePopupDialog.h"
#include "awake/AwakeTheme.h"
#include <QWidget>
#include <QWidgetAction>
#include <memory>

#include <BaseInstance.h>
#include <BuildConfig.h>
#include <DesktopServices.h>
#include <InstanceList.h>
#include <MMCZip.h>
#include <icons/IconList.h>
#include <java/JavaInstallList.h>
#include <java/JavaUtils.h>
#include <launch/LaunchTask.h>
#include <minecraft/MinecraftInstance.h>
#include <minecraft/auth/AccountList.h>
#include <net/ApiRequest.h>
#include <net/NetJob.h>
#include <news/NewsChecker.h>
#include <tools/BaseProfiler.h>
#include <updater/ExternalUpdater.h>
#include "InstanceWindow.h"

#include "ui/GuiUtil.h"
#include "ui/ViewLogWindow.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/CopyInstanceDialog.h"
#include "ui/dialogs/CreateShortcutDialog.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ExportInstanceDialog.h"
#include "ui/dialogs/ExportPackDialog.h"
#include "ui/dialogs/IconPickerDialog.h"
#include "ui/dialogs/ImportResourceDialog.h"
#include "ui/dialogs/MSALoginDialog.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "ui/dialogs/NewsDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/skins/SkinManageDialog.h"
#include "ui/instanceview/InstanceDelegate.h"
#include "ui/instanceview/InstanceProxyModel.h"
#include "ui/instanceview/InstanceView.h"
#include "ui/themes/ITheme.h"
#include "ui/themes/ThemeManager.h"
#include "ui/widgets/LabeledToolButton.h"
#include "ui/widgets/PageContainer.h"

#include "minecraft/PackProfile.h"
#include "minecraft/VersionFile.h"
#include "minecraft/WorldList.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "minecraft/mod/TexturePackFolderModel.h"
#include "minecraft/mod/tasks/LocalResourceParse.h"

#include "modplatform/ModIndex.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/flame/FlameModIndex.h"
#include "modplatform/modrinth/ModrinthAPI.h"

#include "KonamiCode.h"
#include "awake/LibraryDelegate.h"
#include "awake/LibraryWidget.h"
#ifdef AWAKE_WEB_ENABLED
#include "awake/web/AwakeWebBridge.h"
#include "awake/web/AwakePackCatalog.h"
#include "awake/web/AwakeWebPolicy.h"
#include "InstanceImportTask.h"
#include "awake/web/AwakeWebShell.h"
#endif

#include "ui/widgets/AwakeTitleBar.h"
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif

#include "InstanceCopyTask.h"
#include "InstanceDirUpdate.h"

#include "Json.h"

#include "MMCTime.h"

namespace {
QString profileInUseFilter(const QString& profile, bool used)
{
    if (used) {
        return QObject::tr("%1 (in use)").arg(profile);
    } else {
        return profile;
    }
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
#ifdef AWAKE_WEB_ENABLED
    m_webMode = qEnvironmentVariable("AWAKE_FRONTEND") != "widgets";
#endif
    ui->setupUi(this);

    setWindowIcon(APPLICATION->logo());
    setWindowTitle(APPLICATION->applicationDisplayName());
#ifndef QT_NO_ACCESSIBILITY
    setAccessibleName(BuildConfig.LAUNCHER_DISPLAYNAME);
#endif

    // instance toolbar stuff
    {
        // Qt doesn't like vertical moving toolbars, so we have to force them...
        // See https://github.com/PolyMC/PolyMC/issues/493
        connect(ui->instanceToolBar, &QToolBar::orientationChanged, this,
                [this](Qt::Orientation) { ui->instanceToolBar->setOrientation(Qt::Vertical); });

        // if you try to add a widget to a toolbar in a .ui file
        // qt designer will delete it when you save the file >:(
        changeIconButton = new LabeledToolButton(this);
        changeIconButton->setObjectName(QStringLiteral("changeIconButton"));
        changeIconButton->setIcon(QIcon::fromTheme("news"));
        changeIconButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(changeIconButton, &QToolButton::clicked, this, &MainWindow::on_actionChangeInstIcon_triggered);
        ui->instanceToolBar->insertWidgetBefore(ui->actionLaunchInstance, changeIconButton);

        renameButton = new LabeledToolButton(this);
        renameButton->setObjectName(QStringLiteral("renameButton"));
        renameButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(renameButton, &QToolButton::clicked, this, &MainWindow::on_actionRenameInstance_triggered);
        ui->instanceToolBar->insertWidgetBefore(ui->actionLaunchInstance, renameButton);

        ui->instanceToolBar->insertSeparator(ui->actionLaunchInstance);
    }

    // set the menu for the folders help, accounts, and export tool buttons
    {
        auto foldersMenuButton = dynamic_cast<QToolButton*>(ui->mainToolBar->widgetForAction(ui->actionFoldersButton));
        ui->actionFoldersButton->setMenu(ui->foldersMenu);
        foldersMenuButton->setPopupMode(QToolButton::InstantPopup);

        helpMenuButton = dynamic_cast<QToolButton*>(ui->mainToolBar->widgetForAction(ui->actionHelpButton));
        ui->actionHelpButton->setMenu(new QMenu(this));
        ui->actionHelpButton->menu()->addActions(ui->helpMenu->actions());
        ui->actionHelpButton->menu()->removeAction(ui->actionCheckUpdate);
        helpMenuButton->setPopupMode(QToolButton::InstantPopup);

        auto accountMenuButton = dynamic_cast<QToolButton*>(ui->mainToolBar->widgetForAction(ui->actionAccountsButton));
        accountMenuButton->setPopupMode(QToolButton::InstantPopup);

        auto exportInstanceMenu = new QMenu(this);
        exportInstanceMenu->addAction(ui->actionExportInstanceZip);
        exportInstanceMenu->addAction(ui->actionExportInstanceMrPack);
        exportInstanceMenu->addAction(ui->actionExportInstanceFlamePack);
        ui->actionExportInstance->setMenu(exportInstanceMenu);
    }

    // hide, disable and show stuff
    {
        ui->actionReportBug->setVisible(!BuildConfig.BUG_TRACKER_URL.isEmpty());
        ui->actionMATRIX->setVisible(!BuildConfig.MATRIX_URL.isEmpty());
        ui->actionDISCORD->setVisible(!BuildConfig.DISCORD_URL.isEmpty());
        ui->actionREDDIT->setVisible(!BuildConfig.SUBREDDIT_URL.isEmpty());

        ui->actionCheckUpdate->setVisible(APPLICATION->updaterEnabled());

#ifndef Q_OS_MAC
        ui->actionAddToPATH->setVisible(false);
#endif

        // disabled until we have an instance selected
        ui->instanceToolBar->setEnabled(false);
        setInstanceActionsEnabled(false);

        // add a close button at the end of the main toolbar when running on gamescope / steam deck
        // this is only needed on gamescope because it defaults to an X11/XWayland session and
        // does not implement decorations
        if (qgetenv("XDG_CURRENT_DESKTOP") == "gamescope") {
            ui->mainToolBar->addAction(ui->actionCloseWindow);
        }

        ui->actionViewJavaFolder->setEnabled(BuildConfig.JAVA_DOWNLOADER_ENABLED);
    }

    {  // logs viewing
        connect(ui->actionViewLog, &QAction::triggered, this, [] { APPLICATION->showLogWindow(); });
    }

    // add the toolbar toggles to the view menu
    ui->viewMenu->addAction(ui->instanceToolBar->toggleViewAction());
    ui->viewMenu->addAction(ui->newsToolBar->toggleViewAction());

    updateThemeMenu();
    updateMainToolBar();
    ui->actionMATRIX->setVisible(!BuildConfig.MATRIX_URL.isEmpty());
    ui->actionDISCORD->setVisible(!BuildConfig.DISCORD_URL.isEmpty());
    ui->actionREDDIT->setVisible(!BuildConfig.SUBREDDIT_URL.isEmpty());
    if (BuildConfig.NEWS_RSS_URL.isEmpty()) {
        ui->newsToolBar->hide();
        ui->newsToolBar->toggleViewAction()->setVisible(false);
    }
    // OSX magic.
    setUnifiedTitleAndToolBarOnMac(true);

    // Global shortcuts
    {
        // you can't set QKeySequence::StandardKey shortcuts in qt designer >:(
        ui->actionAddInstance->setShortcut(QKeySequence::New);
        ui->actionSettings->setShortcut(QKeySequence::Preferences);
        ui->actionUndoTrashInstance->setShortcut(QKeySequence::Undo);
        ui->actionDeleteInstance->setShortcuts({ QKeySequence(tr("Backspace")), QKeySequence::Delete });
        ui->actionCloseWindow->setShortcut(QKeySequence::Close);
        connect(ui->actionCloseWindow, &QAction::triggered, APPLICATION, &Application::closeCurrentWindow);

        // FIXME: This is kinda weird. and bad. We need some kind of managed shutdown.
        auto q = new QShortcut(QKeySequence::Quit, this);
        connect(q, &QShortcut::activated, APPLICATION, &Application::quit);
    }

    // Konami Code
    {
        secretEventFilter = new KonamiCode(this);
        connect(secretEventFilter, &KonamiCode::triggered, this, &MainWindow::konamiTriggered);
    }

    // Add the news label to the news toolbar.
    {
        m_newsChecker.reset(new NewsChecker(APPLICATION->network(), BuildConfig.NEWS_RSS_URL));
        newsLabel = new QToolButton();
        newsLabel->setIcon(QIcon::fromTheme("news"));
        newsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        newsLabel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        newsLabel->setFocusPolicy(Qt::NoFocus);
        ui->newsToolBar->insertWidget(ui->actionMoreNews, newsLabel);

        connect(newsLabel, &QAbstractButton::clicked, this, &MainWindow::newsButtonClicked);
        connect(m_newsChecker.get(), &NewsChecker::newsLoaded, this, &MainWindow::updateNewsLabel);
        updateNewsLabel();
    }

    // Empty-library initialization can clear the selection immediately.
    m_statusLeft = new QLabel(tr("No instance selected"), this);
    m_statusCenter = new QLabel(tr("Total playtime: 0s"), this);
    statusBar()->addPermanentWidget(m_statusLeft, 1);
    statusBar()->addPermanentWidget(m_statusCenter, 0);

    // Create the instance list widget
    {
        view = new InstanceView(ui->centralWidget);

        view->setSelectionMode(QAbstractItemView::SingleSelection);
        // FIXME: leaks ListViewDelegate
        auto delegate = new Awake::LibraryDelegate(this);
        delegate->bindMetadata(APPLICATION->instances());
        view->setItemDelegate(delegate);
        view->setFrameShape(QFrame::NoFrame);
        // do not show ugly blue border on the mac
        view->setAttribute(Qt::WA_MacShowFocusRect, false);
        connect(delegate, &ListViewDelegate::textChanged, this, [this](QString before, QString after) {
            if (auto newRoot = askToUpdateInstanceDirName(m_selectedInstance, before, after, this); !newRoot.isEmpty()) {
                auto oldID = m_selectedInstance->id();
                auto newID = QFileInfo(newRoot).fileName();
                auto pinned = APPLICATION->settings()->get("AwakePinnedInstances").toStringList();
                if (pinned.removeAll(oldID) > 0) {
                    pinned.append(newID);
                    APPLICATION->settings()->set("AwakePinnedInstances", pinned);
                }
                QString origGroup(APPLICATION->instances()->getInstanceGroup(oldID));
                bool syncGroup = origGroup != GroupId() && oldID != newID;
                if (syncGroup)
                    APPLICATION->instances()->setInstanceGroup(oldID, GroupId());

                refreshInstances();
                setSelectedInstanceById(newID);

                if (syncGroup)
                    APPLICATION->instances()->setInstanceGroup(newID, origGroup);
            }
        });

        view->installEventFilter(this);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested, this, &MainWindow::showInstanceContextMenu);
        connect(view, &InstanceView::droppedURLs, this, &MainWindow::processURLs, Qt::QueuedConnection);

        proxymodel = new InstanceProxyModel(this);
        proxymodel->setSourceModel(APPLICATION->instances());
        proxymodel->sort(0);
        connect(proxymodel, &InstanceProxyModel::dataChanged, this, &MainWindow::instanceDataChanged);

        view->setModel(proxymodel);
        view->setSourceOfGroupCollapseStatus(
            [](const QString& groupName) -> bool { return APPLICATION->instances()->isGroupCollapsed(groupName); });
        connect(view, &InstanceView::groupStateChanged, APPLICATION->instances(), &InstanceList::on_GroupStateChanged);
        auto* applicationMenu = new QMenu(this);
        applicationMenu->addMenu(ui->fileMenu);
        applicationMenu->addMenu(ui->editMenu);
        applicationMenu->addMenu(ui->viewMenu);
        applicationMenu->addMenu(ui->foldersMenu);
        applicationMenu->addMenu(ui->helpMenu);
        applicationMenu->addSeparator();
        auto* reduceMotion = applicationMenu->addAction(tr("Reduce motion"));
        reduceMotion->setObjectName("awakeReduceMotion");
        reduceMotion->setCheckable(true);
        reduceMotion->setChecked(APPLICATION->settings()->get("AwakeReduceMotion").toBool());
        m_library = new Awake::LibraryWidget(
            view,
            { ui->actionRenameInstance, ui->actionChangeInstIcon, ui->actionChangeInstGroup, ui->actionCopyInstance,
              ui->actionExportInstance, ui->actionCreateInstanceShortcut, ui->actionKillInstance, ui->actionDeleteInstance },
            ui->actionLaunchInstance, ui->actionAddInstance, ui->actionEditInstance, ui->actionViewSelectedInstFolder,
            ui->actionSettings, ui->actionAccountsButton, applicationMenu, ui->centralWidget);
        m_library->setReducedMotion(reduceMotion->isChecked());
        connect(reduceMotion, &QAction::toggled, this, [this](bool reduced) {
            APPLICATION->settings()->set("AwakeReduceMotion", reduced);
            m_library->setReducedMotion(reduced);
        });
        ui->horizontalLayout->addWidget(m_library);
        if (m_webMode)
            m_library->hide();
        ui->mainToolBar->removeAction(ui->actionAddInstance);
        ui->instanceToolBar->hide();

        const bool compact = APPLICATION->settings()->get("AwakeCompactLibrary").toBool();
        delegate->setCompact(compact);
        view->setCompact(compact);
        m_library->setViewMode(compact);
        m_library->setSortMode(APPLICATION->settings()->get("InstSortMode").toString());
        connect(m_library, &Awake::LibraryWidget::compactChanged, this, [this, delegate](bool compact) {
            delegate->setCompact(compact);
            view->setCompact(compact);
            APPLICATION->settings()->set("AwakeCompactLibrary", compact);
        });
        connect(m_library, &Awake::LibraryWidget::searchChanged, proxymodel, &InstanceProxyModel::setSearchQuery);
        connect(m_library, &Awake::LibraryWidget::pinnedOnlyChanged, proxymodel, &InstanceProxyModel::setPinnedOnly);
        connect(m_library, &Awake::LibraryWidget::sortChanged, this, [this](const QString& mode) {
            APPLICATION->settings()->set("InstSortMode", mode);
            proxymodel->invalidate();
        });
        connect(m_library, &Awake::LibraryWidget::pinChanged, this, [this](bool pinned) {
            if (!m_selectedInstance)
                return;
            const auto id = m_selectedInstance->id();
            auto ids = APPLICATION->settings()->get("AwakePinnedInstances").toStringList();
            ids.removeAll(id);
            if (pinned)
                ids.append(id);
            APPLICATION->settings()->set("AwakePinnedInstances", ids);
            proxymodel->invalidate();
            setSelectedInstanceById(id);
        });
        auto updateCount = [this] {
            const auto visible = proxymodel->rowCount();
            const auto total = APPLICATION->instances()->count();
            m_library->setResultCount(visible, total);
            view->setFilteredEmpty(total > 0);
            if (visible == 0)
                instanceChanged(QModelIndex(), QModelIndex());
        };
        connect(proxymodel, &QAbstractItemModel::rowsInserted, this, updateCount);
        connect(proxymodel, &QAbstractItemModel::rowsRemoved, this, updateCount);
        connect(proxymodel, &QAbstractItemModel::modelReset, this, updateCount);
        connect(proxymodel, &QAbstractItemModel::layoutChanged, this, updateCount);
        updateCount();
        auto* find = new QShortcut(QKeySequence::Find, this);
        connect(find, &QShortcut::activated, this, [this] {
#ifdef AWAKE_WEB_ENABLED
            if (m_webMode && m_webShell) {
                m_webShell->focusSearch();
                return;
            }
#endif
            m_library->focusSearch();
        });
    }
    // The cat background
    {
        // set the cat action priority here so you can still see the action in qt designer
        ui->actionCAT->setPriority(QAction::LowPriority);
        updateCatState();
        connect(ui->actionCAT, &QAction::toggled, this, &MainWindow::onCatToggled);
        connect(APPLICATION, &Application::currentCatChanged, this, &MainWindow::onCatChanged);
    }

    // Togglable status bar
    {
        bool statusBarVisible = APPLICATION->settings()->get("StatusBarVisible").toBool();
        ui->actionToggleStatusBar->setChecked(statusBarVisible);
        connect(ui->actionToggleStatusBar, &QAction::toggled, this, &MainWindow::setStatusBarVisibility);
        setStatusBarVisibility(statusBarVisible);
    }

    // Lock toolbars
    {
        bool toolbarsLocked = APPLICATION->settings()->get("ToolbarsLocked").toBool();
        ui->actionLockToolbars->setChecked(toolbarsLocked);
        connect(ui->actionLockToolbars, &QAction::toggled, this, &MainWindow::lockToolbars);
        lockToolbars(toolbarsLocked);
    }
    // start instance when double-clicked
    connect(view, &InstanceView::activated, this, &MainWindow::instanceActivated);

    // track the selection -- update the instance toolbar
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::instanceChanged);

    // track icon changes and update the toolbar!
    connect(APPLICATION->icons(), &IconList::iconUpdated, this, &MainWindow::iconUpdated);

    // model reset -> selection is invalid. All the instance pointers are wrong.
    connect(APPLICATION->instances(), &InstanceList::dataIsInvalid, this, &MainWindow::selectionBad);

    // handle newly added instances
    connect(APPLICATION->instances(), &InstanceList::instanceSelectRequest, this, &MainWindow::instanceSelectRequest);

    // When the global settings page closes, we want to know about it and update our state
    connect(APPLICATION, &Application::globalSettingsApplied, this, &MainWindow::globalSettingsClosed);

    // Add "manage accounts" button, right align
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->mainToolBar->insertWidget(ui->actionAccountsButton, spacer);

    // Use undocumented property... https://stackoverflow.com/questions/7121718/create-a-scrollbar-in-a-submenu-qt
    ui->accountsMenu->setStyleSheet("QMenu { menu-scrollable: 1; }");

    repopulateAccountsMenu();

    // Update the menu when the active account changes.
    // Shouldn't have to use lambdas here like this, but if I don't, the compiler throws a fit.
    // Template hell sucks...
    connect(APPLICATION->accounts(), &AccountList::defaultAccountChanged, this, [this] { defaultAccountChanged(); });
    connect(APPLICATION->accounts(), &AccountList::listActivityChanged, this, [this] { defaultAccountChanged(); });
    connect(APPLICATION->accounts(), &AccountList::listChanged, this, [this] { defaultAccountChanged(); });

    // Show initial account
    defaultAccountChanged();

    // TODO: refresh accounts here?
    // auto accounts = APPLICATION->accounts();

    // load the news
    {
        m_newsChecker->reloadNews();
        updateNewsLabel();
    }

    if (APPLICATION->updaterEnabled()) {
        bool updatesAllowed = APPLICATION->updatesAreAllowed();
        updatesAllowedChanged(updatesAllowed);

        connect(ui->actionCheckUpdate, &QAction::triggered, this, &MainWindow::checkForUpdates);

        // set up the updater object.
        auto updater = APPLICATION->updater();

        if (updater) {
            connect(updater, &ExternalUpdater::canCheckForUpdatesChanged, this, &MainWindow::updatesAllowedChanged);
        }
    }

    connect(ui->actionUndoTrashInstance, &QAction::triggered, this, &MainWindow::undoTrashInstance);

    setSelectedInstanceById(APPLICATION->settings()->get("SelectedInstance").toString());

    // removing this looks stupid
    view->setFocus();

    retranslateUi();
    ui->mainToolBar->hide();
    ui->newsToolBar->hide();
    ui->instanceToolBar->hide();
    setMinimumSize(m_webMode ? QSize(1320, 780) : QSize(900, 620));
    resize(m_webMode ? QSize(1320, 780) : QSize(1024, 580));

#if defined(Q_OS_WIN)
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    m_frameless = true;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    DWORD cornerPref = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));
#endif

    // Wrap centralWidget with custom Discord-style title bar
    ui->centralWidget->setParent(nullptr);
    auto* outerWidget = new QWidget(this);
    outerWidget->setObjectName("awakeOuterContainer");
    auto* outerLayout = new QVBoxLayout(outerWidget);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    m_titleBar = new AwakeTitleBar(this, outerWidget);
    outerLayout->addWidget(m_titleBar);
    outerLayout->addWidget(ui->centralWidget);
    setCentralWidget(outerWidget);
#ifdef AWAKE_WEB_ENABLED
    if (m_webMode) {
        m_webShell = new Awake::Web::Shell(ui->centralWidget);
        ui->horizontalLayout->setContentsMargins(0, 0, 0, 0);
        ui->horizontalLayout->addWidget(m_webShell);
        m_webBridge = new Awake::Web::Bridge(m_webShell->assets(), [this](const QString& id) {
            setSelectedInstanceById(id);
            return m_selectedInstance && m_selectedInstance->id() == id;
        }, [this](const QString& action, const QString& id) { return invokeWebAction(action, id); }, m_webShell);
        connect(m_webShell, &Awake::Web::Shell::failed, this, &MainWindow::showWidgetFrontend);
        connect(view->selectionModel(), &QItemSelectionModel::currentChanged, m_webBridge, [this] {
            if (m_webBridge) m_webBridge->scheduleState();
        });
        m_webShell->start(m_webBridge);
        statusBar()->hide();
        ui->menuBar->hide();
    }
#endif
}

#ifdef AWAKE_WEB_ENABLED
void MainWindow::showWidgetFrontend(const QString& reason)
{
    if (!m_webMode) return;
    m_webMode = false;
    m_webShell->shutdown();
    m_webBridge = nullptr;
    m_webShell->hide();
    setMinimumSize(900, 620);
    const bool compact = APPLICATION->settings()->get("AwakeCompactLibrary").toBool();
    if (auto* delegate = dynamic_cast<Awake::LibraryDelegate*>(view->itemDelegate())) delegate->setCompact(compact);
    view->setCompact(compact);
    m_library->setViewMode(compact);
    m_library->setSortMode(APPLICATION->settings()->get("InstSortMode").toString());
    m_library->setReducedMotion(APPLICATION->settings()->get("AwakeReduceMotion").toBool());
    proxymodel->invalidate();
    proxymodel->sort(0);
    m_library->show();
    updateLibraryDetails();
    statusBar()->show();
    if (!reason.isEmpty()) {
        qWarning() << reason;
        statusBar()->showMessage(reason);
    }
    view->setFocus();
}

bool MainWindow::openWebAccounts()
{
#ifdef AWAKE_WEB_ENABLED
    if (!m_webMode || !m_webBridge) return false;
    const auto request = [bridge = m_webBridge] {
        QTimer::singleShot(0, bridge, [bridge] { emit bridge->accountsRequested(); });
    };
    if (m_webShell->isReady()) request();
    else connect(m_webBridge, &Awake::Web::Bridge::ready, m_webBridge, request, Qt::SingleShotConnection);
    return true;
#else
    return false;
#endif
}

QVariantMap MainWindow::invokeWebAction(const QString& action, const QString& id)
{
    const auto ok = [] { return QVariantMap{{"ok", true}}; };
    const auto fail = [](const QString& error) { return QVariantMap{{"ok", false}, {"error", error}}; };
    const bool needsInstance = QStringList{"launch", "edit", "folder", "manage", "launchOptions", "rename", "changeGroup", "copy", "export", "delete", "kill"}.contains(action);
    if (needsInstance) {
        if (!APPLICATION->instances()->getInstanceById(id)) return fail(tr("This instance no longer exists."));
        setSelectedInstanceById(id);
        if (!m_selectedInstance || m_selectedInstance->id() != id) return fail(tr("The instance could not be selected."));
    }
    if (action == "launch") {
        if (m_selectedInstance->isRunning() || !m_selectedInstance->canLaunch()) return fail(tr("This instance cannot be launched right now."));
        ui->actionLaunchInstance->trigger();
    } else if (action == "create") {
        ui->actionAddInstance->trigger();
    } else if (action == "import") {
        addInstance({}, {}, "import");
    } else if (action == "edit") {
        ui->actionEditInstance->trigger();
    } else if (action == "folder") {
        ui->actionViewSelectedInstFolder->trigger();
    } else if (action == "rename") {
        ui->actionRenameInstance->trigger();
    } else if (action == "changeGroup") {
        ui->actionChangeInstGroup->trigger();
    } else if (action == "copy") {
        ui->actionCopyInstance->trigger();
    } else if (action == "export") {
        if (auto* exportMenu = ui->actionExportInstance->menu()) exportMenu->exec(mapToGlobal(QPoint(width() - 280, height() - 240)));
        else ui->actionExportInstance->trigger();
    } else if (action == "delete") {
        ui->actionDeleteInstance->trigger();
    } else if (action == "kill") {
        ui->actionKillInstance->trigger();
    } else if (action == "settings") {
        ui->actionSettings->trigger();
    } else if (action == "logs") {
        APPLICATION->showLogWindow()->show();
    } else if (action == "accounts") {
        on_actionManageAccounts_triggered();
    } else if (action == "addMicrosoft") {
        auto account = MSALoginDialog::newAccount(this);
        if (account) {
            APPLICATION->accounts()->addAccount(account);
            if (APPLICATION->accounts()->count() == 1) {
                APPLICATION->accounts()->setDefaultAccount(account);
            }
        }
    } else if (action == "addOffline") {
        const QString username = id.trimmed();
        if (username.isEmpty()) return fail(tr("Username cannot be empty."));
        if (!APPLICATION->accounts()->anyAccountIsValid()) {
            return fail(tr("You must add a valid Microsoft account before adding an offline account."));
        }
        if (const MinecraftAccountPtr account = MinecraftAccount::createOffline(username)) {
            account->login()->start();
            APPLICATION->accounts()->addAccount(account);
            if (APPLICATION->accounts()->count() == 1) {
                APPLICATION->accounts()->setDefaultAccount(account);
            }
        } else {
            return fail(tr("Failed to create offline account."));
        }
    } else if (action == "removeAccount") {
        auto accounts = APPLICATION->accounts();
        for (int i = 0; i < accounts->count(); ++i) {
            if (accounts->at(i)->internalId() == id) {
                accounts->removeAccount(accounts->index(i, 0));
                break;
            }
        }
    } else if (action == "setDefaultAccount") {
        auto accounts = APPLICATION->accounts();
        for (int i = 0; i < accounts->count(); ++i) {
            if (accounts->at(i)->internalId() == id) {
                accounts->setDefaultAccount(accounts->at(i));
                break;
            }
        }
    } else if (action == "refreshAccount") {
        APPLICATION->accounts()->requestRefresh(id);
    } else if (action == "windowMinimize") {
        showMinimized();
        return ok();
    } else if (action == "windowMaximize") {
        if (isMaximized()) showNormal();
        else showMaximized();
        return ok();
    } else if (action == "windowClose") {
        close();
        return ok();
    } else if (action == "openRootFolder") {
        on_actionViewLauncherRootFolder_triggered();
        return ok();
    } else if (action == "openInstancesFolder") {
        on_actionViewInstanceFolder_triggered();
        return ok();
    } else if (action == "openModsFolder") {
        on_actionViewCentralModsFolder_triggered();
        return ok();
    } else if (action == "openLogsFolder") {
        on_actionViewLogsFolder_triggered();
        return ok();
    } else if (action == "openJavaFolder") {
        on_actionViewJavaFolder_triggered();
        return ok();
    } else if (action == "openSkinsFolder") {
        on_actionViewSkinsFolder_triggered();
        return ok();
    } else if (action == "checkForUpdates") {
        checkForUpdates();
        return ok();
    } else if (action == "clearMetadata") {
        on_actionClearMetadata_triggered();
        return ok();
    } else if (action == "reportBug") {
        on_actionReportBug_triggered();
        return ok();
    } else if (action == "about") {
        on_actionAbout_triggered();
        return ok();
    } else if (action == "discord") {
        on_actionDISCORD_triggered();
        return ok();
    } else if (action == "reddit") {
        on_actionREDDIT_triggered();
        return ok();
    } else if (action == "matrix") {
        on_actionMATRIX_triggered();
        return ok();
    } else if (action == "application") {
        auto* reduceMotion = findChild<QAction*>("awakeReduceMotion");
        auto* applicationMenu = reduceMotion ? qobject_cast<QMenu*>(reduceMotion->parent()) : nullptr;
        if (!applicationMenu) return fail(tr("The application menu is not available."));
        applicationMenu->exec(mapToGlobal(QPoint(24, 40)));
    } else if (action == "launchOptions") {
        updateLaunchButton();
        ui->actionLaunchInstance->menu()->exec(mapToGlobal(QPoint(width() - 280, height() - 170)));
    } else if (action == "manage") {
        QMenu menu(this);
        menu.addActions({ui->actionRenameInstance, ui->actionChangeInstIcon, ui->actionChangeInstGroup, ui->actionCopyInstance,
                         ui->actionExportInstance, ui->actionCreateInstanceShortcut, ui->actionKillInstance, ui->actionDeleteInstance});
        menu.exec(mapToGlobal(QPoint(width() - 280, height() - 240)));
    } else if (action == "installPack" || action == "importArchive") {
        const auto document = QJsonDocument::fromJson(id.toUtf8());
        if (!document.isObject()) return fail(tr("Invalid installation request."));
        const auto request = document.object();
        const auto name = request.value("name").toString().trimmed();
        if (name.isEmpty() || name.size() > 256) return fail(tr("Enter an instance name."));
        InstanceTask* task = nullptr;
        if (action == "installPack") {
            QString error;
            task = m_webBridge->packCatalog()->createTask(request.value("provider").toString(), request.value("packId").toString(),
                request.value("versionId").toString(), this, &error);
            if (!task) return fail(error);
        } else {
            const QUrl url(request.value("url").toString());
            if ((!Awake::Web::externalUrl(url) && !url.isLocalFile()) ||
                (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isFile())) return fail(tr("Choose an archive file or a valid download URL."));
            task = new InstanceImportTask(url, false, this);
            task->setIcon("default");
        }
        task->setName(name);
        task->setGroup(request.value("group").toString().trimmed());
        QString error;
        if (!instanceFromInstanceTask(task, &error)) return fail(error);
    } else if (action == "createQuick") {
        const QJsonDocument doc = QJsonDocument::fromJson(id.toUtf8());
        const QJsonObject obj = doc.object();
        QString name = obj.value("name").toString().trimmed();
        QString mcVersion = obj.value("version").toString().trimmed();
        const QString loader = obj.value("loader").toString().trimmed();
        const QString group = obj.value("group").toString().trimmed();
        if (mcVersion.isEmpty()) return fail(tr("Choose a Minecraft version."));
        if (name.isEmpty()) name = mcVersion;

        auto meta = APPLICATION->metadataIndex();
        if (!meta) return fail(tr("Metadata index is not available."));

        QString error;
        auto minecraftLoadTask = meta->loadVersion("net.minecraft", mcVersion);
        if (!minecraftLoadTask || !runModalTask(minecraftLoadTask.get(), &error))
            return fail(tr("Could not load Minecraft %1 metadata:\n%2").arg(mcVersion, error));
        auto mcVer = meta->get("net.minecraft", mcVersion);
        if (!mcVer || !mcVer->isLoaded()) return fail(tr("The selected Minecraft version could not be loaded: %1").arg(mcVersion));

        QString loaderUid;
        if (loader.compare("Fabric", Qt::CaseInsensitive) == 0) loaderUid = "net.fabricmc.fabric-loader";
        else if (loader.compare("NeoForge", Qt::CaseInsensitive) == 0) loaderUid = "net.neoforged";
        else if (loader.compare("Forge", Qt::CaseInsensitive) == 0) loaderUid = "net.minecraftforge";
        else if (loader.compare("Quilt", Qt::CaseInsensitive) == 0) loaderUid = "org.quiltmc.quilt-loader";
        else if (loader != "Vanilla") return fail(tr("Unsupported mod loader."));

        InstanceTask* task = nullptr;
        if (loaderUid.isEmpty() || loader.compare("Vanilla", Qt::CaseInsensitive) == 0) {
            task = new VanillaCreationTask(mcVer);
        } else {
            auto loaderList = meta->get(loaderUid);
            if (loaderList && !loaderList->isLoaded()) {
                auto loadTask = loaderList->getLoadTask();
                if (!loadTask || !runModalTask(loadTask.get(), &error))
                    return fail(tr("Could not load %1 metadata (%2):\n%3").arg(loader, loaderUid, error));
            }
            BaseVersion::Ptr loaderVer = loaderList ? loaderList->getRecommendedForMinecraft(mcVersion) : nullptr;
            if (loaderVer) {
                task = new VanillaCreationTask(mcVer, loaderUid, loaderVer);
            } else {
                return fail(tr("No compatible %1 version is available for Minecraft %2.\nMetadata: %3 (%4 versions loaded)")
                                .arg(loader, mcVersion, loaderUid).arg(loaderList ? loaderList->count() : 0));
            }
        }

        if (task) {
            task->setName(name);
            if (!group.isEmpty()) task->setGroup(group);
            task->setIcon("default");
            if (!instanceFromInstanceTask(task, &error)) return fail(tr("Could not create %1 for Minecraft %2:\n%3").arg(loader, mcVersion, error));
        }
    } else if (action == "legacy") {
        showWidgetFrontend();
    } else return fail(tr("This action is not available."));
    return ok();
}

void MainWindow::setModalBackdrop(bool active)
{
    if (m_webBridge) {
        m_webBridge->setModalActive(active);
    }
}
#else
void MainWindow::setModalBackdrop(bool) {}
#endif

// macOS always has a native menu bar, so these fixes are not applicable
// Other systems may or may not have a native menu bar (most do not - it seems like only Ubuntu Unity does)
#ifndef Q_OS_MAC
void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Alt && !APPLICATION->settings()->get("MenuBarInsteadOfToolBar").toBool())
        ui->menuBar->setVisible(!ui->menuBar->isVisible());
    else
        QMainWindow::keyReleaseEvent(event);
}
#endif

#if defined(Q_OS_WIN)
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    MSG* msg = static_cast<MSG*>(message);
    if (m_frameless && msg->message == WM_NCHITTEST) {
        if (isMaximized() || isFullScreen()) {
            return false;
        }

        const int border = 8;
        RECT winrect;
        GetWindowRect(msg->hwnd, &winrect);

        long x = GET_X_LPARAM(msg->lParam);
        long y = GET_Y_LPARAM(msg->lParam);

        bool left = (x >= winrect.left && x < winrect.left + border);
        bool right = (x < winrect.right && x >= winrect.right - border);
        bool top = (y >= winrect.top && y < winrect.top + border);
        bool bottom = (y < winrect.bottom && y >= winrect.bottom - border);

        if (top && left) {
            *result = HTTOPLEFT;
            return true;
        }
        if (top && right) {
            *result = HTTOPRIGHT;
            return true;
        }
        if (bottom && left) {
            *result = HTBOTTOMLEFT;
            return true;
        }
        if (bottom && right) {
            *result = HTBOTTOMRIGHT;
            return true;
        }
        if (left) {
            *result = HTLEFT;
            return true;
        }
        if (right) {
            *result = HTRIGHT;
            return true;
        }
        if (top) {
            *result = HTTOP;
            return true;
        }
        if (bottom) {
            *result = HTBOTTOM;
            return true;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::retranslateUi()
{
    if (m_selectedInstance) {
        m_statusLeft->setText(m_selectedInstance->getStatusbarDescription());
    } else {
        m_statusLeft->setText(tr("No instance selected"));
    }

    ui->retranslateUi(this);
    if (auto* reduceMotion = findChild<QAction*>("awakeReduceMotion"))
        reduceMotion->setText(tr("Reduce motion"));
    if (m_library)
        updateLibraryDetails();

    MinecraftAccountPtr defaultAccount = APPLICATION->accounts()->defaultAccount();
    if (defaultAccount) {
        auto profileLabel = profileInUseFilter(defaultAccount->displayName(), defaultAccount->isInUse());
        ui->actionAccountsButton->setText(profileLabel);
    }

    changeIconButton->setToolTip(ui->actionChangeInstIcon->toolTip());
    renameButton->setToolTip(ui->actionRenameInstance->toolTip());

    // replace the %1 with the launcher display name in some actions
    if (helpMenuButton->toolTip().contains("%1"))
        helpMenuButton->setToolTip(helpMenuButton->toolTip().arg(BuildConfig.LAUNCHER_DISPLAYNAME));

    for (auto action : ui->helpMenu->actions()) {
        if (action->text().contains("%1"))
            action->setText(action->text().arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        if (action->toolTip().contains("%1"))
            action->setToolTip(action->toolTip().arg(BuildConfig.LAUNCHER_DISPLAYNAME));
    }
}

MainWindow::~MainWindow() {}

QMenu* MainWindow::createPopupMenu()
{
    QMenu* filteredMenu = QMainWindow::createPopupMenu();
    filteredMenu->removeAction(ui->mainToolBar->toggleViewAction());

    filteredMenu->addAction(ui->actionToggleStatusBar);
    filteredMenu->addAction(ui->actionLockToolbars);

    return filteredMenu;
}
void MainWindow::setStatusBarVisibility(bool state)
{
    statusBar()->setVisible(state);
    APPLICATION->settings()->set("StatusBarVisible", state);
}
void MainWindow::lockToolbars(bool state)
{
    ui->mainToolBar->setMovable(!state);
    ui->instanceToolBar->setMovable(!state);
    ui->newsToolBar->setMovable(!state);
    APPLICATION->settings()->set("ToolbarsLocked", state);
}

void MainWindow::konamiTriggered()
{
    QString gradient =
        " stop:0 rgba(125, 0, 0, 255), stop:0.166 rgba(125, 125, 0, 255), stop:0.333 rgba(0, 125, 0, 255), stop:0.5 rgba(0, 125, 125, "
        "255), stop:0.666 rgba(0, 0, 125, 255), stop:0.833 rgba(125, 0, 125, 255), stop:1 rgba(125, 0, 0, 255));";
    QString stylesheet = "background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0," + gradient;
    if (ui->mainToolBar->styleSheet() == stylesheet) {
        ui->mainToolBar->setStyleSheet("");
        ui->instanceToolBar->setStyleSheet("");
        ui->centralWidget->setStyleSheet("");
        ui->newsToolBar->setStyleSheet("");
        ui->statusBar->setStyleSheet("");
        qDebug() << "Super Secret Mode DEACTIVATED!";
    } else {
        ui->mainToolBar->setStyleSheet(stylesheet);
        ui->instanceToolBar->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1," + gradient);
        ui->centralWidget->setStyleSheet("background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1," + gradient);
        ui->newsToolBar->setStyleSheet(stylesheet);
        ui->statusBar->setStyleSheet(stylesheet);
        qDebug() << "Super Secret Mode ACTIVATED!";
    }
}

void MainWindow::showInstanceContextMenu(const QPoint& pos)
{
    QList<QAction*> actions;

    QAction* actionSep = new QAction("", this);
    actionSep->setSeparator(true);

    bool onInstance = view->indexAt(pos).isValid();
    if (onInstance) {
        // reuse the file menu actions
        actions = ui->fileMenu->actions();

        // remove the add instance action, launcher settings action and close action
        actions.removeFirst();
        actions.removeLast();
        actions.removeLast();

        actions.prepend(ui->actionChangeInstIcon);
        actions.prepend(ui->actionRenameInstance);

        // add header
        actions.prepend(actionSep);
        QAction* actionVoid = new QAction(m_selectedInstance->name(), this);
        actionVoid->setEnabled(false);
        actions.prepend(actionVoid);
    } else {
        auto group = view->groupNameAt(pos);

        QAction* actionVoid = new QAction(group.isNull() ? BuildConfig.LAUNCHER_DISPLAYNAME : group, this);
        actionVoid->setEnabled(false);

        QAction* actionCreateInstance = new QAction(tr("&Create instance"), this);
        actionCreateInstance->setToolTip(ui->actionAddInstance->toolTip());
        if (!group.isNull()) {
            QVariantMap instance_action_data;
            instance_action_data["group"] = group;
            actionCreateInstance->setData(instance_action_data);
        }

        connect(actionCreateInstance, &QAction::triggered, this, &MainWindow::on_actionAddInstance_triggered);

        actions.prepend(actionSep);
        actions.prepend(actionVoid);
        actions.append(actionCreateInstance);
        if (!group.isNull()) {
            QAction* actionDeleteGroup = new QAction(tr("&Delete group"), this);
            connect(actionDeleteGroup, &QAction::triggered, this, [this, group] { deleteGroup(group); });
            actions.append(actionDeleteGroup);

            QAction* actionRenameGroup = new QAction(tr("&Rename group"), this);
            connect(actionRenameGroup, &QAction::triggered, this, [this, group] { renameGroup(group); });
            actions.append(actionRenameGroup);
        }
    }
    QMenu myMenu;
    myMenu.addActions(actions);
    /*
    if (onInstance)
        myMenu.setEnabled(m_selectedInstance->canLaunch());
    */
    myMenu.exec(view->mapToGlobal(pos));
}

void MainWindow::updateMainToolBar()
{
    ui->menuBar->setVisible(!m_webMode && APPLICATION->settings()->get("MenuBarInsteadOfToolBar").toBool());
    ui->mainToolBar->hide();
}

void MainWindow::updateLaunchButton()
{
    QMenu* launchMenu = ui->actionLaunchInstance->menu();
    if (launchMenu)
        launchMenu->clear();
    else
        launchMenu = new QMenu(this);
    if (m_selectedInstance)
        m_selectedInstance->populateLaunchMenu(launchMenu);
    ui->actionLaunchInstance->setMenu(launchMenu);
}

void MainWindow::updateThemeMenu()
{
    QMenu* themeMenu = ui->actionChangeTheme->menu();

    if (themeMenu) {
        themeMenu->clear();
    } else {
        themeMenu = new QMenu(this);
    }

    auto themes = APPLICATION->themeManager()->getValidApplicationThemes();

    QActionGroup* themesGroup = new QActionGroup(this);

    for (auto* theme : themes) {
        QAction* themeAction = themeMenu->addAction(theme->name());

        themeAction->setCheckable(true);
        if (APPLICATION->settings()->get("ApplicationTheme").toString() == theme->id()) {
            themeAction->setChecked(true);
        }
        themeAction->setActionGroup(themesGroup);

        connect(themeAction, &QAction::triggered, APPLICATION, [theme]() {
            APPLICATION->themeManager()->setApplicationTheme(theme->id());
            APPLICATION->settings()->set("ApplicationTheme", theme->id());
        });
    }

    ui->actionChangeTheme->setMenu(themeMenu);
}

void MainWindow::repopulateAccountsMenu()
{
    ui->accountsMenu->clear();

    // NOTE: this is done so the accounts button text is not set to the accounts menu title
    QMenu* accountsButtonMenu = ui->actionAccountsButton->menu();
    if (accountsButtonMenu) {
        accountsButtonMenu->clear();
    } else {
        accountsButtonMenu = new QMenu(this);
        ui->actionAccountsButton->setMenu(accountsButtonMenu);
    }

    auto accounts = APPLICATION->accounts();
    MinecraftAccountPtr defaultAccount = accounts->defaultAccount();

    bool canChangeSkin = defaultAccount && (defaultAccount->accountType() == AccountType::MSA) && !defaultAccount->isActive();
    ui->actionManageSkins->setEnabled(canChangeSkin);

    QString active_profileId = "";
    if (defaultAccount) {
        // this can be called before accountMenuButton exists
        if (ui->actionAccountsButton) {
            auto profileLabel = profileInUseFilter(defaultAccount->displayName(), defaultAccount->isInUse());
            ui->actionAccountsButton->setText(profileLabel);
        }
    }

    QActionGroup* accountsGroup = new QActionGroup(this);

    if (accounts->count() <= 0) {
        ui->actionNoAccountsAdded->setEnabled(false);
        ui->accountsMenu->addAction(ui->actionNoAccountsAdded);
    } else {
        // TODO: Nicer way to iterate?
        for (int i = 0; i < accounts->count(); i++) {
            MinecraftAccountPtr account = accounts->at(i);
            auto profileLabel = profileInUseFilter(account->displayName(), account->isInUse());
            QAction* action = new QAction(profileLabel, this);
            action->setData(i);
            action->setCheckable(true);
            action->setActionGroup(accountsGroup);
            if (defaultAccount == account) {
                action->setChecked(true);
            }

            auto face = account->getFace();
            if (!face.isNull()) {
                action->setIcon(face);
            } else {
                action->setIcon(QIcon::fromTheme("noaccount"));
            }

            const int highestNumberKey = 9;
            if (i < highestNumberKey) {
                action->setShortcut(QKeySequence(tr("Ctrl+%1").arg(i + 1)));
            }

            ui->accountsMenu->addAction(action);
            connect(action, &QAction::triggered, this, &MainWindow::changeActiveAccount);
        }
    }

    ui->accountsMenu->addSeparator();

    ui->actionNoDefaultAccount->setData(-1);
    ui->actionNoDefaultAccount->setChecked(!defaultAccount);
    ui->actionNoDefaultAccount->setActionGroup(accountsGroup);

    ui->accountsMenu->addAction(ui->actionNoDefaultAccount);

    connect(ui->actionNoDefaultAccount, &QAction::triggered, this, &MainWindow::changeActiveAccount);

    ui->accountsMenu->addSeparator();
    ui->accountsMenu->addAction(ui->actionManageSkins);
    ui->accountsMenu->addAction(ui->actionManageAccounts);

    accountsButtonMenu->addActions(ui->accountsMenu->actions());
}

void MainWindow::updatesAllowedChanged(bool allowed)
{
    if (!APPLICATION->updaterEnabled()) {
        return;
    }
    ui->actionCheckUpdate->setEnabled(allowed);
}

/*
 * Assumes the sender is a QAction
 */
void MainWindow::changeActiveAccount()
{
    QAction* sAction = (QAction*)sender();

    // Profile's associated Mojang username
    if (sAction->data().typeId() != QMetaType::Int)
        return;

    QVariant action_data = sAction->data();
    bool valid = false;
    int index = action_data.toInt(&valid);
    if (!valid) {
        index = -1;
    }
    auto accounts = APPLICATION->accounts();
    accounts->setDefaultAccount(index == -1 ? nullptr : accounts->at(index));
    defaultAccountChanged();
}

void MainWindow::defaultAccountChanged()
{
    repopulateAccountsMenu();

    MinecraftAccountPtr account = APPLICATION->accounts()->defaultAccount();

    // FIXME: this needs adjustment for MSA
    if (account && account->profileName() != "") {
        auto profileLabel = profileInUseFilter(account->displayName(), account->isInUse());
        ui->actionAccountsButton->setText(profileLabel);
        auto face = account->getFace();
        if (face.isNull()) {
            ui->actionAccountsButton->setIcon(QIcon::fromTheme("noaccount"));
        } else {
            ui->actionAccountsButton->setIcon(face);
        }
        return;
    }

    // Set the icon to the "no account" icon.
    ui->actionAccountsButton->setIcon(QIcon::fromTheme("noaccount"));
    ui->actionAccountsButton->setText(tr("Accounts"));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == view) {
        if (ev->type() == QEvent::KeyPress) {
            secretEventFilter->input(ev);
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(ev);
            switch (keyEvent->key()) {
                    /*
                case Qt::Key_Enter:
                case Qt::Key_Return:
                    activateInstance(m_selectedInstance);
                    return true;
                    */
                case Qt::Key_Delete:
                    on_actionDeleteInstance_triggered();
                    return true;
                case Qt::Key_F5:
                    refreshInstances();
                    return true;
                case Qt::Key_F2:
                    on_actionRenameInstance_triggered();
                    return true;
                default:
                    break;
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::updateNewsLabel()
{
    if (m_newsChecker->isLoadingNews()) {
        newsLabel->setText(tr("Loading news..."));
        newsLabel->setEnabled(false);
        ui->actionMoreNews->setVisible(false);
    } else {
        QList<NewsEntryPtr> entries = m_newsChecker->getNewsEntries();
        if (entries.length() > 0) {
            newsLabel->setText(entries[0]->title);
            newsLabel->setEnabled(true);
            ui->actionMoreNews->setVisible(true);
        } else {
            newsLabel->setText(tr("No news available."));
            newsLabel->setEnabled(false);
            ui->actionMoreNews->setVisible(false);
        }
    }
}

QList<int> stringToIntList(const QString& string)
{
    QStringList split = string.split(',', Qt::SkipEmptyParts);
    QList<int> out;
    for (int i = 0; i < split.size(); ++i) {
        out.append(split.at(i).toInt());
    }
    return out;
}
QString intListToString(const QList<int>& list)
{
    QStringList slist;
    for (int i = 0; i < list.size(); ++i) {
        slist.append(QString::number(list.at(i)));
    }
    return slist.join(',');
}

void MainWindow::onCatToggled(bool state)
{
    setCatBackground(state);
    APPLICATION->settings()->set("TheCat", state);
}

void MainWindow::setCatBackground(bool enabled)
{
    view->setPaintCat(enabled);
    view->viewport()->repaint();
}

void MainWindow::updateCatState()
{
    SettingsObject* settings = APPLICATION->settings();
    const bool catEnabled = settings->get("EnableCat").toBool();
    bool catVisible = settings->get("TheCat").toBool();
    if (!catEnabled && catVisible) {
        settings->set("TheCat", false);
        catVisible = false;
    }

    ui->actionCAT->setVisible(catEnabled);
    ui->actionCAT->setChecked(catVisible);
    setCatBackground(catVisible);
}

bool MainWindow::runModalTask(Task* task, QString* error)
{
    if (!error) connect(task, &Task::failed, this,
            [this](QString reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
    connect(task, &Task::succeeded, this, [this, task]() {
        QStringList warnings = task->warnings();
        if (warnings.count()) {
            CustomMessageBox::selectable(this, tr("Warnings"), warnings.join('\n'), QMessageBox::Warning)->show();
        }
    });
    ProgressDialog loadDialog(this);
    loadDialog.showSkipButton();
    loadDialog.execWithTask(task);
    if (error && !task->wasSuccessful()) {
        *error = task->failReason();
        if (error->isEmpty()) *error = tr("Task cancelled or incomplete: %1\n%2\n%3").arg(task->objectName(), task->getStatus(), task->getDetails());
    }
    return task->wasSuccessful();
}

bool MainWindow::instanceFromInstanceTask(InstanceTask* rawTask, QString* error)
{
    unique_qobject_ptr<Task> task(APPLICATION->instances()->wrapInstanceTask(rawTask));
    return runModalTask(task.get(), error);
}

void MainWindow::on_actionCopyInstance_triggered()
{
    if (!m_selectedInstance)
        return;

    CopyInstanceDialog copyInstDlg(m_selectedInstance, this);
    if (!copyInstDlg.exec())
        return;

    auto copyTask = new InstanceCopyTask(m_selectedInstance, copyInstDlg.getChosenOptions());
    copyTask->setName(copyInstDlg.instName());
    copyTask->setGroup(copyInstDlg.instGroup());
    copyTask->setIcon(copyInstDlg.iconKey());
    unique_qobject_ptr<Task> task(APPLICATION->instances()->wrapInstanceTask(copyTask));
    runModalTask(task.get());
}

void MainWindow::addInstance(const QString& url, const QMap<QString, QString>& extra_info, const QString& initialPage)
{
    QString groupName;
    do {
        QObject* obj = sender();
        if (!obj)
            break;
        QAction* action = qobject_cast<QAction*>(obj);
        if (!action)
            break;
        auto map = action->data().toMap();
        if (!map.contains("group"))
            break;
        groupName = map["group"].toString();
    } while (0);

    if (groupName.isEmpty()) {
        groupName = APPLICATION->settings()->get("LastUsedGroupForNewInstance").toString();
    }

    NewInstanceDialog newInstDlg(groupName, url, extra_info, this);
    if (!initialPage.isEmpty())
        if (auto* pages = newInstDlg.findChild<PageContainer*>()) pages->selectPage(initialPage);
    if (!newInstDlg.exec())
        return;

    APPLICATION->settings()->set("LastUsedGroupForNewInstance", newInstDlg.instGroup());
    APPLICATION->settings()->set("LastUsedInstDirForNewInstance", newInstDlg.instDir());

    InstanceTask* creationTask = newInstDlg.extractTask();
    if (creationTask) {
        instanceFromInstanceTask(creationTask);
    }
}

void MainWindow::on_actionAddInstance_triggered()
{
    addInstance();
}

void MainWindow::processURLs(QList<QUrl> urls)
{
    // NOTE: This loop only processes one dropped file!
    for (auto& url : urls) {
        if (url.isEmpty() || url.toString().trimmed().isEmpty())
            continue;

        const QUrlQuery parameters(url);
        const bool hasAuthParameters = parameters.hasQueryItem("code") || parameters.hasQueryItem("state") ||
                                       parameters.hasQueryItem("error") || parameters.hasQueryItem("access_token") ||
                                       parameters.hasQueryItem("refresh_token");
        if (url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME &&
            (hasAuthParameters || (url.host() != "install" && url.host() != "import" &&
                                   !url.path().startsWith("/import", Qt::CaseInsensitive)))) {
            const auto callback = OAuthCallback::parse(url);
            if (callback) emit APPLICATION->oauthReplyRecieved(*callback);
            else qWarning() << "[Auth] Rejected malformed OAuth callback";
            continue;
        }
        qDebug() << "Processing" << url;

        // The isLocalFile() check below doesn't work as intended without an explicit scheme.
        if (url.scheme().isEmpty())
            url.setScheme("file");

        ModPlatform::IndexedVersion version;
        QMap<QString, QString> extra_info;
        QUrl local_url;
        if (!url.isLocalFile()) {  // download the remote resource and identify
            if (url.scheme().compare("modrinth", Qt::CaseInsensitive) == 0) {
                const auto packId = ModrinthAPI::getModpackIdFromUrl(url);
                if (!packId.isEmpty()) {
                    extra_info.insert("pack_id", packId);
                    addInstance(url.toString(), extra_info);
                } else {
                    CustomMessageBox::selectable(
                        this, tr("Error"),
                        tr("Unsupported Modrinth link.\n\nPrism Launcher currently only supports modpack links such as "
                           "modrinth://modpack/fabulously-optimized."),
                        QMessageBox::Critical)
                        ->show();
                }
                continue;
            }

            const bool isExternalURLImport = (url.host().toLower() == "import") || (url.path().startsWith("/import", Qt::CaseInsensitive));

            QUrl dl_url;
            if (url.scheme() == "curseforge" || (url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME && url.host() == "install")) {
                // need to find the download link for the modpack / resource
                // format of url curseforge://install?addonId=IDHERE&fileId=IDHERE
                // format of url binaryname://install?platform=curseforge&addonId=IDHERE&fileId=IDHERE
                QUrlQuery query(url);

                // check if this is a binaryname:// url
                if (url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME) {
                    // check this is an curseforge platform request
                    if (query.queryItemValue("platform").toLower() != "curseforge") {
                        qDebug() << "Invalid mod distribution platform:" << query.queryItemValue("platform");
                        continue;
                    }
                }

                if (query.allQueryItemValues("addonId").isEmpty() || query.allQueryItemValues("fileId").isEmpty()) {
                    qDebug() << "Invalid curseforge link:" << url;
                    continue;
                }

                auto addonId = query.allQueryItemValues("addonId")[0];
                auto fileId = query.allQueryItemValues("fileId")[0];

                extra_info.insert("pack_id", addonId);
                extra_info.insert("pack_version_id", fileId);

                auto [job, array] = FlameAPI::getFile(addonId, fileId);

                connect(job.get(), &Task::failed, this, [this](const QString& reason) {
                    CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show();
                });
                connect(job.get(), &Task::succeeded, this, [this, array, addonId, fileId, &dl_url, &version] {
                    qDebug() << "Returned CFURL Json:\n" << array->toStdString().c_str();
                    auto doc = Json::requireDocument(*array);
                    if (!doc) {
                        CustomMessageBox::selectable(this, tr("Error"), doc.error(), QMessageBox::Critical)->show();
                        return;
                    }
                    auto data = doc->object()["data"].toObject();
                    // No way to find out if it's a mod or a modpack before here
                    // And also we need to check if it ends with .zip, instead of any better way
                    auto versionRes = FlameMod::loadIndexedPackVersion(data);
                    if (!versionRes) {
                        CustomMessageBox::selectable(this, tr("Error"), versionRes.error(), QMessageBox::Critical)->show();
                        return;
                    }
                    version = versionRes.value();
                    auto fileName = version.fileName;

                    // Have to use ensureString then use QUrl to get proper url encoding
                    dl_url = QUrl(version.downloadUrl);
                    if (!dl_url.isValid()) {
                        CustomMessageBox::selectable(
                            this, tr("Error"),
                            tr("The modpack, mod, or resource %1 is blocked for third-parties! Please download it manually.").arg(fileName),
                            QMessageBox::Critical)
                            ->show();
                        return;
                    }
                });

                {  // drop stack
                    ProgressDialog dlUrlDialod(this);
                    dlUrlDialod.showSkipButton();
                    dlUrlDialod.execWithTask(job.get());
                }

            } else if ((url.scheme() == "prismlauncher" || url.scheme() == BuildConfig.LAUNCHER_APP_BINARY_NAME) && isExternalURLImport) {
                // PrismLauncher URL protocol modpack import
                // works for any prism fork
                // preferred import format: prismlauncher://import?url=ENCODED
                const auto host = url.host().toLower();
                const auto path = url.path();

                QString encodedTarget;

                {
                    QUrlQuery query(url);
                    const auto values = query.allQueryItemValues("url");
                    if (!values.isEmpty()) {
                        encodedTarget = values.first();
                    }
                }

                // alternative import format: prismlauncher://import/ENCODED
                if (encodedTarget.isEmpty()) {
                    QString p = path;

                    if (p.startsWith("/import/", Qt::CaseInsensitive)) {
                        p = p.mid(QString("/import/").size());
                    } else if (host == "import" && p.startsWith("/")) {
                        p = p.mid(1);
                    }

                    if (!p.isEmpty() && p != "/import") {
                        encodedTarget = p;
                    }
                }

                if (encodedTarget.isEmpty()) {
                    CustomMessageBox::selectable(this, tr("Error"), tr("Invalid import link: missing 'url' parameter."),
                                                 QMessageBox::Critical)
                        ->show();
                    continue;
                }

                const QString decodedStr = QUrl::fromPercentEncoding(encodedTarget.toUtf8()).trimmed();

                QUrl target = QUrl::fromUserInput(decodedStr);

                // Validate: only allow http(s)
                if (!target.isValid() || (target.scheme() != "https" && target.scheme() != "http")) {
                    CustomMessageBox::selectable(this, tr("Error"), tr("Invalid import link: URL must be http(s)."), QMessageBox::Critical)
                        ->show();
                    continue;
                }

                const auto res = QMessageBox::question(
                    this, tr("Install modpack"),
                    tr("Do you want to download and import a modpack from:\n%1\n\nURL:\n%2").arg(target.host(), target.toString()),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (res != QMessageBox::Yes) {
                    continue;
                }

                dl_url = target;
            } else {
                dl_url = url;
            }

            if (!dl_url.isValid()) {
                continue;  // no valid url to download this resource
            }

            const QString path = dl_url.host() + '/' + dl_url.path();
            auto entry = APPLICATION->metacache()->resolveEntry("general", path);
            entry->setStale(true);
            auto dl_job = unique_qobject_ptr<NetJob>(new NetJob(tr("Modpack download"), APPLICATION->network()));
            dl_job->addNetAction(Net::ApiRequest::makeCached(dl_url, entry));
            auto archivePath = entry->getFullPath();

            bool dl_success = false;
            connect(dl_job.get(), &Task::failed, this,
                    [this](QString reason) { CustomMessageBox::selectable(this, tr("Error"), reason, QMessageBox::Critical)->show(); });
            connect(dl_job.get(), &Task::succeeded, this, [&dl_success] { dl_success = true; });

            {  // drop stack
                ProgressDialog dlUrlDialod(this);
                dlUrlDialod.showSkipButton();
                dlUrlDialod.execWithTask(dl_job.get());
            }

            if (!dl_success) {
                continue;  // no local file to identify
            }
            local_url = QUrl::fromLocalFile(archivePath);

        } else {
            local_url = url;
        }

        auto localFileName = QDir::toNativeSeparators(local_url.toLocalFile());
        QFileInfo localFileInfo(localFileName);

        if (localFileName.isEmpty() || !localFileInfo.exists()) {
            qDebug() << "Ignoring invalid path" << localFileName;
            continue;
        }

        auto type = ResourceUtils::identify(localFileInfo);

        if (ModPlatform::ResourceTypeUtils::g_VALID_RESOURCES.count(type) == 0) {  // probably instance/modpack
            addInstance(localFileName, extra_info);
            continue;
        }

        if (APPLICATION->instances()->count() <= 0) {
            CustomMessageBox::selectable(this, tr("No instance!"),
                                         tr("No instance available to add the resource to.\nPlease create a new instance before "
                                            "attempting to install this resource again."),
                                         QMessageBox::Critical)
                ->show();
            continue;
        }
        ImportResourceDialog dlg(localFileName, type, this);

        if (dlg.exec() != QDialog::Accepted)
            continue;

        qDebug() << "Adding resource" << localFileName << "to" << dlg.selectedInstanceKey;

        auto inst = APPLICATION->instances()->getInstanceById(dlg.selectedInstanceKey);
        auto minecraftInst = inst;

        switch (type) {
            case ModPlatform::ResourceType::ResourcePack:
                minecraftInst->resourcePackList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::TexturePack:
                minecraftInst->texturePackList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::DataPack:
                qWarning() << "Importing of Data Packs not supported at this time. Ignoring" << localFileName;
                break;
            case ModPlatform::ResourceType::Mod:
                minecraftInst->loaderModList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::ShaderPack:
                minecraftInst->shaderPackList()->installResourceWithFlameMetadata(localFileName, version);
                break;
            case ModPlatform::ResourceType::World:
                minecraftInst->worldList()->installWorld(localFileInfo);
                break;
            case ModPlatform::ResourceType::Unknown:
            default:
                qDebug() << "Can't Identify" << localFileName << "Ignoring it.";
                break;
        }
    }
}

void MainWindow::on_actionREDDIT_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.SUBREDDIT_URL));
}

void MainWindow::on_actionDISCORD_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.DISCORD_URL));
}

void MainWindow::on_actionMATRIX_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.MATRIX_URL));
}

void MainWindow::on_actionChangeInstIcon_triggered()
{
    if (!m_selectedInstance)
        return;

    IconPickerDialog dlg(this);
    dlg.execWithSelection(m_selectedInstance->iconKey());
    if (dlg.result() == QDialog::Accepted) {
        m_selectedInstance->setIconKey(dlg.selectedIconKey);
        auto icon = APPLICATION->icons()->getIcon(dlg.selectedIconKey);
        ui->actionChangeInstIcon->setIcon(icon);
        changeIconButton->setIcon(icon);
    }
}

void MainWindow::iconUpdated(QString icon)
{
    if (icon == m_currentInstIcon) {
        auto new_icon = APPLICATION->icons()->getIcon(m_currentInstIcon);
        ui->actionChangeInstIcon->setIcon(new_icon);
        changeIconButton->setIcon(new_icon);
    }
}

void MainWindow::updateInstanceToolIcon(QString new_icon)
{
    m_currentInstIcon = new_icon;
    auto icon = APPLICATION->icons()->getIcon(m_currentInstIcon);
    ui->actionChangeInstIcon->setIcon(icon);
    changeIconButton->setIcon(icon);
}

void MainWindow::setSelectedInstanceById(const QString& id)
{
    if (id.isNull())
        return;
    const QModelIndex index = APPLICATION->instances()->getInstanceIndexById(id);
    if (index.isValid()) {
        QModelIndex selectionIndex = proxymodel->mapFromSource(index);
        view->selectionModel()->setCurrentIndex(selectionIndex, QItemSelectionModel::ClearAndSelect);
        updateStatusCenter();
    }
}

void MainWindow::on_actionChangeInstGroup_triggered()
{
    if (!m_selectedInstance)
        return;

    InstanceId instId = m_selectedInstance->id();
    QString src(APPLICATION->instances()->getInstanceGroup(instId));

    QStringList groups = APPLICATION->instances()->getGroups();
    groups.prepend("");
    int index = groups.indexOf(src);
    bool ok = false;
    QString dst = QInputDialog::getItem(this, tr("Group name"), tr("Enter a new group name."), groups, index, true, &ok);
    dst = dst.simplified();

    if (ok) {
        APPLICATION->instances()->setInstanceGroup(instId, dst);
    }
}

void MainWindow::deleteGroup(QString group)
{
    Q_ASSERT(!group.isEmpty());

    const int reply = QMessageBox::question(this, tr("Delete group"), tr("Are you sure you want to delete the group '%1'?").arg(group),
                                            QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes)
        APPLICATION->instances()->deleteGroup(group);
}

void MainWindow::renameGroup(QString group)
{
    Q_ASSERT(!group.isEmpty());

    QString name = QInputDialog::getText(this, tr("Rename group"), tr("Enter a new group name."), QLineEdit::Normal, group);
    name = name.simplified();
    if (name.isNull() || name == group)
        return;

    const bool empty = name.isEmpty();
    const bool duplicate = APPLICATION->instances()->getGroups().contains(name, Qt::CaseInsensitive) && group.toLower() != name.toLower();

    if (empty || duplicate) {
        QMessageBox::warning(this, tr("Cannot rename group"), empty ? tr("Cannot set empty name.") : tr("Group already exists. :/"));
        return;
    }

    APPLICATION->instances()->renameGroup(group, name);
}

void MainWindow::undoTrashInstance()
{
    if (!APPLICATION->instances()->undoTrashInstance())
        QMessageBox::warning(
            this, tr("Failed to undo trashing instance"),
            tr("Some instances and shortcuts could not be restored.\nPlease check your trashbin to manually restore them."));
    ui->actionUndoTrashInstance->setEnabled(APPLICATION->instances()->trashedSomething());
}

void MainWindow::on_actionViewLauncherRootFolder_triggered()
{
    DesktopServices::openPath(".");
}

void MainWindow::on_actionViewInstanceFolder_triggered()
{
    QString str = APPLICATION->settings()->get("InstanceDir").toString();
    DesktopServices::openPath(str);
}

void MainWindow::on_actionViewCentralModsFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->settings()->get("CentralModsDir").toString(), true);
}

void MainWindow::on_actionViewSkinsFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->settings()->get("SkinsDir").toString(), true);
}

void MainWindow::on_actionViewIconThemeFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->themeManager()->getIconThemesFolder().path(), true);
}

void MainWindow::on_actionViewWidgetThemeFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->themeManager()->getApplicationThemesFolder().path(), true);
}

void MainWindow::on_actionViewCatPackFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->themeManager()->getCatPacksFolder().path(), true);
}

void MainWindow::on_actionViewIconsFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->icons()->getDirectory(), true);
}

void MainWindow::on_actionViewLogsFolder_triggered()
{
    DesktopServices::openPath("logs", true);
}

void MainWindow::on_actionViewJavaFolder_triggered()
{
    DesktopServices::openPath(APPLICATION->javaPath(), true);
}

void MainWindow::refreshInstances()
{
    APPLICATION->instances()->loadList();
}

void MainWindow::checkForUpdates()
{
    if (APPLICATION->updaterEnabled()) {
        APPLICATION->triggerUpdateCheck();
    } else {
        qWarning() << "Updater not set up. Cannot check for updates.";
    }
}

void MainWindow::on_actionSettings_triggered()
{
    APPLICATION->ShowGlobalSettings(this, "global-settings");
}

void MainWindow::globalSettingsClosed()
{
    m_library->setSortMode(APPLICATION->settings()->get("InstSortMode").toString());
    updateLibraryDetails();
    proxymodel->invalidate();
    proxymodel->sort(0);
    updateMainToolBar();
    updateLaunchButton();
    updateThemeMenu();
    updateStatusCenter();
    updateCatState();
    // This needs to be done to prevent UI elements disappearing in the event the config is changed
    // but Prism Launcher exits abnormally, causing the window state to never be saved:
    APPLICATION->settings()->set("MainWindowState", QString::fromUtf8(saveState().toBase64()));
    update();
}

void MainWindow::on_actionEditInstance_triggered()
{
    if (!m_selectedInstance)
        return;

    if (m_selectedInstance->canEdit()) {
        APPLICATION->showInstanceWindow(m_selectedInstance);
    } else {
        CustomMessageBox::selectable(this, tr("Instance not editable"),
                                     tr("This instance is not editable. It may be broken, invalid, or too old. Check logs for details."),
                                     QMessageBox::Critical)
            ->show();
    }
}

void MainWindow::on_actionManageSkins_triggered()
{
    auto account = APPLICATION->accounts()->defaultAccount();

    if (account && (account->accountType() == AccountType::MSA) && !account->isActive()) {
        SkinManageDialog dialog(this, account);
        dialog.exec();
    }
}

void MainWindow::on_actionManageAccounts_triggered()
{
    APPLICATION->ShowGlobalSettings(this, "accounts");
}

void MainWindow::on_actionReportBug_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.BUG_TRACKER_URL));
}

void MainWindow::on_actionClearMetadata_triggered()
{
    // This if contains side effects!
    if (!APPLICATION->metacache()->evictAll()) {
        CustomMessageBox::selectable(this, tr("Error"),
                                     tr("Metadata cache clear Failed!\nTo clear the metadata cache manually, press Folders -> View "
                                        "Launcher Root Folder, and after closing the launcher delete the folder named \"meta\"\n"),
                                     QMessageBox::Warning)
            ->show();
    }

    APPLICATION->metacache()->SaveNow();
}

#ifdef Q_OS_MAC
void MainWindow::on_actionAddToPATH_triggered()
{
    auto binaryPath = APPLICATION->applicationFilePath();
    auto targetPath = QString("/usr/local/bin/%1").arg(BuildConfig.LAUNCHER_APP_BINARY_NAME);
    qDebug() << "Symlinking" << binaryPath << "to" << targetPath;

    QStringList args;
    args << "-e";
    args << QString("do shell script \"mkdir -p /usr/local/bin && ln -sf '%1' '%2'\" with administrator privileges")
                .arg(binaryPath, targetPath);
    auto outcome = QProcess::execute("/usr/bin/osascript", args);
    if (!outcome) {
        QMessageBox::information(this, tr("Successfully added %1 to PATH").arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                                 tr("%1 was successfully added to your PATH. You can now start it by running `%2`.")
                                     .arg(BuildConfig.LAUNCHER_DISPLAYNAME, BuildConfig.LAUNCHER_APP_BINARY_NAME));
    } else {
        QMessageBox::critical(this, tr("Failed to add %1 to PATH").arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                              tr("An error occurred while trying to add %1 to PATH").arg(BuildConfig.LAUNCHER_DISPLAYNAME));
    }
}
#endif

void MainWindow::on_actionOpenWiki_triggered()
{
    DesktopServices::openUrl(QUrl(BuildConfig.WIKI_URL));
}

void MainWindow::on_actionMoreNews_triggered()
{
    auto entries = m_newsChecker->getNewsEntries();
    NewsDialog news_dialog(entries, this);
    news_dialog.exec();
}

void MainWindow::newsButtonClicked()
{
    auto entries = m_newsChecker->getNewsEntries();
    NewsDialog news_dialog(entries, this);
    news_dialog.toggleArticleList();
    news_dialog.exec();
}

void MainWindow::onCatChanged(int)
{
    setCatBackground(APPLICATION->settings()->get("TheCat").toBool());
}

void MainWindow::on_actionAbout_triggered()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_actionDeleteInstance_triggered()
{
    if (!m_selectedInstance) {
        return;
    }
    if (APPLICATION->instances()->isRemoving()) return;

    if (m_selectedInstance->isRunning()) {
        CustomMessageBox::selectable(this, tr("Cannot Delete Running Instance"),
                                     tr("The selected instance is currently running and cannot be deleted. Please stop the instance before "
                                        "attempting to delete it."),
                                     QMessageBox::Warning, QMessageBox::Ok)
            ->exec();
        return;
    }
    auto id = m_selectedInstance->id();

    QString shortcutStr;
    auto shortcuts = m_selectedInstance->shortcuts();
    if (!shortcuts.isEmpty())
        shortcutStr = tr(" and its %n registered shortcut(s)", "", shortcuts.size());
    AwakePopupDialog confirmation(this);
    confirmation.setObjectName("awakeDeleteConfirmation");
    confirmation.setWindowTitle(tr("Delete instance"));
    confirmation.setPanelSize(QSize(540, 330));
    auto* title = new QLabel(tr("Delete this instance?"), confirmation.panel());
    title->setProperty("role", "title");
    confirmation.panelLayout()->addWidget(title);
    auto* name = new QLabel(m_selectedInstance->name(), confirmation.panel());
    name->setTextFormat(Qt::PlainText);
    name->setWordWrap(true);
    name->setStyleSheet("font-size: 17px; font-weight: 600;");
    confirmation.panelLayout()->addWidget(name);
    auto* explanation = new QLabel(tr("This removes the instance%1, including its worlds, mods and settings. "
                                      "Copy any worlds you want to keep before continuing.").arg(shortcutStr), confirmation.panel());
    explanation->setWordWrap(true);
    explanation->setProperty("role", "muted");
    confirmation.panelLayout()->addWidget(explanation);
    confirmation.panelLayout()->addStretch();
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton(tr("Keep instance"), confirmation.panel());
    cancel->setObjectName("keepInstance");
    cancel->setDefault(true);
    auto* remove = new QPushButton(tr("Delete instance"), confirmation.panel());
    remove->setObjectName("deleteInstance");
    remove->setAutoDefault(false);
    remove->setStyleSheet("QPushButton { background: #7f2836; border-color: #c35b6c; } "
                          "QPushButton:hover { background: #993347; } QPushButton:focus { border: 2px solid #fecaca; }");
    buttons->addWidget(cancel);
    buttons->addWidget(remove);
    confirmation.panelLayout()->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &confirmation, &QDialog::reject);
    connect(remove, &QPushButton::clicked, &confirmation, &QDialog::accept);
    cancel->setFocus();
    if (confirmation.exec() != QDialog::Accepted)
        return;

    if (!checkLinkedInstances(id, this, tr("Deleting")))
        return;

    QString error;
    if (!APPLICATION->instances()->removeInstance(id, &error)) {
        CustomMessageBox::selectable(this, tr("Error"), error, QMessageBox::Critical)->show();
        return;
    }
    statusBar()->showMessage(tr("Deleting instance…"));
    connect(APPLICATION->instances(), &InstanceList::removalFinished, this, [this](const QString&, const QString& error) {
        statusBar()->clearMessage();
        ui->actionUndoTrashInstance->setEnabled(APPLICATION->instances()->trashedSomething());
        refreshCurrentInstance();
        if (!error.isEmpty() && !m_webMode)
            CustomMessageBox::selectable(this, tr("Error"), error, QMessageBox::Critical)->show();
    }, Qt::SingleShotConnection);
}

void MainWindow::on_actionExportInstanceZip_triggered()
{
    if (m_selectedInstance) {
        ExportInstanceDialog dlg(m_selectedInstance, this);
        dlg.exec();
    }
}

void MainWindow::on_actionExportInstanceMrPack_triggered()
{
    if (m_selectedInstance) {
        ExportPackDialog dlg(m_selectedInstance, this);
        dlg.exec();
    }
}

void MainWindow::on_actionExportInstanceFlamePack_triggered()
{
    if (m_selectedInstance) {
        if (auto cmp = m_selectedInstance->getPackProfile()->getComponent("net.minecraft");
            cmp && cmp->getVersionFile() && cmp->getVersionFile()->type == "snapshot") {
            QMessageBox msgBox(this);
            msgBox.setText("Snapshots are currently not supported by CurseForge modpacks.");
            msgBox.exec();
            return;
        }
        ExportPackDialog dlg(m_selectedInstance, this, ModPlatform::ResourceProvider::FLAME);
        dlg.exec();
    }
}

void MainWindow::on_actionRenameInstance_triggered()
{
    if (m_selectedInstance) {
        if (m_webMode) {
            const auto before = m_selectedInstance->name();
            bool accepted = false;
            auto name = QInputDialog::getText(this, tr("Rename Instance"), tr("Instance name:"), QLineEdit::Normal, before, &accepted);
            name = name.trimmed();
            name.truncate(128);
            if (accepted && !name.trimmed().isEmpty() && name != before) {
                view->model()->setData(view->currentIndex(), name, Qt::EditRole);
                if (auto* delegate = qobject_cast<ListViewDelegate*>(view->itemDelegate())) delegate->textChanged(before, name);
            }
            return;
        }
        view->edit(view->currentIndex());
    }
}

void MainWindow::on_actionViewSelectedInstFolder_triggered()
{
    if (m_selectedInstance) {
        QString str = m_selectedInstance->instanceRoot();
        DesktopServices::openPath(QFileInfo(str));
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (APPLICATION->instances()->isRemoving()) {
        event->ignore();
        return;
    }
#ifdef AWAKE_WEB_ENABLED
    if (m_webShell) {
        m_webShell->shutdown();
        m_webBridge = nullptr;
    }
#endif
    // Save the window state and geometry.
    APPLICATION->settings()->set("MainWindowState", QString::fromUtf8(saveState().toBase64()));
    APPLICATION->settings()->set("MainWindowGeometry", QString::fromUtf8(saveGeometry().toBase64()));
    event->accept();
    emit isClosing();
}

void MainWindow::changeEvent(QEvent* event)
{
#ifdef AWAKE_WEB_ENABLED
    if (m_webMode && m_webShell && event->type() == QEvent::WindowStateChange)
        m_webShell->setSuspended(isMinimized());
#endif
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::instanceActivated(QModelIndex index)
{
    if (!index.isValid())
        return;
    QString id = index.data(InstanceList::InstanceIDRole).toString();
    MinecraftInstance* inst = APPLICATION->instances()->getInstanceById(id);
    if (!inst)
        return;

    if (APPLICATION->settings()->get("EditInstanceOnDoubleClick").toBool()) {
        if (inst->canEdit()) {
            APPLICATION->showInstanceWindow(inst);
        } else {
            CustomMessageBox::selectable(
                this, tr("Instance not editable"),
                tr("This instance is not editable. It may be broken, invalid, or too old. Check logs for details."), QMessageBox::Critical)
                ->show();
        }
        return;
    }
    APPLICATION->launch(inst);
}

void MainWindow::on_actionLaunchInstance_triggered()
{
    if (m_selectedInstance && !m_selectedInstance->isRunning()) {
        APPLICATION->launch(m_selectedInstance);
    }
}

void MainWindow::on_actionKillInstance_triggered()
{
    if (m_selectedInstance && m_selectedInstance->isRunning()) {
        APPLICATION->kill(m_selectedInstance);
    }
}

void MainWindow::on_actionCreateInstanceShortcut_triggered()
{
    if (!m_selectedInstance)
        return;

    CreateShortcutDialog shortcutDlg(m_selectedInstance, this);
    if (!shortcutDlg.exec())
        return;
    shortcutDlg.createShortcut();
}

void MainWindow::taskEnd()
{
    QObject* sender = QObject::sender();
    if (sender == m_versionLoadTask)
        m_versionLoadTask = NULL;

    sender->deleteLater();
}

void MainWindow::startTask(Task* task)
{
    connect(task, &Task::succeeded, this, &MainWindow::taskEnd);
    connect(task, &Task::failed, this, &MainWindow::taskEnd);
    task->start();
}

void MainWindow::instanceChanged(const QModelIndex& current, [[maybe_unused]] const QModelIndex& previous)
{
    if (!current.isValid()) {
        APPLICATION->settings()->set("SelectedInstance", QString());
        selectionBad();
        return;
    }
    if (m_selectedInstance) {
        disconnect(m_selectedInstance, &BaseInstance::runningStatusChanged, this, &MainWindow::refreshCurrentInstance);
        disconnect(m_selectedInstance, &BaseInstance::profilerChanged, this, &MainWindow::refreshCurrentInstance);
    }
    QString id = current.data(InstanceList::InstanceIDRole).toString();
    m_selectedInstance = APPLICATION->instances()->getInstanceById(id);
    if (m_selectedInstance) {
        ui->instanceToolBar->setEnabled(true);
        setInstanceActionsEnabled(true);
        ui->actionLaunchInstance->setEnabled(m_selectedInstance->canLaunch());

        ui->actionKillInstance->setEnabled(m_selectedInstance->isRunning());
        ui->actionExportInstance->setEnabled(m_selectedInstance->canExport() && !m_selectedInstance->isDeleting());
        renameButton->setText(m_selectedInstance->name());
        m_statusLeft->setText(m_selectedInstance->getStatusbarDescription());
        updateStatusCenter();
        updateInstanceToolIcon(m_selectedInstance->iconKey());

        updateLaunchButton();
        updateLibraryDetails();

        APPLICATION->settings()->set("SelectedInstance", m_selectedInstance->id());

        connect(m_selectedInstance, &BaseInstance::runningStatusChanged, this, &MainWindow::refreshCurrentInstance);
        connect(m_selectedInstance, &BaseInstance::profilerChanged, this, &MainWindow::refreshCurrentInstance);
    } else {
        APPLICATION->settings()->set("SelectedInstance", QString());
        selectionBad();
        return;
    }
}

void MainWindow::instanceSelectRequest(QString id)
{
    setSelectedInstanceById(id);
}

void MainWindow::instanceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    auto current = view->selectionModel()->currentIndex();
    QItemSelection test(topLeft, bottomRight);
    if (test.contains(current)) {
        instanceChanged(current, current);
    }
}

void MainWindow::selectionBad()
{
    // start by reseting everything...
    m_selectedInstance = nullptr;
    if (m_library)
        m_library->clearInstance();
    m_statusLeft->setText(tr("No instance selected"));

    statusBar()->clearMessage();
    ui->instanceToolBar->setEnabled(false);
    setInstanceActionsEnabled(false);
    updateLaunchButton();
    renameButton->setText(tr("Rename Instance"));
    updateInstanceToolIcon("grass");

    // ...and then see if we can enable the previously selected instance
    setSelectedInstanceById(APPLICATION->settings()->get("SelectedInstance").toString());
}

void MainWindow::checkInstancePathForProblems()
{
    QString instanceFolder = APPLICATION->settings()->get("InstanceDir").toString();
    if (FS::checkProblemticPathJava(QDir(instanceFolder))) {
        QMessageBox warning(this);
        warning.setText(tr("Your instance folder contains \'!\' and this is known to cause Java problems!"));
        warning.setInformativeText(tr("You have now two options: <br/>"
                                      " - change the instance folder in the settings <br/>"
                                      " - move this installation of %1 to a different folder")
                                       .arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        warning.setDefaultButton(QMessageBox::Ok);
        warning.exec();
    }
    auto tempFolderText =
        tr("This is a problem: <br/>"
           " - The launcher will likely be deleted without warning by the operating system <br/>"
           " - close the launcher now and extract it to a real location, not a temporary folder");
    QString pathfoldername = QDir(instanceFolder).absolutePath();
    if (pathfoldername.contains("Rar$", Qt::CaseInsensitive)) {
        QMessageBox warning(this);
        warning.setText(tr("Your instance folder contains \'Rar$\' - that means you haven't extracted the launcher archive!"));
        warning.setInformativeText(tempFolderText);
        warning.setDefaultButton(QMessageBox::Ok);
        warning.exec();
    } else if (pathfoldername.startsWith(QDir::tempPath()) || pathfoldername.contains("/TempState/")) {
        QMessageBox warning(this);
        warning.setText(tr("Your instance folder is in a temporary folder: \'%1\'!").arg(QDir::tempPath()));
        warning.setInformativeText(tempFolderText);
        warning.setDefaultButton(QMessageBox::Ok);
        warning.exec();
    }
}

void MainWindow::updateStatusCenter()
{
    m_statusCenter->setVisible(APPLICATION->settings()->get("ShowGlobalGameTime").toBool());
    int64_t timePlayed = APPLICATION->playtimeSettings()->get("TotalPlayTime").toLongLong();
    if (timePlayed > 0) {
        m_statusCenter->setText(
            tr("Total playtime: %1")
                .arg(Time::prettifyDuration(timePlayed, APPLICATION->settings()->get("ShowGameTimeWithoutDays").toBool())));
    }
}

void MainWindow::updateLibraryDetails()
{
#ifdef AWAKE_WEB_ENABLED
    if (m_webMode) {
        if (m_webBridge) m_webBridge->scheduleState();
        return;
    }
#endif
    if (!m_library || !m_selectedInstance)
        return;
    auto* settings = m_selectedInstance->settings();
    const auto javaPath = settings->get("JavaPath").toString();
    const bool automaticJava = APPLICATION->settings()->get("AutomaticJavaSwitch").toBool() &&
                               !(settings->get("OverrideJavaLocation").toBool() && QFileInfo::exists(javaPath));
    const auto java = automaticJava ? tr("Automatic (selected at launch)") : javaPath;
    const auto last = m_selectedInstance->lastLaunch();
    const auto lastPlayed = last > 0 ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(last), QLocale::ShortFormat) : tr("Never played");
    const auto* delegate = dynamic_cast<Awake::LibraryDelegate*>(view->itemDelegate());
    const auto metadata = delegate ? delegate->metadata(view->currentIndex()) : QString();
    m_library->setInstance(m_selectedInstance->name(), metadata.isEmpty() ? m_selectedInstance->getStatusbarDescription() : metadata,
                           java.isEmpty() ? tr("Not configured") : java, tr("%1 MiB").arg(settings->get("MaxMemAlloc").toInt()), lastPlayed,
                           Time::prettifyDuration(m_selectedInstance->totalTimePlayed(), false),
                           APPLICATION->settings()->get("AwakePinnedInstances").toStringList().contains(m_selectedInstance->id()));
    m_library->setArtworkSource(m_selectedInstance->id(), m_selectedInstance->gameRoot());
}
// "Instance actions" are actions that require an instance to be selected (i.e. "new instance" is not here)
// Actions that also require other conditions (e.g. a running instance) won't be changed.
void MainWindow::setInstanceActionsEnabled(bool enabled)
{
    enabled = enabled && m_selectedInstance && !m_selectedInstance->isDeleting();
    ui->actionLaunchInstance->setEnabled(enabled);
    ui->actionKillInstance->setEnabled(enabled);
    ui->actionRenameInstance->setEnabled(enabled);
    ui->actionChangeInstIcon->setEnabled(enabled);
    ui->actionEditInstance->setEnabled(enabled);
    ui->actionChangeInstGroup->setEnabled(enabled);
    ui->actionViewSelectedInstFolder->setEnabled(enabled);
    ui->actionExportInstance->setEnabled(enabled);
    ui->actionDeleteInstance->setEnabled(enabled);
    ui->actionCopyInstance->setEnabled(enabled);
    ui->actionCreateInstanceShortcut->setEnabled(enabled);
}

void MainWindow::refreshCurrentInstance()
{
    auto current = view->selectionModel()->currentIndex();
    instanceChanged(current, current);
}
