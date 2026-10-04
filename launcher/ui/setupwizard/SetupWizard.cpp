#include "SetupWizard.h"

#include "JavaWizardPage.h"
#include "LanguageWizardPage.h"
#include "LoginWizardPage.h"
#include "ui/widgets/ModalHeaderBar.h"

#include <Application.h>
#include <FileSystem.h>
#include "translations/TranslationsModel.h"

#include <BuildConfig.h>
#include <QAbstractButton>
#include <QBoxLayout>
#include <QGridLayout>
#include <QPainter>

#if defined(Q_OS_WIN)
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

SetupWizard::SetupWizard(QWidget* parent) : QWizard(parent)
{
    setObjectName(QStringLiteral("SetupWizard"));
    resize(580, 640);
    setMinimumSize(500, 540);

    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
#if defined(Q_OS_WIN)
    HWND hwnd = (HWND)winId();
    DWORD corner = 2;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
#endif

    setWizardStyle(QWizard::ClassicStyle);
    setOptions(QWizard::NoCancelButton | QWizard::IndependentPages | QWizard::HaveCustomButton1);
    setOption(QWizard::NoBackButtonOnStartPage);

    m_headerBar = new ModalHeaderBar(this, tr("%1 Setup").arg(BuildConfig.LAUNCHER_DISPLAYNAME), QIcon::fromTheme("accounts"), this);
    if (auto* box = qobject_cast<QBoxLayout*>(layout())) {
        box->insertWidget(0, m_headerBar);
    } else if (auto* grid = qobject_cast<QGridLayout*>(layout())) {
        grid->addWidget(m_headerBar, 0, 0, 1, -1);
    } else if (layout()) {
        layout()->addWidget(m_headerBar);
    }

    if (layout()) {
        layout()->setContentsMargins(12, 4, 12, 12);
        layout()->setSpacing(8);
    }

    retranslate();

    connect(this, &QWizard::currentIdChanged, this, &SetupWizard::pageChanged);
}

void SetupWizard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, QColor(10, 18, 32, 255));
    bg.setColorAt(1.0, QColor(6, 11, 20, 255));
    p.setBrush(bg);
    p.setPen(QPen(QColor(96, 165, 250, 75), 1.5));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
}

void SetupWizard::retranslate()
{
    const bool loginPage = qobject_cast<LoginWizardPage*>(currentPage()) != nullptr;
    setButtonText(QWizard::NextButton, loginPage ? tr("Skip for now") : tr("&Next >"));
    setButtonText(QWizard::BackButton, tr("< &Back"));
    setButtonText(QWizard::FinishButton, loginPage ? tr("Skip for now") : tr("&Finish"));
    setButtonText(QWizard::CustomButton1, tr("&Refresh"));
    setWindowTitle(tr("%1 Quick Setup").arg(BuildConfig.LAUNCHER_DISPLAYNAME));
    if (m_headerBar) {
        m_headerBar->setTitle(tr("%1 Setup").arg(BuildConfig.LAUNCHER_DISPLAYNAME));
    }
}

BaseWizardPage* SetupWizard::getBasePage(int id)
{
    if (id == -1)
        return nullptr;
    auto pagePtr = page(id);
    if (!pagePtr)
        return nullptr;
    return dynamic_cast<BaseWizardPage*>(pagePtr);
}

BaseWizardPage* SetupWizard::getCurrentBasePage()
{
    return getBasePage(currentId());
}

void SetupWizard::pageChanged(int id)
{
    retranslate();
    auto basePagePtr = getBasePage(id);
    if (!basePagePtr) {
        return;
    }
    if (basePagePtr->wantsRefreshButton()) {
        setButtonLayout({ QWizard::CustomButton1, QWizard::Stretch, QWizard::BackButton, QWizard::NextButton, QWizard::FinishButton });
        auto customButton = button(QWizard::CustomButton1);
        connect(customButton, &QAbstractButton::clicked, this, [this]() {
            auto basePagePtr = getCurrentBasePage();
            if (basePagePtr) {
                basePagePtr->refresh();
            }
        });
    } else {
        setButtonLayout({ QWizard::Stretch, QWizard::BackButton, QWizard::NextButton, QWizard::FinishButton });
    }
}

void SetupWizard::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWizard::changeEvent(event);
}

SetupWizard::~SetupWizard() {}
