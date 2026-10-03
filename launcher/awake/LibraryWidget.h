// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QWidget>

class QAction;
class QLabel;
class QLineEdit;
class QComboBox;
class QCheckBox;

namespace Awake {
class LibraryWidget : public QWidget {
    Q_OBJECT
   public:
    LibraryWidget(QWidget* instanceView,
                  const QList<QAction*>& management,
                  QAction* launch,
                  QAction* create,
                  QAction* edit,
                  QAction* folder,
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

   private:
    void retranslate();
    QLabel* m_heading;
    QLabel* m_count;
    QLineEdit* m_search;
    QComboBox* m_viewMode;
    QComboBox* m_sort;
    QCheckBox* m_pinnedOnly;
    QLabel* m_name;
    QLabel* m_description;
    QLabel* m_runtime;
    QCheckBox* m_pin;
    QStringList m_detailValues;
    int m_visible = 0;
    int m_total = 0;
};
}  // namespace Awake
