// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QFutureWatcher>
#include <QHash>
#include <QToolButton>
#include <QVariantAnimation>
#include "InstanceArtwork.h"

class QPainter;
namespace Awake {
enum class Glyph { Add, Play, Edit, Folder, Settings, Accounts, More, Search, Menu };
QIcon glyph(Glyph kind, const QColor& color = QColor("#f0f4f8"));

class MotionButton : public QToolButton {
    Q_OBJECT
   public:
    MotionButton(Glyph kind, QWidget* parent, bool primary = false);
    void setReducedMotion(bool reduced);

   protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

   private:
    void animateHover(qreal target);
    QIcon m_symbol;
    QVariantAnimation m_animation;
    qreal m_hover = 0;
    bool m_primary;
    bool m_reducedMotion = false;
};

class ArtworkCanvas : public QWidget {
    Q_OBJECT
   public:
    explicit ArtworkCanvas(QWidget* parent = nullptr);
    ~ArtworkCanvas() override;
    void setArtworkSource(const QString& instanceId, const QString& gameRoot);
    void setReducedMotion(bool reduced);
    QString artworkPath() const { return m_loadedPath; }
    QString artworkInstance() const { return m_id; }
    bool artworkLoading() const { return m_loading; }
    bool animationRunning() const { return m_fade.state() == QAbstractAnimation::Running; }
    void paintGlass(QPainter& painter, QWidget* surface, int radius);
   signals:
    void artworkChanged();

   protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* event) override;

   private:
    void startLoad();
    void transition(QImage image);
    void rebuildLayers();
    void refreshSurfaces();
    QImage cover(const QImage& image) const;
    QImage m_image, m_previous, m_cover, m_previousCover, m_frost, m_previousFrost;
    QString m_id, m_root, m_loadedPath;
    QHash<QString, QString> m_previousFiles;
    QFutureWatcher<Artwork> m_watcher;
    QVariantAnimation m_fade;
    quint64 m_request = 0, m_job = 0;
    qreal m_blend = 1;
    bool m_reducedMotion = false;
    bool m_loading = false;
    bool m_busy = false;
    std::shared_ptr<std::atomic_bool> m_canceled;
};

class GlassSurface : public QWidget {
   public:
    GlassSurface(ArtworkCanvas* canvas, QWidget* parent);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    ArtworkCanvas* m_canvas;
};
}  // namespace Awake
