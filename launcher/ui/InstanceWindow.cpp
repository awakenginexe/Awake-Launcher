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

#include "InstanceWindow.h"
#include "Application.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>

#include "ui/widgets/AwakeTitleBar.h"
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif

#include "ui/widgets/PageContainer.h"

#include "InstancePageProvider.h"

#include "icons/IconList.h"

InstanceWindow::InstanceWindow(MinecraftInstance* instance, QWidget* parent) : QMainWindow(parent), m_instance(instance)
{
    setAttribute(Qt::WA_DeleteOnClose);

    auto icon = APPLICATION->icons()->getIcon(m_instance->iconKey());
    QString windowTitle = tr("Console window for ") + m_instance->name();

    // Set window properties
    {
        setWindowIcon(icon);
        setWindowTitle(windowTitle);
    }

#if defined(Q_OS_WIN)
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    m_frameless = true;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    DWORD cornerPref = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));
#endif

    // Add page container
    {
        auto provider = std::make_shared<InstancePageProvider>(m_instance);
        m_container = new PageContainer(provider.get(), "console", this);
        m_container->setParentContainer(this);

        auto* outerWidget = new QWidget(this);
        outerWidget->setObjectName("awakeOuterContainer");
        auto* outerLayout = new QVBoxLayout(outerWidget);
        outerLayout->setContentsMargins(0, 0, 0, 0);
        outerLayout->setSpacing(0);
        m_titleBar = new AwakeTitleBar(this, outerWidget);
        m_titleBar->setTitle(windowTitle);
        outerLayout->addWidget(m_titleBar);
        outerLayout->addWidget(m_container);
        setCentralWidget(outerWidget);
        setContentsMargins(0, 0, 0, 0);
    }

    // Add custom buttons to the page container layout.
    {
        auto horizontalLayout = new QHBoxLayout(this);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(12, 8, 16, 12);
        horizontalLayout->setSpacing(8);

        auto btnHelp = new QPushButton(this);
        btnHelp->setText(tr("Help"));
        horizontalLayout->addWidget(btnHelp);
        connect(btnHelp, &QPushButton::clicked, m_container, &PageContainer::help);

        auto spacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        horizontalLayout->addSpacerItem(spacer);

        m_launchButton = new QToolButton(this);
        m_launchButton->setObjectName("btnPrimaryLaunch");
        m_launchButton->setText(tr("&Launch"));
        m_launchButton->setToolTip(tr("Launch the instance"));
        m_launchButton->setPopupMode(QToolButton::MenuButtonPopup);
        m_launchButton->setMinimumWidth(80);  // HACK!!
        horizontalLayout->addWidget(m_launchButton);
        connect(m_launchButton, &QPushButton::clicked, this, [this] { APPLICATION->launch(m_instance); });

        m_restartButton = new QPushButton(this);
        m_restartButton->setText(tr("&Restart"));
        m_restartButton->setToolTip(tr("Restart the running instance"));
        horizontalLayout->addWidget(m_restartButton);
        connect(m_restartButton, &QPushButton::clicked, this, &InstanceWindow::restartInstance);

        m_restartButton->hide();

        m_killButton = new QPushButton(this);
        m_killButton->setText(tr("&Kill"));
        m_killButton->setToolTip(tr("Kill the running instance"));
        m_killButton->setShortcut(QKeySequence(tr("Ctrl+K")));
        horizontalLayout->addWidget(m_killButton);
        connect(m_killButton, &QPushButton::clicked, this, [this] { APPLICATION->kill(m_instance); });

        updateButtons();

        m_closeButton = new QPushButton(this);
        m_closeButton->setText(tr("Close"));
        horizontalLayout->addWidget(m_closeButton);
        connect(m_closeButton, &QPushButton::clicked, this, &QMainWindow::close);

        m_container->addButtons(horizontalLayout);

        connect(m_instance, &BaseInstance::profilerChanged, this, &InstanceWindow::updateButtons);
        connect(APPLICATION, &Application::globalSettingsApplied, this, &InstanceWindow::updateButtons);
    }

    // restore window state
    {
        auto base64State = APPLICATION->settings()->get("ConsoleWindowState").toString().toUtf8();
        restoreState(QByteArray::fromBase64(base64State));
        auto base64Geometry = APPLICATION->settings()->get("ConsoleWindowGeometry").toString().toUtf8();
        restoreGeometry(QByteArray::fromBase64(base64Geometry));
    }

    // set up instance and launch process recognition
    {
        auto launchTask = m_instance->getLaunchTask();
        instanceLaunchTaskChanged(launchTask);
        connect(m_instance, &BaseInstance::launchTaskChanged, this, &InstanceWindow::instanceLaunchTaskChanged);
        connect(m_instance, &BaseInstance::runningStatusChanged, this, &InstanceWindow::runningStateChanged);
    }

    // set up instance destruction detection
    {
        connect(m_instance, &BaseInstance::statusChanged, this, &InstanceWindow::on_instanceStatusChanged);
        const auto updateDeletion = [this] {
            const bool deleting = m_instance->isDeleting();
            m_container->setSavingEnabled(!deleting);
            m_container->setEnabled(!deleting);
            updateButtons();
        };
        connect(m_instance, &BaseInstance::propertiesChanged, this, updateDeletion);
        updateDeletion();
    }

    // add ourself as the modpack page's instance window
    {
        static_cast<ManagedPackPage*>(m_container->getPage("managed_pack"))->setInstanceWindow(this);
    }

    show();
}

void InstanceWindow::on_instanceStatusChanged(BaseInstance::Status, BaseInstance::Status newStatus)
{
    if (newStatus == BaseInstance::Status::Gone) {
        m_doNotSave = true;
        close();
    }
}

void InstanceWindow::updateButtons()
{
    const bool running = m_instance->isRunning();

    m_launchButton->setVisible(!running);
    m_restartButton->setVisible(running);

    m_launchButton->setEnabled(m_instance->canLaunch());
    m_restartButton->setEnabled(running && !m_restartQueued);
    m_killButton->setEnabled(running);

    QMenu* launchMenu = m_launchButton->menu();
    if (launchMenu)
        launchMenu->clear();
    else
        launchMenu = new QMenu(this);
    m_instance->populateLaunchMenu(launchMenu);
    m_launchButton->setMenu(launchMenu);
}

void InstanceWindow::instanceLaunchTaskChanged(LaunchTask* proc)
{
    m_proc = proc;
}

void InstanceWindow::runningStateChanged(bool running)
{
    updateButtons();
    m_container->refreshContainer();
    if (running) {
        selectPage("log");
    } else if (m_restartQueued) {
        m_restartQueued = false;
        // Wait until the current launch controller has handled the stopped instance before launching again.
        QTimer::singleShot(0, this, [this] {
            APPLICATION->launch(m_instance);
            updateButtons();
        });
    }
}

void InstanceWindow::restartInstance()
{
    if (!m_instance->isRunning()) {
        return;
    }

    m_restartQueued = APPLICATION->kill(m_instance);
    updateButtons();
}

void InstanceWindow::closeEvent(QCloseEvent* event)
{
    if (!m_doNotSave && m_instance->isDeleting()) {
        event->ignore();
        return;
    }
    bool proceed = true;
    if (!m_doNotSave) {
        proceed &= m_container->prepareToClose();
    }

    if (!proceed) {
        return;
    }

    APPLICATION->settings()->set("ConsoleWindowState", QString::fromUtf8(saveState().toBase64()));
    APPLICATION->settings()->set("ConsoleWindowGeometry", QString::fromUtf8(saveGeometry().toBase64()));
    emit isClosing();
    event->accept();
}

bool InstanceWindow::saveAll()
{
    if (m_doNotSave || m_instance->isDeleting()) return false;
    return m_container->saveAll();
}

QString InstanceWindow::instanceId()
{
    return m_instance->id();
}

bool InstanceWindow::selectPage(QString pageId)
{
    return m_container->selectPage(pageId);
}

void InstanceWindow::refreshContainer()
{
    m_container->refreshContainer();
}

BasePage* InstanceWindow::selectedPage() const
{
    return m_container->selectedPage();
}

bool InstanceWindow::requestClose()
{
    if (m_doNotSave) {
        close();
        return true;
    }
    if (m_instance->isDeleting()) return false;
    if (m_container->prepareToClose()) {
        close();
        return true;
    }
    return false;
}

#if defined(Q_OS_WIN)
bool InstanceWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
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
