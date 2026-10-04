// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>

class QVBoxLayout;
class QPushButton;

class AwakePopupDialog : public QDialog {
   public:
    explicit AwakePopupDialog(QWidget* parent);
    static bool confirmAccountSetup(QWidget* parent);
    QWidget* panel() const { return m_panel; }
    QVBoxLayout* panelLayout() const { return m_content; }
    void setPanelSize(const QSize& size);
    void setCloseButtonVisible(bool visible);

   protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void syncGeometry();
    QWidget* m_anchor;
    QWidget* m_panel;
    QVBoxLayout* m_content;
    QPushButton* m_close;
    QSize m_panelSize{ 760, 600 };
};
