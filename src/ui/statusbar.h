/* SPDX-License-Identifier: MIT
 * SVXConnect-Qt — Copyright (c) 2026 Diëlectricum BV
 *
 * The connection status row.
 *
 * Shows four states, not the macOS app's thirteen. Those thirteen existed to
 * narrate an inline enrolment flow; the core exposes rc_state — idle,
 * connecting, connected, reconnecting — and rc_last_error() carries the detail
 * that actually helps when something is wrong. Reproducing the other nine
 * would mean new callback surface in the core for cosmetic gain.
 *
 * The packet counters are worth more than they look. rc_get_stats() gives
 * rx_lost, rx_replayed and rx_auth_fail, none of which the macOS app has,
 * because the C core does authenticated-counter replay protection the Swift
 * implementation never did. A non-zero replay or auth-fail count means
 * somebody is replaying your UDP audio, so it is surfaced rather than buried.
 */
#ifndef SVXCONNECT_QT_STATUSBAR_H
#define SVXCONNECT_QT_STATUSBAR_H

#include <QWidget>

#include "core/svxcore.h"

class QLabel;
class QPushButton;

class ConnectionBar : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionBar(svx_app *app, QWidget *parent = nullptr);

    void tickModel(quint64 nowMs);

private:
    void buildUi();

    svx_app *m_app = nullptr;

    QLabel      *m_dot      = nullptr;
    QLabel      *m_state    = nullptr;
    QLabel      *m_detail   = nullptr;
    QLabel      *m_identity = nullptr;
    QLabel      *m_rx       = nullptr;
    QLabel      *m_tx       = nullptr;
    QLabel      *m_nodes    = nullptr;
    QLabel      *m_grid     = nullptr;
    QPushButton *m_connect  = nullptr;

    rc_state m_lastState = RC_IDLE;
    bool     m_stateInit  = false;
};

#endif
