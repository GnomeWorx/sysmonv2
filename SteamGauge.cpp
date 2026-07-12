#include "SteamGauge.h"
#include <QtMath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPainterPath>

// ── Constants ──────────────────────────────────────────────────
static constexpr double DEG_START = 135.0;   // start angle (bottom-left)
static constexpr double DEG_SPAN  = 270.0;   // arc span (bottom-right)

static constexpr double RING_WIDTH_RATIO = 0.10;   // 10% of gauge width
static constexpr double TICK_LONG_RATIO  = 0.035;
static constexpr double TICK_SHORT_RATIO = 0.020;
static constexpr double NEEDLE_LEN_RATIO = 0.70;
static constexpr double NEEDLE_WIDTH     = 2.5;   // px at base

static const QColor COLOR_BRASS_LIGHT(212, 168, 67);   // #d4a843
static const QColor COLOR_BRASS_MID(184, 134, 11);     // #b8860b
static const QColor COLOR_BRASS_DARK(139, 90, 0);      // #8b5a00
static const QColor COLOR_CRIMSON(139, 0, 0);           // #8b0000
static const QColor COLOR_NEEDLE(220, 30, 30);          // #dc1e1e
static const QColor COLOR_PARCHMENT(245, 230, 200);     // #f5e6c8
static const QColor COLOR_RED_ZONE(180, 40, 40, 60);    // translucent red
static const QColor COLOR_WOOD(26, 20, 16);             // #1a1410
static const QColor COLOR_RIVET(192, 160, 96);          // #c0a060
static const QColor COLOR_GOLD_TEXT(212, 168, 67);      // #d4a843
static const QColor COLOR_ENGRAVED(42, 31, 20);         // #2a1f14

// ── Constructor ────────────────────────────────────────────────
SteamGauge::SteamGauge(const QString &title,
                       const QString &unit,
                       double minValue,
                       double maxValue,
                       double redThreshold,
                       QWidget *parent)
    : QWidget(parent)
    , m_title(title)
    , m_unit(unit)
    , m_minValue(minValue)
    , m_maxValue(maxValue)
    , m_redThreshold(redThreshold)
{
    setMinimumSize(120, 140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);

    // ── Smooth needle animation ──
    m_anim = new QPropertyAnimation(this, "animatedValue", this);
    m_anim->setDuration(400);          // 400ms smooth sweep
    m_anim->setEasingCurve(QEasingCurve::OutCubic);

    // ── Shake timer: fires every 30ms while shaking ──
    m_shakeTimer.setInterval(30);
    connect(&m_shakeTimer, &QTimer::timeout, this, [this]() {
        if (m_shakeDuration > 0) {
            m_shakeDuration -= 30;
            m_shakeFrame++;
            update();
        } else {
            m_shakeTimer.stop();
            m_shakeFrame = 0;
            update();   // final repaint at rest position
        }
    });
}

// ── Public API ─────────────────────────────────────────────────
void SteamGauge::setAnimDuration(int ms) {
    m_anim->setDuration(ms);
}

void SteamGauge::setValue(double val) {
    m_value = qBound(m_minValue, val, m_maxValue);

    if (m_anim->duration() == 0) {
        // Instant mode — snap needle directly, no animation
        m_animatedValue = m_value;
        // Skip red-zone shake for instant-mode gauges
    } else {
        // Animate needle
        m_anim->stop();
        m_anim->setStartValue(m_animatedValue);
        m_anim->setEndValue(m_value);
        m_anim->start();
    }

    // Red-zone shake is intentionally removed — it was annoying.
    m_wasInRed = false;
    m_shakeDuration = 0;
    m_shakeTimer.stop();
    m_shakeFrame = 0;
}

void SteamGauge::setSecondaryValue(double val) {
    m_secondaryValue = val;
    update();
}

void SteamGauge::setTertiaryValue(double val) {
    m_tertiaryValue = val;
    update();
}

void SteamGauge::setSubtitle(const QString &text) {
    m_subtitle = text;
    update();
}

void SteamGauge::setAnimatedValue(double v) {
    m_animatedValue = v;
    update();
}

// ── Resize ─────────────────────────────────────────────────────
void SteamGauge::resizeEvent(QResizeEvent *event) {
    m_cacheDirty = true;
    QWidget::resizeEvent(event);
}

// ── Paint ──────────────────────────────────────────────────────
void SteamGauge::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // ── Gauge rect (square, centered, padding for subtitle at bottom) ──
    int w = width();
    int h = height();
    int gaugeSize = qMin(w, h - 20);          // leave 20px for subtitle
    int gx = (w - gaugeSize) / 2;
    int gy = (h - 20 - gaugeSize) / 2;
    QRectF gaugeRect(gx, gy, gaugeSize, gaugeSize);

    // ── Background — transparent (panel paints behind us) ──
    // The full-width wood panel background is painted by SystemMonitorV2.

    // ── Draw components (background layers first) ──
    if (m_unit == "HMS") {
        drawClockBezel(p, gaugeRect);
    } else {
        drawBrassRing(p, gaugeRect);
    }
    drawDialFace(p, gaugeRect);
    drawRedZone(p, gaugeRect);
    drawTickMarks(p, gaugeRect);

    // Text labels drawn BEFORE needle so they sit behind it
    drawTitle(p, gaugeRect);
    drawSubtitle(p, gaugeRect);

    // Needle on top — covers text if it passes over it (realism)
    drawNeedle(p, gaugeRect, m_animatedValue, m_needleColor.isValid() ? m_needleColor : COLOR_NEEDLE);

    // Secondary needle (e.g. temperature on CPU gauge)
    if (m_secondaryValue >= m_minValue) {
        double ratio = (m_secondaryValue - m_minValue) / (m_maxValue - m_minValue);
        double secVal = m_minValue + ratio * (m_maxValue - m_minValue);
        drawNeedle(p, gaugeRect, secVal, QColor(200, 100, 20), 0.65);   // amber secondary needle
    }

    // Tertiary needle (e.g. hour hand on clock)
    if (m_tertiaryValue >= m_minValue) {
        double ratio = (m_tertiaryValue - m_minValue) / (m_maxValue - m_minValue);
        double terVal = m_minValue + ratio * (m_maxValue - m_minValue);
        drawNeedle(p, gaugeRect, terVal, QColor(180, 140, 60), 0.50);   // gold tertiary (short & stubby)
    }

    drawGlassOverlay(p, gaugeRect);
    drawRivets(p, gaugeRect);
}

// ── Drawing helpers ────────────────────────────────────────────

void SteamGauge::setArc(double degStart, double degSpan) {
    m_degStart = degStart;
    m_degSpan = degSpan;
    update();
}

void SteamGauge::setNeedleBaseWidth(double ratio) {
    m_needleBaseWidth = ratio;
}

void SteamGauge::setBezelColor(const QColor &c) {
    m_bezelColor = c;
    update();
}

void SteamGauge::setNeedleColor(const QColor &c) {
    m_needleColor = c;
    update();
}

void SteamGauge::drawBrassRing(QPainter &p, const QRectF &rect) {
    double ringW = rect.width() * RING_WIDTH_RATIO;

    // ── Outer drop shadow (behind the gauge, on the wood panel) ──
    QRectF shadowRect = rect.adjusted(3, 4, 3, 4);
    QRadialGradient sg(shadowRect.center(), shadowRect.width() / 2);
    sg.setColorAt(0.6, QColor(0, 0, 0, 60));
    sg.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.setBrush(sg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(shadowRect);

    // Light source: 1m above, 1m in front of CEM → upward offset
    double cx = rect.center().x();
    double cy = rect.center().y();
    double outerR = rect.width() / 2;
    double lx = cx;
    double ly = cy - outerR * 0.15;  // light focal point: 15% up from centre

    // The brass ring itself: directional gradient from light source
    QRadialGradient rg(lx, ly, outerR);
    if (m_bezelColor.isValid()) {
        // Custom bezel colour (e.g. NVIDIA green)
        int r = m_bezelColor.red(), g = m_bezelColor.green(), b = m_bezelColor.blue();
        rg.setColorAt(0.0, m_bezelColor.lighter(130));
        rg.setColorAt(0.5, m_bezelColor);
        rg.setColorAt(0.85, m_bezelColor.darker(150));
        rg.setColorAt(1.0, m_bezelColor.darker(250));
    } else {
    rg.setColorAt(0.0, COLOR_BRASS_LIGHT);         // #d4a843
    rg.setColorAt(0.5, COLOR_BRASS_MID);            // #b8860b
    rg.setColorAt(0.85, COLOR_BRASS_DARK);          // #8b5a00
    rg.setColorAt(1.0, QColor(50, 30, 0));          // dark edge
    }
    p.setBrush(rg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(rect);

    // Inner bevel — chamfer ring between brass and dial face
    QRectF innerRect2 = rect.adjusted(ringW, ringW, -ringW, -ringW);
    QRadialGradient irg(lx, ly, innerRect2.width() / 2);
    irg.setColorAt(0.0, COLOR_PARCHMENT);
    irg.setColorAt(0.85, QColor(210, 190, 150));
    irg.setColorAt(0.95, QColor(160, 120, 60));
    irg.setColorAt(1.0, QColor(80, 50, 15));
    p.setBrush(irg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(innerRect2);
}

void SteamGauge::drawClockBezel(QPainter &p, const QRectF &rect) {
    // Silver bezel — brushed metal look, light source top-left
    double cx = rect.center().x();
    double cy = rect.center().y();
    double outerR = rect.width() / 2;

    // Outer drop shadow (same as brass ring)
    QRectF shadowRect = rect.adjusted(3, 4, 3, 4);
    QRadialGradient sg(shadowRect.center(), shadowRect.width() / 2);
    sg.setColorAt(0.6, QColor(0, 0, 0, 60));
    sg.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.setBrush(sg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(shadowRect);

    // Silver ring with centered gradient — no directional banding
    QRadialGradient rg(cx, cy, outerR);
    rg.setColorAt(0.0, QColor(210, 210, 215));
    rg.setColorAt(0.5, QColor(190, 190, 195));
    rg.setColorAt(0.85, QColor(155, 155, 160));
    rg.setColorAt(1.0, QColor(90, 90, 95));
    p.setBrush(rg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(rect);

    // Inner bevel — chamfer to the enamel face
    double ringW = rect.width() * RING_WIDTH_RATIO;
    QRectF innerRect = rect.adjusted(ringW, ringW, -ringW, -ringW);
    QRadialGradient irg(cx, cy, innerRect.width() / 2);
    irg.setColorAt(0.0, QColor(220, 215, 200));
    irg.setColorAt(0.85, QColor(190, 185, 170));
    irg.setColorAt(0.95, QColor(140, 130, 115));
    irg.setColorAt(1.0, QColor(80, 75, 65));
    p.setBrush(irg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(innerRect);
}

void SteamGauge::drawDialFace(QPainter &p, const QRectF &rect) {
    double ringW = rect.width() * RING_WIDTH_RATIO;
    QRectF faceRect = rect.adjusted(ringW + 3, ringW + 3, -(ringW + 3), -(ringW + 3));
    double cx = faceRect.center().x();
    double cy = faceRect.center().y();

    if (m_unit == "HMS") {
        // Clock face — darker enamel dial, light from above
        QRadialGradient fg(cx, cy - faceRect.height() * 0.15,
                            faceRect.width() / 2);
        fg.setColorAt(0.0, QColor(220, 215, 200));
        fg.setColorAt(0.4, QColor(200, 195, 180));
        fg.setColorAt(0.75, QColor(185, 178, 160));
        fg.setColorAt(1.0, QColor(160, 150, 130));
        p.setBrush(fg);
        p.setPen(QPen(QColor(120, 110, 90), 1));
        p.drawEllipse(faceRect);

        // Subtle ring around dial face
        p.setPen(QPen(QColor(100, 90, 70, 60), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(faceRect.adjusted(2,2,-2,-2));
        return;
    }

    // Parchment dial face — light from above
    QRadialGradient fg(cx, cy - faceRect.height() * 0.15, faceRect.width() / 2);
    fg.setColorAt(0.0, QColor(255, 252, 242));
    fg.setColorAt(0.5, QColor(252, 242, 222));
    fg.setColorAt(0.85, COLOR_PARCHMENT);
    fg.setColorAt(1.0, QColor(200, 180, 140));
    p.setBrush(fg);
    p.setPen(QPen(QColor(160, 120, 60), 1));
    p.drawEllipse(faceRect);

    // ── Deep concentric rings for machined-metal look ──
    for (int i = 1; i <= 3; ++i) {
        double inset = faceRect.width() * (0.05 + i * 0.04);
        QRectF ring = faceRect.adjusted(inset, inset, -inset, -inset);
        QColor ringColor(180, 150, 100, 30 + i * 15);
        p.setPen(QPen(ringColor, 0.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ring);
    }

    // ── Subtle shadow at the bottom rim (dial receding into case) ──
    QRectF innerShadowRect = faceRect.adjusted(2, 2, -2, -2);
    QRadialGradient isg(cx, cy + faceRect.height() * 0.3, faceRect.width() / 2);
    isg.setColorAt(0.0, QColor(0, 0, 0, 0));
    isg.setColorAt(0.7, QColor(0, 0, 0, 0));
    isg.setColorAt(0.9, QColor(0, 0, 0, 15));
    isg.setColorAt(1.0, QColor(0, 0, 0, 40));
    p.setBrush(isg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(innerShadowRect);
}

void SteamGauge::drawRedZone(QPainter &p, const QRectF &rect) {
    double ringW = rect.width() * RING_WIDTH_RATIO;
    QRectF faceRect = rect.adjusted(ringW + 4, ringW + 4, -(ringW + 4), -(ringW + 4));

    // Red zone: top 1/5 of the arc
    double redStart = m_degStart + m_degSpan * (m_redThreshold - m_minValue) / (m_maxValue - m_minValue);
    double redSpan = m_degStart + m_degSpan - redStart;

    p.setPen(QPen(QColor(180, 30, 30, 80), faceRect.width() * 0.06));
    p.setBrush(Qt::NoBrush);

    // Draw red danger arc
    QPainterPath redPath;
    double rx = faceRect.center().x();
    double ry = faceRect.center().y();
    double r = faceRect.width() / 2 - faceRect.width() * 0.03;
    redPath.arcMoveTo(faceRect.adjusted(r*0.06, r*0.06, -r*0.06, -r*0.06), redStart);
    for (double a = redStart; a <= redStart + redSpan; a += 1.0) {
        double rad = qDegreesToRadians(a);
        redPath.lineTo(rx + r * qCos(rad), ry + r * qSin(rad));
    }
    p.setPen(QPen(QColor(180, 30, 30, 60), faceRect.width() * 0.04));
    p.drawPath(redPath);

    // More visible red band on the outermost track
    p.setPen(QPen(QColor(200, 50, 50, 60), faceRect.width() * 0.02));
    p.setBrush(Qt::NoBrush);
    p.drawArc(faceRect.adjusted(2,2,-2,-2), qRound(-redStart * 16), qRound(-redSpan * 16));
}

void SteamGauge::drawTickMarks(QPainter &p, const QRectF &rect) {
    double ringW = rect.width() * RING_WIDTH_RATIO;
    double outerR = rect.width() / 2 - ringW - 4;
    double innerR = outerR - rect.width() * TICK_LONG_RATIO;
    double shortR = outerR - rect.width() * TICK_SHORT_RATIO;

    double cx = rect.center().x();
    double cy = rect.center().y();

    p.setPen(QPen(COLOR_ENGRAVED, 1.2));

    // ── Clock face (HMS unit) ──
    if (m_unit == "HMS") {
        // Full 360° clock with 12 at top. m_degStart=90, m_degSpan=360
        // Hours 1-12 spaced evenly: each = m_degStart + m_degSpan * (h/12)
        for (int h = 1; h <= 12; ++h) {
            double frac = (double)h / 12.0;
            double angle = m_degStart + frac * m_degSpan;
            double rad = qDegreesToRadians(angle);

            double hInner = outerR - rect.width() * 0.04;
            double x1 = cx + outerR * qCos(rad);
            double y1 = cy + outerR * qSin(rad);
            double x2 = cx + hInner * qCos(rad);
            double y2 = cy + hInner * qSin(rad);
            p.setPen(QPen(COLOR_ENGRAVED, 1.8));
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2));

            double labelR = hInner - 10;
            double lx = cx + labelR * qCos(rad);
            double ly = cy + labelR * qSin(rad);

            p.setPen(COLOR_ENGRAVED);
            QFont f = font();
            f.setPointSize(8);
            f.setBold(true);
            p.setFont(f);

            QRectF lr(lx - 10, ly - 7, 20, 14);
            lr.moveCenter(QPointF(lx, ly));
            p.drawText(lr, Qt::AlignCenter, QString::number(h));
            p.setPen(QPen(COLOR_ENGRAVED, 1.8));
        }
        return;
    }

    // ── Standard industrial gauge ticks ──
    int numMajor = 10;
    int numMinor = 50;  // 5 per major

    for (int i = 0; i <= numMinor; ++i) {
        double frac = (double)i / numMinor;
        double angle = m_degStart + frac * m_degSpan;
        double rad = qDegreesToRadians(angle);

        bool isMajor = (i % (numMinor / numMajor) == 0);
        double inner = isMajor ? innerR : shortR;

        double x1 = cx + outerR * qCos(rad);
        double y1 = cy + outerR * qSin(rad);
        double x2 = cx + inner  * qCos(rad);
        double y2 = cy + inner  * qSin(rad);
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));

        // Major tick: draw value label
        if (isMajor && i < numMinor) {
            double labelR = inner - 8;
            double val = m_minValue + frac * (m_maxValue - m_minValue);
            double lx = cx + labelR * qCos(rad);
            double ly = cy + labelR * qSin(rad);

            p.setPen(COLOR_ENGRAVED);
            QFont f = font();
            f.setPointSize(7);
            f.setBold(false);
            p.setFont(f);

            QString label;
            if (m_unit == "%" || m_unit == "°C") {
                label = QString::number((int)val);
            } else {
                label = QString::number(val, 'f', 0);
            }

            QRectF labelRect(lx - 10, ly - 6, 20, 12);
            labelRect.moveCenter(QPointF(lx, ly));
            p.drawText(labelRect, Qt::AlignCenter, label);
            p.setPen(QPen(COLOR_ENGRAVED, 1.2));
        }
    }
}

void SteamGauge::drawNeedle(QPainter &p, const QRectF &rect, double val, const QColor &color, double lenRatio) {
    double frac = (val - m_minValue) / (m_maxValue - m_minValue);
    double angle = m_degStart + frac * m_degSpan;
    double rad = qDegreesToRadians(angle);

    double cx = rect.center().x();
    double cy = rect.center().y();
    double needleLen = rect.width() / 2 * lenRatio;
    double ringW = rect.width() * RING_WIDTH_RATIO;
    double innerR = rect.width() / 2 - ringW - 4;

    // Needle tip
    double tipX = cx + needleLen * qCos(rad);
    double tipY = cy + needleLen * qSin(rad);

    // Needle base (wider)
    double baseLen = rect.width() * m_needleBaseWidth;
    double baseAngle1 = angle + 85.0;
    double baseAngle2 = angle - 85.0;
    double b1x = cx + baseLen * qCos(qDegreesToRadians(baseAngle1));
    double b1y = cy + baseLen * qSin(qDegreesToRadians(baseAngle1));
    double b2x = cx + baseLen * qCos(qDegreesToRadians(baseAngle2));
    double b2y = cy + baseLen * qSin(qDegreesToRadians(baseAngle2));

    // Needle body (tapered triangle)
    QPainterPath needlePath;
    needlePath.moveTo(tipX, tipY);
    needlePath.lineTo(b1x, b1y);
    needlePath.lineTo(b2x, b2y);
    needlePath.closeSubpath();

    // ── Drop shadow: light from above → shadow casts downward ──
    double shadowDist = 3.0;
    double shadX = 0;
    double shadY = shadowDist;
    p.save();
    p.translate(shadX, shadY);
    QPainterPath shadowPath = needlePath;  // copy
    QLinearGradient sg(tipX + shadX, tipY + shadY, cx + shadX, cy + shadY);
    sg.setColorAt(0.0, QColor(0, 0, 0, 60));
    sg.setColorAt(0.6, QColor(0, 0, 0, 40));
    sg.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.setBrush(sg);
    p.setPen(Qt::NoPen);
    p.drawPath(shadowPath);
    p.restore();

    // Additional soft blur pass: wider, darker shadow right at the base
    p.save();
    p.translate(shadX * 1.5, shadY * 1.5);
    p.setBrush(QColor(0, 0, 0, 25));
    p.setPen(Qt::NoPen);
    p.drawPath(needlePath);
    p.restore();

    // Gradient fill for 3D effect
    QLinearGradient ng(tipX, tipY, cx, cy);
    ng.setColorAt(0.0, color.lighter(150));
    ng.setColorAt(0.5, color);
    ng.setColorAt(1.0, color.darker(180));
    p.setPen(Qt::NoPen);
    p.setBrush(ng);
    p.drawPath(needlePath);

    // Centre hub
    double hubR = rect.width() * 0.045;
    QRadialGradient hg(cx, cy, hubR);
    hg.setColorAt(0.0, COLOR_BRASS_LIGHT);
    hg.setColorAt(0.6, COLOR_BRASS_MID);
    hg.setColorAt(1.0, COLOR_BRASS_DARK);
    p.setBrush(hg);
    p.drawEllipse(QPointF(cx, cy), hubR, hubR);

    // Centre screw
    p.setBrush(QColor(60, 40, 10));
    p.drawEllipse(QPointF(cx, cy), hubR * 0.4, hubR * 0.4);
}

void SteamGauge::drawGlassOverlay(QPainter &p, const QRectF &rect) {
    // Glass reflection: bright crescent at the top (light source above)
    QRadialGradient gg(rect.center().x(),
                       rect.top() + rect.height() * 0.2,
                       rect.width() * 0.5);
    gg.setColorAt(0.0, QColor(255, 255, 255, 30));
    gg.setColorAt(0.5, QColor(255, 255, 255, 8));
    gg.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(gg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(rect);

    // Edge highlight arc (glass rim)
    QRadialGradient er(rect.center(), rect.width() / 2);
    er.setColorAt(0.95, QColor(255, 255, 255, 0));
    er.setColorAt(0.98, QColor(255, 255, 255, 20));
    er.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(er);
    p.drawEllipse(rect);
}

void SteamGauge::drawTitle(QPainter &p, const QRectF &rect) {
    // Title on the dial face, ~1/3 down from top of the inner face
    QFont f = font();
    f.setPointSize(10);
    f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    p.setFont(f);

    double ringW = rect.width() * RING_WIDTH_RATIO;
    QRectF faceRect = rect.adjusted(ringW + 4, ringW + 4, -(ringW + 4), -(ringW + 4));

    double cx = faceRect.center().x();
    double cy = faceRect.top() + faceRect.height() * 0.28;

    QRectF titleRect(cx - 60, cy - 10, 120, 20);
    p.setPen(QPen(COLOR_GOLD_TEXT));
    p.drawText(titleRect, Qt::AlignCenter, m_title);

    // Small unit text beneath title on the dial face
    if (m_unit != "HMS") {  // clock has no unit label needed
        QFont uf = font();
        uf.setPointSize(7);
        uf.setBold(false);
        p.setFont(uf);
        p.setPen(QColor(160, 130, 60));
        QRectF unitRect(cx - 60, cy + 10, 120, 14);
        p.drawText(unitRect, Qt::AlignCenter, m_unit);
    }
}

void SteamGauge::drawSubtitle(QPainter &p, const QRectF &rect) {
    if (m_subtitle.isEmpty()) return;

    QFont f = font();
    f.setPointSize(9);
    f.setBold(false);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    p.setFont(f);
    p.setPen(Qt::white);
    // Subtle shadow for readability
    p.setPen(QColor(0, 0, 0, 180));

    double ringW = rect.width() * RING_WIDTH_RATIO;
    QRectF faceRect = rect.adjusted(ringW + 4, ringW + 4, -(ringW + 4), -(ringW + 4));
    double cx = faceRect.center().x();
    double cy = faceRect.top() + faceRect.height() * 0.72;

    QRectF shadowRect(cx - 50, cy - 8, 100, 16);
    p.drawText(shadowRect, Qt::AlignCenter, m_subtitle);

    // Actual text in bright white
    QFont f2 = font();
    f2.setPointSize(9);
    f2.setBold(false);
    f2.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    // Use monospace for the value
    f2.setFamily("monospace");
    p.setFont(f2);
    p.setPen(QColor(255, 230, 160));
    QRectF subRect(cx - 50, cy - 9, 100, 16);
    p.drawText(subRect, Qt::AlignCenter, m_subtitle);
}

void SteamGauge::drawRivets(QPainter &p, const QRectF &rect) {
    // We draw rivets around the panel edge, not the dial itself
    // 4 corner rivets
    double r = 3.5;
    struct Pt { double x, y; };
    QVector<Pt> corners = {
        {rect.left() - 6, rect.top() - 6},
        {rect.right() + 6, rect.top() - 6},
        {rect.left() - 6, rect.bottom() + 6},
        {rect.right() + 6, rect.bottom() + 6},
    };

    for (auto &c : corners) {
        QRadialGradient rg(c.x, c.y, r);
        rg.setColorAt(0.0, COLOR_RIVET.lighter(130));
        rg.setColorAt(0.5, COLOR_RIVET);
        rg.setColorAt(1.0, COLOR_RIVET.darker(150));
        p.setBrush(rg);
        p.setPen(QPen(QColor(60, 40, 10), 0.5));
        p.drawEllipse(QPointF(c.x, c.y), r, r);
    }
}
