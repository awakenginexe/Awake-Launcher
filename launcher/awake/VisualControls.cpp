// SPDX-License-Identifier: GPL-3.0-only
#include "VisualControls.h"
#include <QAction>
#include <QEnterEvent>
#include <QIconEngine>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

namespace Awake {
namespace {
class GlyphEngine : public QIconEngine {
   public:
    GlyphEngine(Glyph kind, QColor color) : m_kind(kind), m_color(color) {}
    QIconEngine* clone() const override { return new GlyphEngine(m_kind, m_color); }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->translate(rect.topLeft());
        p->scale(rect.width() / 24.0, rect.height() / 24.0);
        auto ink = m_color;
        if (mode == QIcon::Disabled)
            ink.setAlpha(105);
        p->setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        QPainterPath path;
        switch (m_kind) {
            case Glyph::Add:
                p->drawLine(12, 5, 12, 19);
                p->drawLine(5, 12, 19, 12);
                break;
            case Glyph::Play:
                path.moveTo(8, 5);
                path.lineTo(19, 12);
                path.lineTo(8, 19);
                path.closeSubpath();
                p->fillPath(path, ink);
                break;
            case Glyph::Folder:
                path.moveTo(3, 7);
                path.lineTo(3, 19);
                path.lineTo(21, 19);
                path.lineTo(21, 7);
                path.lineTo(11, 7);
                path.lineTo(9, 4);
                path.lineTo(3, 4);
                path.closeSubpath();
                p->drawPath(path);
                break;
            case Glyph::Edit:
                path.moveTo(5, 16);
                path.lineTo(16, 5);
                path.lineTo(20, 9);
                path.lineTo(9, 20);
                path.lineTo(4, 21);
                path.closeSubpath();
                p->drawPath(path);
                p->drawLine(14, 7, 18, 11);
                break;
            case Glyph::Settings:
                p->drawEllipse(QRectF(7, 7, 10, 10));
                p->drawEllipse(QRectF(10, 10, 4, 4));
                for (int i = 0; i < 8; ++i) {
                    const double a = i * 3.141592653589793 / 4;
                    p->drawLine(QPointF(12 + 7 * std::cos(a), 12 + 7 * std::sin(a)), QPointF(12 + 9 * std::cos(a), 12 + 9 * std::sin(a)));
                }
                break;
            case Glyph::Accounts:
                p->drawEllipse(QRectF(8, 3, 8, 8));
                path.moveTo(4, 21);
                path.cubicTo(4, 10, 20, 10, 20, 21);
                p->drawPath(path);
                break;
            case Glyph::Search:
                p->drawEllipse(QRectF(4, 4, 12, 12));
                p->drawLine(14, 14, 21, 21);
                break;
            case Glyph::More:
                p->setBrush(ink);
                p->setPen(Qt::NoPen);
                for (int x : { 5, 12, 19 })
                    p->drawEllipse(QPointF(x, 12), 1.5, 1.5);
                break;
            case Glyph::Menu:
                p->drawLine(4, 6, 20, 6);
                p->drawLine(4, 12, 20, 12);
                p->drawLine(4, 18, 20, 18);
                break;
        }
        p->restore();
    }

   private:
    Glyph m_kind;
    QColor m_color;
};
}  // namespace

QIcon glyph(Glyph kind, const QColor& color)
{
    return QIcon(new GlyphEngine(kind, color));
}

MotionButton::MotionButton(Glyph kind, QWidget* parent, bool primary)
    : QToolButton(parent), m_symbol(glyph(kind, primary ? QColor("#101b20") : QColor("#f0f4f8"))), m_primary(primary)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(primary ? 56 : 42);
    setIcon(m_symbol);
    setIconSize(QSize(20, 20));
    setStyleSheet("QToolButton { background: transparent; border: none; padding: 8px 12px; }");
    m_animation.setDuration(140);
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_hover = value.toReal();
        update();
    });
}
void MotionButton::animateHover(qreal target)
{
    m_animation.stop();
    if (m_reducedMotion) {
        m_hover = target;
        update();
        return;
    }
    m_animation.setStartValue(m_hover);
    m_animation.setEndValue(target);
    m_animation.start();
}
void MotionButton::setReducedMotion(bool reduced)
{
    m_reducedMotion = reduced;
    if (reduced) {
        m_animation.stop();
        m_hover = underMouse() ? 1 : 0;
        update();
    }
}
void MotionButton::enterEvent(QEnterEvent* event)
{
    animateHover(1);
    QToolButton::enterEvent(event);
}
void MotionButton::leaveEvent(QEvent* event)
{
    animateHover(0);
    QToolButton::leaveEvent(event);
}
void MotionButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds = QRectF(rect()).adjusted(1, 1, -1, -1);
    const auto hover = isEnabled() ? m_hover : 0;
    auto background = m_primary ? QColor("#a5e1ca") : QColor(255, 255, 255, int(12 + hover * 24));
    if (m_primary && !isEnabled())
        background = QColor(70, 80, 89, 150);
    if (m_primary && hover > 0)
        background = QColor::fromRgbF(0.647 + hover * 0.09, 0.882 + hover * 0.05, 0.792 + hover * 0.06);
    p.setBrush(background);
    p.setPen(QPen(hasFocus() ? QColor("#a5e1ca") : QColor(255, 255, 255, int(35 + hover * 35)), hasFocus() ? 2 : 1));
    p.drawRoundedRect(bounds, m_primary ? 16 : 11, m_primary ? 16 : 11);
    const bool text = toolButtonStyle() != Qt::ToolButtonIconOnly;
    const bool arrow = menu() != nullptr;
    const int iconX = text ? 16 : (width() - 20) / 2;
    m_symbol.paint(&p, QRect(iconX, (height() - 20) / 2, 20, 20), Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
    if (text) {
        auto f = font();
        f.setWeight(m_primary ? QFont::Bold : QFont::Medium);
        p.setFont(f);
        p.setPen(isEnabled() ? (m_primary ? QColor("#101b20") : QColor("#f0f4f8")) : QColor("#aab6c3"));
        const auto area = rect().adjusted(46, 0, arrow ? -27 : -14, 0);
        p.drawText(area, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(this->text(), Qt::ElideRight, area.width()));
    }
    if (arrow) {
        p.setPen(QPen(m_primary ? QColor("#101b20") : QColor("#f0f4f8"), 1.5));
        p.drawLine(width() - 20, height() / 2 - 2, width() - 16, height() / 2 + 2);
        p.drawLine(width() - 16, height() / 2 + 2, width() - 12, height() / 2 - 2);
    }
}

ArtworkCanvas::ArtworkCanvas(QWidget* parent) : QWidget(parent)
{
    m_fade.setDuration(320);
    m_fade.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_blend = value.toReal();
        refreshSurfaces();
    });
    connect(&m_fade, &QVariantAnimation::finished, this, [this] {
        m_previous = {};
        m_previousCover = {};
        m_previousFrost = {};
    });
    connect(&m_watcher, &QFutureWatcher<Artwork>::finished, this, [this] {
        m_busy = false;
        if (m_job == m_request) {
            auto artwork = m_watcher.result();
            m_loadedPath = artwork.path;
            if (!artwork.path.isEmpty())
                m_previousFiles.insert(m_id, artwork.path);
            m_loading = false;
            transition(std::move(artwork.image));
            emit artworkChanged();
        } else if (!m_root.isEmpty()) {
            startLoad();
        }
    });
}
ArtworkCanvas::~ArtworkCanvas()
{
    if (m_canceled)
        m_canceled->store(true);
}
void ArtworkCanvas::setArtworkSource(const QString& instanceId, const QString& gameRoot)
{
    if (instanceId == m_id && gameRoot == m_root)
        return;
    m_id = instanceId;
    m_root = gameRoot;
    m_loadedPath.clear();
    ++m_request;
    if (m_canceled)
        m_canceled->store(true);
    m_loading = !m_root.isEmpty();
    if (m_root.isEmpty())
        transition({});
    else if (!m_busy)
        startLoad();
    emit artworkChanged();
}
void ArtworkCanvas::startLoad()
{
    m_job = m_request;
    m_busy = true;
    m_canceled = std::make_shared<std::atomic_bool>(false);
    const auto root = m_root;
    const auto previous = m_previousFiles.value(m_id);
    const auto canceled = m_canceled;
    m_watcher.setFuture(QtConcurrent::run([root, previous, canceled] { return loadRandomScreenshot(root, previous, canceled); }));
}
void ArtworkCanvas::setReducedMotion(bool reduced)
{
    m_reducedMotion = reduced;
    for (auto* button : findChildren<MotionButton*>())
        button->setReducedMotion(reduced);
    if (reduced) {
        m_fade.stop();
        m_blend = 1;
        m_previous = {};
        m_previousCover = {};
        m_previousFrost = {};
        refreshSurfaces();
    }
}
QImage ArtworkCanvas::cover(const QImage& image) const
{
    if (width() <= 0 || height() <= 0)
        return {};
    QImage result(size(), QImage::Format_RGB32);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!image.isNull()) {
        const auto scaled = image.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
        const auto scale = qreal(scaled.width()) / image.width();
        const QRectF source((image.width() - width() / scale) / 2, (image.height() - height() / scale) / 2, width() / scale,
                            height() / scale);
        p.drawImage(QRectF(rect()), image, source);
    } else {
        QLinearGradient sky(0, 0, width(), height());
        sky.setColorAt(0, QColor("#253846"));
        sky.setColorAt(0.6, QColor("#182630"));
        sky.setColorAt(1, QColor("#101a23"));
        p.fillRect(rect(), sky);
        p.setPen(Qt::NoPen);
        QPainterPath ridge;
        ridge.moveTo(width() * .2, height());
        ridge.lineTo(width() * .58, height() * .34);
        ridge.lineTo(width() * .68, height() * .5);
        ridge.lineTo(width() * .8, height() * .24);
        ridge.lineTo(width(), height() * .56);
        ridge.lineTo(width(), height());
        ridge.closeSubpath();
        p.fillPath(ridge, QColor("#29454c"));
        p.translate(width() * .12, height() * .08);
        p.scale(1.04, 1.04);
        p.fillPath(ridge, QColor(13, 29, 37, 190));
    }
    return result;
}
void ArtworkCanvas::rebuildLayers()
{
    m_cover = cover(m_image);
    m_frost = frostedImage(m_cover);
    if (m_blend < 1) {
        m_previousCover = cover(m_previous);
        m_previousFrost = frostedImage(m_previousCover);
    }
}
void ArtworkCanvas::transition(QImage image)
{
    m_fade.stop();
    m_previous = m_image;
    m_image = std::move(image);
    m_blend = m_reducedMotion ? 1 : 0;
    rebuildLayers();
    if (!m_reducedMotion) {
        m_fade.setStartValue(0.0);
        m_fade.setEndValue(1.0);
        m_fade.start();
    }
    refreshSurfaces();
}
void ArtworkCanvas::refreshSurfaces()
{
    update();
    // The cached frosted layers must follow the same crossfade as the world image.
    for (auto* child : findChildren<QWidget*>()) {
        if (dynamic_cast<GlassSurface*>(child))
            child->update();
    }
}
void ArtworkCanvas::resizeEvent(QResizeEvent* event)
{
    rebuildLayers();
    QWidget::resizeEvent(event);
}
void ArtworkCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (m_cover.isNull())
        rebuildLayers();
    if (m_blend < 1)
        p.drawImage(rect(), m_previousCover);
    p.setOpacity(m_blend);
    p.drawImage(rect(), m_cover);
    p.setOpacity(1);
    QLinearGradient shade(0, 0, 0, height());
    shade.setColorAt(0, QColor(3, 8, 14, 185));
    shade.setColorAt(.45, QColor(3, 8, 14, 30));
    shade.setColorAt(1, QColor(3, 8, 14, 220));
    p.fillRect(rect(), shade);
}
void ArtworkCanvas::paintGlass(QPainter& painter, QWidget* surface, int radius)
{
    const auto offset = surface->mapTo(this, QPoint());
    const QRectF target = QRectF(surface->rect()).adjusted(.5, .5, -.5, -.5);
    QPainterPath clip;
    clip.addRoundedRect(target, radius, radius);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.save();
    painter.setClipPath(clip);
    const QRect source(offset, surface->size());
    if (m_blend < 1)
        painter.drawImage(surface->rect(), m_previousFrost, source);
    painter.setOpacity(m_blend);
    painter.drawImage(surface->rect(), m_frost, source);
    painter.setOpacity(1);
    painter.fillPath(clip, QColor(10, 18, 26, surface->objectName() == "awakeNavigation" ? 213 : 190));
    painter.restore();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QColor(234, 244, 255, 48));
    painter.drawPath(clip);
}
GlassSurface::GlassSurface(ArtworkCanvas* canvas, QWidget* parent) : QWidget(parent), m_canvas(canvas) {}
void GlassSurface::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    m_canvas->paintGlass(painter, this, 22);
}
}  // namespace Awake
