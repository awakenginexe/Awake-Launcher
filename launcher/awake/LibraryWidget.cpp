// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryWidget.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

namespace Awake {
LibraryWidget::LibraryWidget(QWidget* instanceView,
                             const QList<QAction*>& management,
                             QAction* launch,
                             QAction* create,
                             QAction* edit,
                             QAction* folder,
                             QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(16);
    auto* heading = new QHBoxLayout;
    m_heading = new QLabel(this);
    m_heading->setObjectName("awakeHeading");
    m_count = new QLabel(this);
    m_count->setObjectName("awakeMuted");
    heading->addWidget(m_heading);
    heading->addWidget(m_count);
    heading->addStretch();
    auto* add = new QToolButton(this);
    add->setDefaultAction(create);
    add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    heading->addWidget(add);
    layout->addLayout(heading);

    auto* controls = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setObjectName("awakeSearch");
    m_search->setClearButtonEnabled(true);
    controls->addWidget(m_search, 1);
    m_pinnedOnly = new QCheckBox(this);
    m_pinnedOnly->setObjectName("awakePinnedOnly");
    controls->addWidget(m_pinnedOnly);
    m_sort = new QComboBox(this);
    m_sort->setObjectName("awakeSort");
    controls->addWidget(m_sort);
    m_viewMode = new QComboBox(this);
    m_viewMode->setObjectName("awakeViewMode");
    controls->addWidget(m_viewMode);
    layout->addLayout(controls);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    split->addWidget(instanceView);
    auto* details = new QWidget(split);
    details->setObjectName("awakeDetails");
    details->setMinimumWidth(230);
    auto* detailLayout = new QVBoxLayout(details);
    detailLayout->setContentsMargins(20, 22, 20, 22);
    detailLayout->setSpacing(14);
    m_name = new QLabel(details);
    m_name->setTextFormat(Qt::PlainText);
    m_name->setWordWrap(true);
    m_name->setObjectName("awakeDetailName");
    detailLayout->addWidget(m_name);
    m_description = new QLabel(details);
    m_description->setTextFormat(Qt::PlainText);
    m_description->setWordWrap(true);
    m_description->setObjectName("awakeMuted");
    detailLayout->addWidget(m_description);
    auto* play = new QToolButton(details);
    play->setObjectName("awakeLaunch");
    play->setDefaultAction(launch);
    play->setToolButtonStyle(Qt::ToolButtonTextOnly);
    play->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    detailLayout->addWidget(play);
    m_pin = new QCheckBox(details);
    m_pin->setObjectName("awakePin");
    detailLayout->addWidget(m_pin);
    m_runtime = new QLabel(details);
    m_runtime->setTextFormat(Qt::PlainText);
    m_runtime->setWordWrap(true);
    m_runtime->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_runtime);
    detailLayout->addStretch();
    auto* editButton = new QToolButton(details);
    editButton->setDefaultAction(edit);
    editButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    detailLayout->addWidget(editButton);
    auto* folderButton = new QToolButton(details);
    folderButton->setDefaultAction(folder);
    folderButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    detailLayout->addWidget(folderButton);
    auto* menu = new QMenu(details);
    for (auto* action : management)
        menu->addAction(action);
    auto* more = new QToolButton(details);
    more->setText(tr("More actions"));
    more->setObjectName("awakeMore");
    more->setMenu(menu);
    more->setPopupMode(QToolButton::InstantPopup);
    more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    detailLayout->addWidget(more);
    split->addWidget(details);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({ 650, 280 });
    layout->addWidget(split, 1);

    retranslate();
    clearInstance();
    connect(m_search, &QLineEdit::textChanged, this, &LibraryWidget::searchChanged);
    connect(m_pin, &QCheckBox::toggled, this, &LibraryWidget::pinChanged);
    connect(m_pinnedOnly, &QCheckBox::toggled, this, &LibraryWidget::pinnedOnlyChanged);
    connect(m_viewMode, &QComboBox::currentIndexChanged, this, [this](int index) { emit compactChanged(index == 1); });
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this](int index) { emit sortChanged(m_sort->itemData(index).toString()); });
}

void LibraryWidget::setInstance(const QString& name,
                                const QString& description,
                                const QString& java,
                                const QString& memory,
                                const QString& lastPlayed,
                                const QString& playTime,
                                bool pinned)
{
    m_name->setText(name);
    m_description->setText(description);
    m_detailValues = { java, memory, lastPlayed, playTime };
    m_runtime->setText(tr("Java: %1\nMaximum memory: %2\nLast played: %3\nPlay time: %4").arg(java, memory, lastPlayed, playTime));
    const QSignalBlocker blocker(m_pin);
    m_pin->setEnabled(true);
    findChild<QToolButton*>("awakeMore")->setEnabled(true);
    m_pin->setChecked(pinned);
}

void LibraryWidget::clearInstance()
{
    m_name->setText(tr("Choose an instance"));
    m_description->setText(tr("Create an instance or select one from your library to play and manage it."));
    m_runtime->clear();
    m_detailValues.clear();
    const QSignalBlocker blocker(m_pin);
    m_pin->setEnabled(false);
    findChild<QToolButton*>("awakeMore")->setEnabled(false);
    m_pin->setChecked(false);
}

void LibraryWidget::setResultCount(int visible, int total)
{
    m_visible = visible;
    m_total = total;
    m_count->setText(visible == total ? tr("%n instance(s)", nullptr, total) : tr("%1 of %2 instances").arg(visible).arg(total));
}
void LibraryWidget::setViewMode(bool compact)
{
    const QSignalBlocker blocker(m_viewMode);
    m_viewMode->setCurrentIndex(compact ? 1 : 0);
}
void LibraryWidget::setSortMode(const QString& mode)
{
    const QSignalBlocker blocker(m_sort);
    m_sort->setCurrentIndex(qMax(0, m_sort->findData(mode)));
}
void LibraryWidget::focusSearch()
{
    m_search->setFocus();
    m_search->selectAll();
}
void LibraryWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(event);
}
void LibraryWidget::retranslate()
{
    m_heading->setText(tr("Library"));
    m_search->setPlaceholderText(tr("Search instances or groups"));
    m_search->setAccessibleName(tr("Search instances"));
    m_pinnedOnly->setText(tr("Pinned only"));
    m_pin->setText(tr("Pin to top of group"));
    {
        const QSignalBlocker blocker(m_viewMode);
        const int current = m_viewMode->currentIndex();
        m_viewMode->clear();
        m_viewMode->addItems({ tr("Grid"), tr("List") });
        m_viewMode->setCurrentIndex(qMax(0, current));
        m_viewMode->setAccessibleName(tr("Library view"));
    }
    {
        const QSignalBlocker blocker(m_sort);
        const auto current = m_sort->currentData();
        m_sort->clear();
        m_sort->addItem(tr("Name"), "Name");
        m_sort->addItem(tr("Last played"), "LastLaunch");
        m_sort->addItem(tr("Play time"), "Playtime");
        m_sort->setCurrentIndex(qMax(0, m_sort->findData(current)));
        m_sort->setAccessibleName(tr("Sort instances"));
    }
    if (auto* more = findChild<QToolButton*>("awakeMore"))
        more->setText(tr("More actions"));
    setResultCount(m_visible, m_total);
    if (m_detailValues.isEmpty())
        clearInstance();
    else
        m_runtime->setText(tr("Java: %1\nMaximum memory: %2\nLast played: %3\nPlay time: %4")
                               .arg(m_detailValues[0], m_detailValues[1], m_detailValues[2], m_detailValues[3]));
}
}  // namespace Awake
