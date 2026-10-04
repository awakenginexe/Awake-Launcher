#pragma once

#include <QWidget>
#include <QPoint>

class QLabel;
class QPushButton;

class AwakeTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit AwakeTitleBar(QWidget* targetWindow, QWidget* parent = nullptr);
    ~AwakeTitleBar() override = default;

    void setTitle(const QString& title);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* m_window = nullptr;
    QLabel* m_logoLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QPushButton* m_minButton = nullptr;
    QPushButton* m_maxButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    QPoint m_dragPosition;
};
