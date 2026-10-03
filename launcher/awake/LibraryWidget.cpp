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
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace Awake {
LibraryWidget::LibraryWidget(QWidget* instanceView,
                             const QList<QAction*>& management,
                             QAction* launch,
                             QAction* create,
                             QAction* edit,
                             QAction* folder,
                             QAction* settings,
                             QAction* accounts,
                             QMenu* applicationMenu,
                             QWidget* parent)
    : ArtworkCanvas(parent), m_instanceView(instanceView)
{
    setObjectName("awakeCanvas");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 18, 26, 24);
    layout->setSpacing(30);
    m_navigation = new GlassSurface(this, this);
    m_navigation->setObjectName("awakeNavigation");
    m_navigation->setFixedWidth(280);
    auto* navigation = new QVBoxLayout(m_navigation);
    navigation->setContentsMargins(18, 22, 18, 18);
    navigation->setSpacing(16);
    auto* brand = new QHBoxLayout;
    auto* mark = new QLabel("A", m_navigation);
    mark->setObjectName("awakeMark");
    mark->setAlignment(Qt::AlignCenter);
    mark->setFixedSize(38, 38);
    auto* wordmark = new QLabel("AWAKE", m_navigation);
    wordmark->setObjectName("awakeWordmark");
    brand->addWidget(mark);
    brand->addSpacing(8);
    brand->addWidget(wordmark);
    brand->addStretch();
    auto* add = new MotionButton(Glyph::Add, m_navigation);
    add->setDefaultAction(create);
    add->setFixedWidth(42);
    brand->addWidget(add);
    navigation->addLayout(brand);
    auto* heading = new QHBoxLayout;
    m_heading = new QLabel(m_navigation);
    m_heading->setObjectName("awakeHeading");
    m_count = new QLabel(m_navigation);
    m_count->setObjectName("awakeMuted");
    heading->addWidget(m_heading);
    heading->addStretch();
    navigation->addSpacing(10);
    navigation->addLayout(heading);
    navigation->addWidget(m_count);
    m_search = new QLineEdit(m_navigation);
    m_search->setObjectName("awakeSearch");
    m_search->setClearButtonEnabled(true);
    m_search->addAction(glyph(Glyph::Search), QLineEdit::LeadingPosition);
    m_search->setMinimumHeight(40);
    navigation->addWidget(m_search);
    auto* controls = new QHBoxLayout;
    m_sort = new QComboBox(m_navigation);
    m_sort->setObjectName("awakeSort");
    m_viewMode = new QComboBox(m_navigation);
    m_viewMode->setObjectName("awakeViewMode");
    controls->addWidget(m_sort, 1);
    controls->addWidget(m_viewMode);
    navigation->addLayout(controls);
    m_pinnedOnly = new QCheckBox(m_navigation);
    m_pinnedOnly->setObjectName("awakePinnedOnly");
    navigation->addWidget(m_pinnedOnly);
    navigation->addWidget(instanceView, 1);
    m_emptyHint = new QLabel(m_navigation);
    m_emptyHint->setObjectName("awakeEmptyHint");
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setAlignment(Qt::AlignTop);
    navigation->addWidget(m_emptyHint, 1);
    auto* settingsButton = new MotionButton(Glyph::Settings, m_navigation);
    settingsButton->setDefaultAction(settings);
    settingsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    settingsButton->setMinimumWidth(150);
    settingsButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    navigation->addWidget(settingsButton);
    layout->addWidget(m_navigation);
    auto* hero = new QVBoxLayout;
    hero->setContentsMargins(0, 5, 0, 0);
    hero->setSpacing(22);
    auto* top = new QHBoxLayout;
    m_artworkCaption = new QLabel(this);
    m_artworkCaption->setObjectName("awakeArtworkCaption");
    m_artworkCaption->setTextFormat(Qt::PlainText);
    top->addWidget(m_artworkCaption);
    top->addStretch();
    auto* accountButton = new MotionButton(Glyph::Accounts, this);
    accountButton->setDefaultAction(accounts);
    accountButton->setObjectName("awakeAccounts");
    accountButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    accountButton->setPopupMode(QToolButton::InstantPopup);
    accountButton->setMinimumWidth(140);
    accountButton->setMaximumWidth(210);
    top->addWidget(accountButton);
    m_menu = new MotionButton(Glyph::Menu, this);
    m_menu->setObjectName("awakeApplicationMenu");
    m_menu->setMenu(applicationMenu);
    m_menu->setPopupMode(QToolButton::InstantPopup);
    m_menu->setFixedWidth(44);
    top->addWidget(m_menu);
    hero->addLayout(top);
    hero->addSpacing(28);
    m_name = new QLabel(this);
    m_name->setObjectName("awakeDetailName");
    m_name->setTextFormat(Qt::PlainText);
    m_name->setWordWrap(true);
    hero->addWidget(m_name);
    m_description = new QLabel(this);
    m_description->setObjectName("awakeWorldDescription");
    m_description->setTextFormat(Qt::PlainText);
    m_description->setWordWrap(true);
    hero->addWidget(m_description);
    hero->addStretch(1);
    m_createHero = new MotionButton(Glyph::Add, this, true);
    m_createHero->setDefaultAction(create);
    m_createHero->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_createHero->setMinimumWidth(220);
    m_createHero->setObjectName("awakeCreateHero");
    hero->addWidget(m_createHero, 0, Qt::AlignLeft);
    m_dock = new GlassSurface(this, this);
    m_dock->setObjectName("awakeLaunchDock");
    m_dock->setMaximumWidth(560);
    auto* dock = new QVBoxLayout(m_dock);
    dock->setContentsMargins(20, 18, 20, 20);
    dock->setSpacing(14);
    m_runtime = new QLabel(m_dock);
    m_runtime->setObjectName("awakeRuntime");
    m_runtime->setTextFormat(Qt::PlainText);
    m_runtime->setWordWrap(true);
    m_runtime->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dock->addWidget(m_runtime);
    m_pin = new QCheckBox(m_dock);
    m_pin->setObjectName("awakePin");
    dock->addWidget(m_pin);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(10);
    auto* play = new MotionButton(Glyph::Play, m_dock, true);
    play->setObjectName("awakeLaunch");
    play->setDefaultAction(launch);
    play->setPopupMode(QToolButton::MenuButtonPopup);
    play->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    play->setMinimumWidth(190);
    play->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    actions->addWidget(play, 1);
    auto* editButton = new MotionButton(Glyph::Edit, m_dock);
    editButton->setDefaultAction(edit);
    editButton->setFixedWidth(44);
    actions->addWidget(editButton);
    auto* folderButton = new MotionButton(Glyph::Folder, m_dock);
    folderButton->setDefaultAction(folder);
    folderButton->setFixedWidth(44);
    actions->addWidget(folderButton);
    auto* menu = new QMenu(m_dock);
    for (auto* action : management)
        menu->addAction(action);
    m_more = new MotionButton(Glyph::More, m_dock);
    m_more->setObjectName("awakeMore");
    m_more->setMenu(menu);
    m_more->setPopupMode(QToolButton::InstantPopup);
    m_more->setFixedWidth(44);
    actions->addWidget(m_more);
    dock->addLayout(actions);
    hero->addWidget(m_dock, 0, Qt::AlignRight);
    layout->addLayout(hero, 1);
    retranslate();
    clearInstance();
    connect(this, &ArtworkCanvas::artworkChanged, this, &LibraryWidget::updateArtworkCaption);
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
    m_dock->show();
    m_createHero->hide();
    const QSignalBlocker blocker(m_pin);
    m_pin->setEnabled(true);
    m_more->setEnabled(true);
    m_pin->setChecked(pinned);
}
void LibraryWidget::clearInstance()
{
    m_runtime->clear();
    m_detailValues.clear();
    const QSignalBlocker blocker(m_pin);
    m_pin->setEnabled(false);
    m_more->setEnabled(false);
    m_pin->setChecked(false);
    setArtworkSource({}, {});
    updateEmptyState();
}
void LibraryWidget::updateEmptyState()
{
    m_instanceView->setVisible(m_visible > 0);
    m_emptyHint->setVisible(m_visible == 0);
    m_emptyHint->setText(m_total == 0 ? tr("Your library starts here. Create an instance to add your first world.")
                                      : tr("No instances match your search."));
    if (!m_detailValues.isEmpty())
        return;
    m_dock->hide();
    m_createHero->setVisible(m_total == 0);
    m_name->setText(m_total == 0 ? tr("Your worlds, within reach.") : tr("Choose an instance"));
    m_description->setText(m_total == 0 ? tr("Create your first instance to start playing.")
                                        : tr("Choose a world from your library to play and manage it."));
}
void LibraryWidget::updateArtworkCaption()
{
    m_artworkCaption->setText(artworkLoading()          ? tr("Loading screenshots…")
                              : artworkPath().isEmpty() ? tr("No screenshot available")
                                                        : tr("Screenshots from this instance"));
    m_artworkCaption->setVisible(!artworkInstance().isEmpty());
    m_artworkCaption->setToolTip(artworkPath());
}
void LibraryWidget::setResultCount(int visible, int total)
{
    m_visible = visible;
    m_total = total;
    m_count->setText(visible == total ? tr("%n instance(s)", nullptr, total) : tr("%1 of %2 instances").arg(visible).arg(total));
    updateEmptyState();
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
void LibraryWidget::resizeEvent(QResizeEvent* event)
{
    m_navigation->setFixedWidth(width() < 1000 ? 246 : 280);
    ArtworkCanvas::resizeEvent(event);
}
void LibraryWidget::retranslate()
{
    m_heading->setText(tr("Library"));
    m_search->setPlaceholderText(tr("Search instances or groups"));
    m_search->setAccessibleName(tr("Search instances"));
    m_pinnedOnly->setText(tr("Pinned only"));
    m_pin->setText(tr("Pin to top of group"));
    m_more->setToolTip(tr("More actions"));
    m_more->setAccessibleName(tr("More actions"));
    m_menu->setToolTip(tr("Launcher menu"));
    m_menu->setAccessibleName(tr("Launcher menu"));
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
    setResultCount(m_visible, m_total);
    updateArtworkCaption();
    if (!m_detailValues.isEmpty())
        m_runtime->setText(tr("Java: %1\nMaximum memory: %2\nLast played: %3\nPlay time: %4")
                               .arg(m_detailValues[0], m_detailValues[1], m_detailValues[2], m_detailValues[3]));
}
}  // namespace Awake
