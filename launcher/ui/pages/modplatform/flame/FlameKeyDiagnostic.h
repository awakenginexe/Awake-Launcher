// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>
#include "ui/pages/BasePage.h"

class NewInstanceDialog;

QWidget* createFlameKeyDiagnostic(QWidget* parent);

class FlameUnavailablePage : public QWidget, public BasePage {
   public:
    explicit FlameUnavailablePage(NewInstanceDialog* dialog);
    QString id() const override { return "flame"; }
    QString displayName() const override { return "CurseForge"; }
    QIcon icon() const override { return QIcon::fromTheme("flame"); }
    QString helpPage() const override { return "APIs"; }
    void openedImpl() override;

   private:
    NewInstanceDialog* m_dialog;
};
