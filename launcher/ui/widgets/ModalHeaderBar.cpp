#include "ModalHeaderBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QDialog>

ModalHeaderBar::ModalHeaderBar(QWidget* targetDialog,
                               const QString& titleText,
                               const QIcon& icon,
                               QWidget* parent)
    : QWidget(parent), m_dialog(targetDialog)
{
    setObjectName("modalHeaderBar");
    setFixedHeight(42);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 10, 0);
    layout->setSpacing(8);

    if (!icon.isNull()) {
        auto* iconLabel = new QLabel(this);
        iconLabel->setObjectName("modalIconLabel");
        iconLabel->setPixmap(icon.pixmap(18, 18));
        iconLabel->setFixedSize(18, 18);
        layout->addWidget(iconLabel);
    }

    m_titleLabel = new QLabel(titleText, this);
    m_titleLabel->setObjectName("modalTitleLabel");
    layout->addWidget(m_titleLabel);

    layout->addStretch();

    m_closeButton = new QPushButton(this);
    m_closeButton->setObjectName("modalCloseButton");
    m_closeButton->setText(QString::fromUtf8("\u2715"));
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_closeButton);

    connect(m_closeButton, &QPushButton::clicked, this, [this]() {
        if (auto* dlg = qobject_cast<QDialog*>(m_dialog)) {
            dlg->reject();
        } else if (m_dialog) {
            m_dialog->close();
        }
    });
}

void ModalHeaderBar::setTitle(const QString& title)
{
    if (m_titleLabel) {
        m_titleLabel->setText(title);
    }
}

void ModalHeaderBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dialog) {
        m_dragPosition = event->globalPosition().toPoint() - m_dialog->frameGeometry().topLeft();
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void ModalHeaderBar::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton && !m_dragPosition.isNull() && m_dialog) {
        m_dialog->move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    } else {
        QWidget::mouseMoveEvent(event);
    }
}
