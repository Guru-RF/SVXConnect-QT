/* SPDX-License-Identifier: MIT
 * SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
 */
#include "ui/trayicon.h"
#include "ui/theme.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QPixmap>
#include <QGuiApplication>

TrayIcon::TrayIcon(svx_app *app, QObject *parent)
    : QObject(parent), m_app(app)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_tray = new QSystemTrayIcon(this);
    rebuildMenu();

    m_tray->setIcon(iconFor(false, false));
    m_tray->setToolTip(tr("SVXConnect"));

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                /* Trigger is a left click. DoubleClick too, because several
                 * desktops send only one or the other. */
                if (reason == QSystemTrayIcon::Trigger
                    || reason == QSystemTrayIcon::DoubleClick)
                    emit showWindowRequested();
            });

    m_tray->show();
}

bool TrayIcon::isAvailable() const
{
    return m_tray != nullptr;
}

void TrayIcon::rebuildMenu()
{
    m_menu = new QMenu;

    QAction *show = m_menu->addAction(tr("Show SVXConnect"));
    connect(show, &QAction::triggered, this, &TrayIcon::showWindowRequested);

    m_menu->addSeparator();

    /* Toggle, not hold: a menu item cannot report a release, so offering
     * "hold to talk" here would be a lie. The global shortcut is the one that
     * does a real hold. */
    m_pttAction = m_menu->addAction(tr("Transmit"));
    m_pttAction->setCheckable(true);
    connect(m_pttAction, &QAction::triggered, this, [this]() {
        if (m_app) app_ptt(m_app, CTL_TOGGLE);
    });

    m_connectAction = m_menu->addAction(tr("Disconnect"));
    connect(m_connectAction, &QAction::triggered, this, [this]() {
        if (m_app) app_toggle_connect(m_app);
    });

    m_menu->addSeparator();

    QAction *quit = m_menu->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, this, &TrayIcon::quitRequested);

    m_tray->setContextMenu(m_menu);
}

QIcon TrayIcon::iconFor(bool connected, bool transmitting) const
{
    /* The application icon with a small state dot in the corner.
     *
     * Drawn rather than shipped as three separate files: the dot has to read
     * against both light and dark panels, and compositing it here means the
     * status colours stay in one place (Theme::) instead of being baked into
     * PNGs that drift from the rest of the interface.
     */
    QIcon base;
    for (int size : {22, 24, 32, 48, 64})
        base.addFile(QStringLiteral(":/icons/app-%1.png").arg(size), QSize(size, size));

    const int px = 64;
    QPixmap pm = base.pixmap(px, px);
    if (pm.isNull())
        return base;

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor dot = transmitting ? Theme::tx()
                     : connected    ? Theme::connected()
                                    : Theme::down();

    const qreal r = px * 0.22;
    const QPointF c(px - r - 2, px - r - 2);

    /* A contrasting ring, so the dot is visible on any panel colour. */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 160));
    p.drawEllipse(c, r + 2.0, r + 2.0);
    p.setBrush(dot);
    p.drawEllipse(c, r, r);
    p.end();

    return QIcon(pm);
}

void TrayIcon::tickModel()
{
    if (!m_tray || !m_app)
        return;

    const rc_state st = rc_get_state(app_rc(m_app));
    const bool connected = (st == RC_CONNECTED);
    const bool tx        = app_tx_active(m_app) != 0;

    if (!m_stateInit || connected != m_shownConnected || tx != m_shownTx) {
        m_stateInit     = true;
        m_shownConnected = connected;
        m_shownTx        = tx;
        m_tray->setIcon(iconFor(connected, tx));

        if (m_pttAction)
            m_pttAction->setChecked(tx);
        if (m_connectAction)
            m_connectAction->setText(st == RC_IDLE ? tr("Connect") : tr("Disconnect"));
    }

    /* The tooltip carries what the macOS menu bar shows as text next to its
     * icon — QSystemTrayIcon has no text label on most Linux desktops, so this
     * is where the callsign and talkgroup have to live. */
    const svx_config *cfg = app_config(m_app);
    const uint32_t tg = tgm_selected(app_tgm(m_app));

    QString state = QString::fromUtf8(rc_state_name(st));
    if (!state.isEmpty())
        state[0] = state[0].toUpper();

    QString tip = QStringLiteral("SVXConnect — %1\n%2")
                      .arg(QString::fromUtf8(cfg->callsign), state);
    if (connected)
        tip += tg ? tr("  ·  TG %1").arg(tg) : tr("  ·  monitoring");
    if (tx)
        tip += tr("\nTRANSMITTING");

    if (tip != m_tip) {
        m_tip = tip;
        m_tray->setToolTip(tip);
    }
}


void TrayIcon::notifyStillRunning()
{
    if (!m_tray || !QSystemTrayIcon::supportsMessages())
        return;

    m_tray->showMessage(
        tr("SVXConnect is still running"),
        tr("The window is closed but SVXConnect keeps receiving, and the "
           "push-to-talk shortcut keeps working. Quit from the tray icon."),
        QSystemTrayIcon::Information, 6000);
}
