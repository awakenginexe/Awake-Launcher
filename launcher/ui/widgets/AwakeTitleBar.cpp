#include "AwakeTitleBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <QIcon>
#include "Application.h"
#include "BuildConfig.h"

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

AwakeTitleBar::AwakeTitleBar(QWidget* targetWindow, QWidget* parent)
    : QWidget(parent), m_window(targetWindow)
{
    setObjectName("awakeTitleBar");
    setFixedHeight(36);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(8);

    // App Logo
    m_logoLabel = new QLabel(this);
    m_logoLabel->setObjectName("titleBarLogo");
    m_logoLabel->setFixedSize(18, 18);
    m_logoLabel->setPixmap(APPLICATION->logo().pixmap(18, 18));
    layout->addWidget(m_logoLabel, 0, Qt::AlignVCenter);

    const QString titleText = APPLICATION->applicationDisplayName();

    m_titleLabel = new QLabel(titleText, this);
    m_titleLabel->setObjectName("titleBarText");
    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);

    layout->addStretch();

    // MacOS-style traffic light buttons container
    auto* controlsWidget = new QWidget(this);
    controlsWidget->setObjectName("macControls");
    auto* controlsLayout = new QHBoxLayout(controlsWidget);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(8);
    controlsLayout->setAlignment(Qt::AlignVCenter);

    // Minimize (Yellow)
    m_minButton = new QPushButton(controlsWidget);
    m_minButton->setObjectName("macMinBtn");
    m_minButton->setFixedSize(13, 13);
    m_minButton->setText(QStringLiteral("−"));
    m_minButton->setToolTip(tr("Minimize"));
    m_minButton->setCursor(Qt::PointingHandCursor);
    m_minButton->setFocusPolicy(Qt::NoFocus);
    controlsLayout->addWidget(m_minButton, 0, Qt::AlignVCenter);

    // Maximize / Restore (Green)
    m_maxButton = new QPushButton(controlsWidget);
    m_maxButton->setObjectName("macMaxBtn");
    m_maxButton->setFixedSize(13, 13);
    m_maxButton->setText(QStringLiteral("+"));
    m_maxButton->setToolTip(tr("Maximize"));
    m_maxButton->setCursor(Qt::PointingHandCursor);
    m_maxButton->setFocusPolicy(Qt::NoFocus);
    controlsLayout->addWidget(m_maxButton, 0, Qt::AlignVCenter);

    // Close (Red)
    m_closeButton = new QPushButton(controlsWidget);
    m_closeButton->setObjectName("macCloseBtn");
    m_closeButton->setFixedSize(13, 13);
    m_closeButton->setText(QStringLiteral("✕"));
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    controlsLayout->addWidget(m_closeButton, 0, Qt::AlignVCenter);

    layout->addWidget(controlsWidget, 0, Qt::AlignVCenter);

    connect(m_minButton, &QPushButton::clicked, this, [this]() {
        if (m_window) m_window->showMinimized();
    });

    connect(m_maxButton, &QPushButton::clicked, this, [this]() {
        if (!m_window) return;
        if (m_window->isMaximized()) {
            m_window->showNormal();
        } else {
            m_window->showMaximized();
        }
    });

    connect(m_closeButton, &QPushButton::clicked, this, [this]() {
        if (m_window) m_window->close();
    });
}

void AwakeTitleBar::setTitle(const QString& title)
{
    if (m_titleLabel) {
        m_titleLabel->setText(title);
    }
}

void AwakeTitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_window) {
#if defined(Q_OS_WIN)
        ReleaseCapture();
        SendMessage(reinterpret_cast<HWND>(m_window->winId()), WM_NCLBUTTONDOWN, HTCAPTION, 0);
        event->accept();
        return;
#else
        m_dragPosition = event->globalPosition().toPoint() - m_window->frameGeometry().topLeft();
        event->accept();
#endif
    } else {
        QWidget::mousePressEvent(event);
    }
}

void AwakeTitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton && !m_dragPosition.isNull() && m_window) {
        m_window->move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    } else {
        QWidget::mouseMoveEvent(event);
    }
}

void AwakeTitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_window) {
        if (m_window->isMaximized()) {
            m_window->showNormal();
        } else {
            m_window->showMaximized();
        }
        event->accept();
    } else {
        QWidget::mouseDoubleClickEvent(event);
    }
}

void AwakeTitleBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Seamless dark glass top bar
    p.fillRect(rect(), QColor(6, 11, 20, 245));
    p.setPen(QPen(QColor(96, 165, 250, 25), 1));
    p.drawLine(0, height() - 1, width(), height() - 1);
}
