// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QElapsedTimer>
#include <QWidget>

class QLabel;
class QTimer;
class QWebEngineView;
class QWebEngineProfile;
class QWebEnginePermission;
class QEvent;
namespace Awake::Web {
class Bridge;
class Assets;
class Shell final : public QWidget {
    Q_OBJECT
public:
    explicit Shell(QWidget* parent = nullptr);
    ~Shell() override;
    Assets* assets() const { return m_assets; }
    bool isReady() const { return m_ready; }
    void start(Bridge* bridge);
    void shutdown();
    void setSuspended(bool suspended);
    void focusSearch();
signals:
    void failed(QString reason);
protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private slots:
    void denyPermission(const QWebEnginePermission& permission);
private:
    void updateArtworkFocus();
    QWebEngineProfile* m_profile;
    QWebEngineView* m_view;
    Assets* m_assets;
    Bridge* m_bridge = nullptr;
    QLabel* m_loading;
    QTimer* m_timeout;
    QElapsedTimer m_started;
    bool m_ready = false;
    bool m_stopped = false;
};
}  // namespace Awake::Web
