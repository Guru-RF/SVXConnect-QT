/* SPDX-License-Identifier: MIT
 * SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
 */
#include "ui/levelmeter.h"
#include "ui/theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <cmath>

namespace {
constexpr int   kHeight = 8;
constexpr qreal kRadius = 2.0;

/* Below this, a repaint would not change a pixel. At 30 Hz that saves a lot of
 * composited frames on an idle desktop. */
constexpr float kEpsilon = 0.004f;
} // namespace

LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFocusPolicy(Qt::NoFocus);
}

QSize LevelMeter::sizeHint() const        { return QSize(120, kHeight); }
QSize LevelMeter::minimumSizeHint() const { return QSize(40,  kHeight); }

void LevelMeter::setLevel(float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;

    if (std::fabs(level - m_level) < kEpsilon)
        return;

    m_level = level;
    update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF track(0, (height() - kHeight) / 2.0, width(), kHeight);

    QPainterPath clip;
    clip.addRoundedRect(track, kRadius, kRadius);

    /* The unfilled track: a wash of the text colour, so it reads correctly in
     * both light and dark without a second palette. */
    QColor bg = palette().color(QPalette::WindowText);
    bg.setAlpha(38);
    p.fillPath(clip, bg);

    if (m_level <= 0.0f)
        return;

    /* The gradient spans the FULL track, not the filled portion. That is what
     * makes a given colour mean a given level: if it spanned the fill, a quiet
     * signal would still paint red at its own right-hand edge. */
    QLinearGradient g(track.left(), 0, track.right(), 0);
    g.setColorAt(0.00, Theme::meterLow());
    g.setColorAt(0.62, Theme::meterLow());
    g.setColorAt(0.82, Theme::meterMid());
    g.setColorAt(1.00, Theme::meterHigh());

    QRectF fill = track;
    fill.setWidth(track.width() * static_cast<qreal>(m_level));

    p.save();
    p.setClipPath(clip);
    p.fillRect(fill, g);
    p.restore();
}
