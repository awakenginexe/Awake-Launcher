// SPDX-License-Identifier: GPL-3.0-only
#include "AwakePopupDialog.h"

#include <QEvent>
#include <QApplication>
#include <QColor>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShowEvent>
#include <QSize>
#include <QVBoxLayout>

#include "Application.h"
#include "awake/AwakeTheme.h"
#include "settings/SettingsObject.h"

namespace {
void paintPanel(QPainter& painter, const QRectF& bounds)
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor("#0e182b"));
    painter.setPen(QPen(QColor("#2a3e60"), 1));
    painter.drawRoundedRect(bounds.adjusted(.5, .5, -.5, -.5), 18, 18);
}

void paintClose(QPainter& painter, const QRectF& bounds, const QPushButton* button)
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(button->isDown() ? "#15243a" : button->underMouse() ? "#29456e" : "#1c2e4c"));
    painter.setPen(QPen(QColor(button->hasFocus() ? "#94baff" : "#3a5177"), button->hasFocus() ? 2 : 1));
    painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 8, 8);
    painter.setPen(QPen(QColor("#eaf2ff"), 2, Qt::SolidLine, Qt::RoundCap));
    const auto center = bounds.center();
    painter.drawLine(center + QPointF(-4.5, -4.5), center + QPointF(4.5, 4.5));
    painter.drawLine(center + QPointF(-4.5, 4.5), center + QPointF(4.5, -4.5));
}

class PopupPanel : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        paintPanel(painter, rect());
    }
};

class PopupCloseButton : public QPushButton {
public:
    using QPushButton::QPushButton;
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        paintClose(painter, rect(), this);
    }
};
}

AwakePopupDialog::AwakePopupDialog(QWidget* parent)
    : QDialog(parent ? parent : QApplication::activeWindow()),
      m_anchor(parentWidget() ? parentWidget()->window() : nullptr),
      m_panel(new PopupPanel(this)),
      m_content(new QVBoxLayout)
{
    for (auto* ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (!m_anchor || ancestor->width() * ancestor->height() > m_anchor->width() * m_anchor->height())
            m_anchor = ancestor->window();
    }
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowModality(Qt::WindowModal);
    resize(m_panelSize + QSize(40, 40));
    m_panel->setObjectName("awakePopupPanel");
    const auto regularFont = Awake::useRegularUiFont();
    m_panel->setStyleSheet(QStringLiteral(
        "QWidget { font-family: K2D; %1 }"
        "QWidget#awakePopupPanel { background: #0e182b; border: 1px solid #2a3e60; border-radius: 18px; color: #eef4ff; }"
        "QLabel { color: #dce7f9; background: transparent; }"
        "QLabel[role=title] { font-size: 22px; font-weight: %2; color: #f3f7ff; }"
        "QLabel[role=muted] { color: #b0bfd5; }"
        "QPushButton, QPushButton:default { font-weight: %3; color: #eaf2ff; background: #1c2e4c; border: 1px solid #3a5177; border-radius: 8px; padding: 9px 15px; }"
        "QPushButton:hover { background: #29456e; border-color: #6085bd; }"
        "QPushButton:focus { border: 2px solid #94baff; padding: 8px 14px; }"
        "QPushButton:disabled { color: #91a2bb; background: #15243a; border-color: #293c56; }"
        "QPushButton[primary=true] { background: #326bc9; border-color: #6a9ced; color: white; }"
        "QPushButton[primary=true]:hover { background: #3872d2; }"
        "QScrollArea { background: transparent; border: none; }"
        "QWidget#downloadRows { background: transparent; }"
        "QFrame#downloadRow { background: #14223a; border: 1px solid #293c5a; border-radius: 10px; }")
        .arg(regularFont ? QStringLiteral("font-weight: 400;") : QString{})
        .arg(600)
        .arg(regularFont ? 500 : 400));
    auto* overlay = new QVBoxLayout(this);
    overlay->setContentsMargins(20, 20, 20, 20);
    overlay->addWidget(m_panel, 0, Qt::AlignCenter);
    auto* layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(24, 16, 24, 24);
    auto* top = new QHBoxLayout;
    top->addStretch();
    auto* close = new PopupCloseButton(m_panel);
    close->setObjectName(QStringLiteral("awakePopupClose"));
    m_close = close;
    QPixmap closePixmap(40, 40);
    closePixmap.setDevicePixelRatio(2.0);
    closePixmap.fill(Qt::transparent);
    {
        QPainter iconPainter(&closePixmap);
        QPen pen(QColor("#eaf2ff"), 2.0, Qt::SolidLine, Qt::RoundCap);
        iconPainter.setPen(pen);
        iconPainter.setRenderHint(QPainter::Antialiasing);
        iconPainter.drawLine(QPointF(5.5, 5.5), QPointF(14.5, 14.5));
        iconPainter.drawLine(QPointF(14.5, 5.5), QPointF(5.5, 14.5));
    }
    close->setIcon(QIcon(closePixmap));
    close->setIconSize(QSize(20, 20));
    close->setAccessibleName(QCoreApplication::translate("AwakePopupDialog", "Close dialog"));
    close->setToolTip(QCoreApplication::translate("AwakePopupDialog", "Close (Esc)"));
    close->setFixedSize(36, 36);
    close->setAutoDefault(false);
    close->setStyleSheet(QStringLiteral("QPushButton { font-weight: 400; padding: 0; text-align: center; } QPushButton:focus { padding: 0; }"));
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    top->addWidget(close);
    layout->addLayout(top);
    m_content->setContentsMargins(0, 0, 0, 0);
    m_content->setSpacing(16);
    layout->addLayout(m_content, 1);
    if (m_anchor)
        m_anchor->installEventFilter(this);
}

bool AwakePopupDialog::confirmAccountSetup(QWidget* parent)
{
    AwakePopupDialog popup(parent);
    popup.setObjectName("awakeAccountRequired");
    popup.setPanelSize(QSize(560, 340));
    const auto text = [](const char* source) { return QCoreApplication::translate("AwakePopupDialog", source); };
    auto* heading = new QLabel(text("Add an account to play"), popup.panel());
    heading->setProperty("role", "title");
    heading->setWordWrap(true);
    popup.panelLayout()->addWidget(heading);
    auto* instruction = new QLabel(text("Sign in with the Microsoft account that owns Minecraft. You can add it in the Accounts menu, then come back and press Play."), popup.panel());
    instruction->setWordWrap(true);
    popup.panelLayout()->addWidget(instruction);
    popup.panelLayout()->addStretch();
    auto* actions = new QHBoxLayout;
    actions->addStretch();
    auto* later = new QPushButton(text("Not now"), popup.panel());
    later->setObjectName("accountSetupLater");
    later->setAutoDefault(false);
    connect(later, &QPushButton::clicked, &popup, &QDialog::reject);
    actions->addWidget(later);
    auto* add = new QPushButton(text("Add account"), popup.panel());
    add->setObjectName("accountSetupAdd");
    add->setProperty("primary", true);
    add->setDefault(true);
    connect(add, &QPushButton::clicked, &popup, &QDialog::accept);
    actions->addWidget(add);
    popup.panelLayout()->addLayout(actions);
    add->setFocus();
    return popup.exec() == QDialog::Accepted;
}

void AwakePopupDialog::setPanelSize(const QSize& size)
{
    m_panelSize = size;
    if (!m_anchor)
        resize(m_panelSize + QSize(40, 40));
    syncGeometry();
}

void AwakePopupDialog::setCloseButtonVisible(bool visible)
{
    m_close->setVisible(visible);
}

void AwakePopupDialog::syncGeometry()
{
    if (m_anchor) {
        setGeometry(QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()));
    }
    m_panel->setFixedSize(m_panelSize.boundedTo(QSize(qMax(1, width() - 40), qMax(1, height() - 40))));
}

void AwakePopupDialog::showEvent(QShowEvent* event)
{
    syncGeometry();
    QDialog::showEvent(event);
    if (!APPLICATION->settings()->get("AwakeReduceMotion").toBool()) {
        setWindowOpacity(0);
        auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
        fade->setDuration(150);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        setWindowOpacity(1);
    }
}

void AwakePopupDialog::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(3, 8, 18, 185));
    paintPanel(painter, m_panel->geometry());
    if (m_close->isVisible()) {
        const QRectF closeBounds(m_close->mapTo(this, QPoint(0, 0)), m_close->size());
        paintClose(painter, closeBounds, m_close);
    }
}

bool AwakePopupDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_anchor && (event->type() == QEvent::Move || event->type() == QEvent::Resize) && isVisible())
        syncGeometry();
    return QDialog::eventFilter(watched, event);
}
