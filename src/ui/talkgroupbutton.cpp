/* SPDX-License-Identifier: MIT
 * SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
 */
#include "ui/talkgroupbutton.h"
#include "ui/theme.h"
#include "ui/timefmt.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QMenu>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <cmath>

namespace {

constexpr int kRowHeight = 38;
constexpr int kPadX      = 8;
constexpr qreal kRadius  = 5.0;
constexpr qreal kDotR    = 4.0;   /* 8 px dot */
constexpr qreal kHaloR   = 6.0;   /* + 2 px halo */

/* A five-pointed star, drawn rather than shipped as an icon: it is 12 px, it
 * needs to be tinted, and it must not depend on the desktop icon theme having
 * a "starred" glyph that looks like anything in particular. */
QPolygonF starPolygon(QPointF c, qreal r)
{
    QPolygonF poly;
    for (int i = 0; i < 10; ++i) {
        const qreal rr = (i % 2 == 0) ? r : r * 0.42;
        const qreal a  = -M_PI / 2.0 + i * M_PI / 5.0;
        poly << QPointF(c.x() + rr * std::cos(a), c.y() + rr * std::sin(a));
    }
    return poly;
}

} // namespace

TalkgroupButton::TalkgroupButton(quint32 tg, int priority, QWidget *parent)
    : QAbstractButton(parent), m_tg(tg), m_priority(priority)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setToolTip(tr("Switch to TG %1. Right-click to mute.").arg(tg));
}

QSize TalkgroupButton::sizeHint() const { return QSize(180, kRowHeight); }

void TalkgroupButton::setMuted(bool muted)
{
    if (m_muted == muted) return;
    m_muted = muted;
    update();
}

void TalkgroupButton::setTalker(const QString &callsign)
{
    if (m_talker == callsign) return;
    m_talker = callsign;
    update();
}

void TalkgroupButton::setLastHeard(quint64 ms)
{
    if (m_lastHeard == ms) return;
    m_lastHeard = ms;
    update();
}

void TalkgroupButton::setPriority(int priority)
{
    if (m_priority == priority) return;
    m_priority = priority;
    update();
}

void TalkgroupButton::refreshAge(quint64 nowMs)
{
    const QString next = m_lastHeard ? TimeFmt::compactAge(m_lastHeard, nowMs) : QString();
    if (next == m_ageText) return;
    m_ageText = next;
    update();
}

void TalkgroupButton::enterEvent(QEnterEvent *) { m_hover = true;  update(); }
void TalkgroupButton::leaveEvent(QEvent *)      { m_hover = false; update(); }

void TalkgroupButton::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    QAction *mute = menu.addAction(m_muted ? tr("Unmute TG %1").arg(m_tg)
                                           : tr("Mute TG %1").arg(m_tg));
    if (menu.exec(e->globalPos()) == mute)
        emit muteRequested(m_tg);
}

void TalkgroupButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0, 1, 0, -1);
    const bool active = isChecked();

    /* Surface. Selected uses the palette Highlight so it matches whatever the
     * desktop considers "selected"; hover is a light wash of the text colour,
     * which works on both light and dark grounds. */
    if (active) {
        QPainterPath path;
        path.addRoundedRect(r, kRadius, kRadius);
        p.fillPath(path, palette().color(QPalette::Highlight));
    } else if (m_hover || isDown()) {
        QPainterPath path;
        path.addRoundedRect(r, kRadius, kRadius);
        p.fillPath(path, Theme::wash(palette().color(QPalette::WindowText), 20));
    }

    QColor fg = active ? palette().color(QPalette::HighlightedText)
                       : palette().color(QPalette::WindowText);
    /* Muted rows dim rather than vanish — you still need to see that the
     * talkgroup exists and that it is the reason you are not hearing it. */
    if (m_muted)
        fg.setAlphaF(active ? 0.75f : 0.55f);

    /* ---- title ---- */
    QFont title = font();
    title.setBold(active);
    title.setStrikeOut(m_muted);
    p.setFont(title);
    p.setPen(fg);

    const QFontMetrics fm(title);
    const QString label = tr("TG %1").arg(m_tg);
    const int titleY = static_cast<int>(r.top()) + fm.ascent() + 4;
    p.drawText(kPadX, titleY, label);

    int x = kPadX + fm.horizontalAdvance(label) + 6;

    /* ---- priority stars, one per '+' ---- */
    if (m_priority > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_muted ? Theme::wash(Theme::star(), 140) : Theme::star());
        for (int i = 0; i < m_priority; ++i) {
            p.drawPolygon(starPolygon(QPointF(x + 5, titleY - fm.xHeight() / 2.0 - 1), 5.0));
            x += 12;
        }
        p.setBrush(Qt::NoBrush);
    }

    /* ---- traffic dot, right-aligned, with a halo so it reads against the
     *      Highlight surface as well as the window background ---- */
    if (!m_talker.isEmpty()) {
        const QPointF c(r.right() - kPadX - kHaloR, r.top() + kHaloR + 6);
        QColor halo = palette().color(QPalette::Window);
        halo.setAlphaF(0.85f);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(c, kHaloR, kHaloR);
        p.setBrush(Theme::connected());
        p.drawEllipse(c, kDotR, kDotR);
        p.setBrush(Qt::NoBrush);
    }

    /* ---- sub-line: who is talking, else how long since anyone was ---- */
    QFont sub = font();
    sub.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.5));
    p.setFont(sub);

    QColor subFg = fg;
    subFg.setAlphaF(fg.alphaF() * 0.7f);
    p.setPen(subFg);

    const QFontMetrics sfm(sub);
    const int subY = static_cast<int>(r.bottom()) - 5;

    QString subText;
    if (!m_talker.isEmpty())
        subText = m_talker;
    else if (!m_ageText.isEmpty())
        subText = m_ageText;

    if (!subText.isEmpty()) {
        const int avail = static_cast<int>(r.width()) - kPadX * 2
                        - (m_talker.isEmpty() ? 0 : 18);
        p.drawText(kPadX, subY, sfm.elidedText(subText, Qt::ElideRight, avail));
    }

    /* ---- mute glyph: a speaker with a slash, drawn at the right of the
     *      sub-line so it never collides with the traffic dot ---- */
    if (m_muted) {
        const qreal gx = r.right() - kPadX - 10;
        const qreal gy = subY - sfm.xHeight() / 2.0;
        QPen pen(subFg);
        pen.setWidthF(1.4);
        p.setPen(pen);
        p.drawLine(QPointF(gx - 4, gy - 4), QPointF(gx + 4, gy + 4));
        p.drawLine(QPointF(gx + 4, gy - 4), QPointF(gx - 4, gy + 4));
    }
}
