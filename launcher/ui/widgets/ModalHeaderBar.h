#pragma once

#include <QWidget>
#include <QPoint>

class QLabel;
class QPushButton;
class QIcon;

class ModalHeaderBar : public QWidget {
    Q_OBJECT
public:
    explicit ModalHeaderBar(QWidget* targetDialog,
                            const QString& titleText,
                            const QIcon& icon = QIcon(),
                            QWidget* parent = nullptr);
    ~ModalHeaderBar() override = default;

    void setTitle(const QString& title);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QWidget* m_dialog = nullptr;
    QLabel* m_titleLabel = nullptr;
    QPushButton* m_closeButton = nullptr;
    QPoint m_dragPosition;
};
