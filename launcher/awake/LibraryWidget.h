// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "VisualControls.h"
class QAction;
class QLabel;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QMenu;
namespace Awake {
class LibraryWidget : public ArtworkCanvas {
    Q_OBJECT
   public:
    LibraryWidget(QWidget* instanceView,
                  const QList<QAction*>& management,
                  QAction* launch,
                  QAction* create,
                  QAction* edit,
                  QAction* folder,
                  QAction* settings,
                  QAction* accounts,
                  QMenu* applicationMenu,
                  QWidget* parent = nullptr);
    void setInstance(const QString& name,
                     const QString& description,
                     const QString& java,
                     const QString& memory,
                     const QString& lastPlayed,
                     const QString& playTime,
                     bool pinned);
    void clearInstance();
    void setResultCount(int visible, int total);
    void setViewMode(bool compact);
    void setSortMode(const QString& mode);
    void focusSearch();
   signals:
    void searchChanged(const QString& query);
    void compactChanged(bool compact);
    void sortChanged(const QString& mode);
    void pinnedOnlyChanged(bool enabled);
    void pinChanged(bool pinned);

   protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

   private:
    void retranslate();
    void updateEmptyState();
    void updateArtworkCaption();
    QWidget* m_instanceView;
    GlassSurface* m_navigation;
    GlassSurface* m_dock;
    QLabel* m_heading;
    QLabel* m_count;
    QLabel* m_emptyHint;
    QLabel* m_name;
    QLabel* m_description;
    QLabel* m_runtime;
    QLabel* m_artworkCaption;
    QLineEdit* m_search;
    QComboBox* m_viewMode;
    QComboBox* m_sort;
    QCheckBox* m_pinnedOnly;
    QCheckBox* m_pin;
    MotionButton* m_add;
    MotionButton* m_createHero;
    MotionButton* m_more;
    MotionButton* m_menu;
    QStringList m_detailValues;
    int m_visible = 0;
    int m_total = 0;
};
}  // namespace Awake
