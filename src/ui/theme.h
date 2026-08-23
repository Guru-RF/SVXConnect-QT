/* SPDX-License-Identifier: MIT
 * SVXConnect-Qt — Copyright (c) 2026 Diëlectricum BV
 *
 * The only hard-coded colours in the application.
 *
 * Everything else uses palette roles, so light and dark both work without a
 * second stylesheet. These six are deliberate exceptions, each for a reason:
 *
 *   Tx        The transmit cue must read as "hot" even when the window is
 *             unfocused. Several desktop themes desaturate palette-derived
 *             accents on inactive windows, which is exactly the moment a
 *             transmitting operator most needs to see it. The macOS app made
 *             the same call for the same reason.
 *   Connected
 *   Busy      State semantics. Green/amber/grey mean the same thing in every
 *   Down      theme; a palette Highlight does not.
 *   Star      Priority stars. Amber against both a light and a dark surface.
 *   Badge     Priority badges, drawn on a 10% wash of themselves.
 *
 * The level-meter gradient is defined here too, because it is a fixed
 * green→yellow→red scale whose meaning would be destroyed by theming.
 */
#ifndef SVXCONNECT_QT_THEME_H
#define SVXCONNECT_QT_THEME_H

#include <QColor>

namespace Theme {

inline QColor tx()        { return QColor(0xd1, 0x3b, 0x3b); }  /* transmitting  */
inline QColor connected() { return QColor(0x2e, 0xa0, 0x43); }  /* linked        */
inline QColor busy()      { return QColor(0xd2, 0x99, 0x22); }  /* (re)connecting */
inline QColor down()      { return QColor(0x6e, 0x76, 0x81); }  /* idle          */
inline QColor star()      { return QColor(0xe3, 0xb3, 0x41); }  /* priority star */
inline QColor badge()     { return QColor(0xe0, 0x8c, 0x33); }  /* priority badge */

/* Meter scale. Green for most of the travel, yellow approaching clip, red at
 * the top — the stops are positions along the FULL track width, not along the
 * filled portion, so a given colour always means the same level. */
inline QColor meterLow()  { return QColor(0x35, 0xb3, 0x5a); }
inline QColor meterMid()  { return QColor(0xd8, 0xc4, 0x3a); }
inline QColor meterHigh() { return QColor(0xd1, 0x3b, 0x3b); }

/* A colour at 10% over the current surface, for badge and hover washes. */
inline QColor wash(const QColor &c, int alpha = 26)
{
    QColor out = c;
    out.setAlpha(alpha);
    return out;
}

} // namespace Theme

#endif
