// SPDX-License-Identifier: GPL-3.0-only
#include "FlameKeyDiagnostic.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include "Application.h"
#include "ui/dialogs/NewInstanceDialog.h"

QWidget* createFlameKeyDiagnostic(QWidget* parent)
{
    auto* notice = new QWidget(parent);
    auto* layout = new QVBoxLayout(notice);
    auto* message = new QLabel(QCoreApplication::translate("FlameKeyDiagnostic",
        "CurseForge needs an API key. This Awake build does not include one. "
        "Add your key under Settings → Services → API Keys → CurseForge, then reopen this dialog."), notice);
    message->setObjectName("curseforgeKeyDiagnostic");
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    auto* configure = new QPushButton(QCoreApplication::translate("FlameKeyDiagnostic", "Configure API key"), notice);
    configure->setAutoDefault(false);
    layout->addWidget(message);
    layout->addWidget(configure, 0, Qt::AlignLeft);
    QObject::connect(configure, &QPushButton::clicked, notice, [notice, message] {
        APPLICATION->ShowGlobalSettings(notice, "apis");
        if (APPLICATION->capabilities() & Application::SupportsFlame) {
            message->setText(QCoreApplication::translate("FlameKeyDiagnostic",
                "A CurseForge API key is configured. Reopen this dialog to load CurseForge."));
        }
    });
    return notice;
}

FlameUnavailablePage::FlameUnavailablePage(NewInstanceDialog* dialog) : QWidget(dialog), m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(createFlameKeyDiagnostic(this));
    layout->addStretch();
}

void FlameUnavailablePage::openedImpl()
{
    m_dialog->setSuggestedPack();
}
