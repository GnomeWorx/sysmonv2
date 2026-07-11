#ifndef STEAMGAUGE_H
#define STEAMGAUGE_H

#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QtMath>
#include <QStyle>
#include <QStyleOption>

/// A steampunk analog dial gauge with brass styling, glass dome,
/// crimson needle, and a game-style shake when the needle enters
/// the red zone (top 1/5 of range).
class SteamGauge : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double animatedValue READ animatedValue WRITE setAnimatedValue)

public:
    explicit SteamGauge(const QString &title,
                        const QString &unit,
                        double minValue = 0.0,
                        double maxValue = 100.0,
                        double redThreshold = 80.0,   // top 1/5 of range
                        QWidget *parent = nullptr);

    void setValue(double val);
    void setSecondaryValue(double val);        ///< second needle (e.g. temp on CPU gauge)
    void setSubtitle(const QString &text);     ///< e.g. "42%  68°C"

    double value() const { return m_value; }
    double animatedValue() const { return m_animatedValue; }

    // ── Red-zone shake ──
    bool isInRedZone() const { return m_value >= m_redThreshold; }

signals:
    void enteredRedZone();
    void exitedRedZone();

public slots:
    void setAnimatedValue(double v);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void drawBrassRing(QPainter &p, const QRectF &rect);
    void drawDialFace(QPainter &p, const QRectF &rect);
    void drawTickMarks(QPainter &p, const QRectF &rect);
    void drawNeedle(QPainter &p, const QRectF &rect, double val, const QColor &color);
    void drawGlassOverlay(QPainter &p, const QRectF &rect);
    void drawTitle(QPainter &p, const QRectF &rect);
    void drawSubtitle(QPainter &p, const QRectF &rect);
    void drawRivets(QPainter &p, const QRectF &rect);
    void drawRedZone(QPainter &p, const QRectF &rect);

    // ── Shake mechanics ──
    void updateShake();
    QTimer m_shakeTimer;
    int m_shakeFrame = 0;
    int m_shakeDuration = 0;
    bool m_wasInRed = false;

    // ── Cached pixmaps ──
    QPixmap m_cachedFace;
    QPixmap m_cachedRing;
    bool m_cacheDirty = true;

    // ── Data ──
    QString m_title;
    QString m_unit;
    QString m_subtitle;
    double m_minValue;
    double m_maxValue;
    double m_redThreshold;
    double m_value = 0.0;
    double m_secondaryValue = -1.0;   // -1 means disabled
    double m_animatedValue = 0.0;

    // ── Animation ──
    QPropertyAnimation *m_anim = nullptr;
};

#endif // STEAMGAUGE_H
