/* SPDX-License-Identifier: MIT
 * SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
 *
 * The tray icon, and the reason the application can outlive its window.
 *
 * WHY THIS IS NOT OPTIONAL POLISH
 * -------------------------------
 * The global push-to-talk shortcut is registered by the RUNNING PROCESS: the
 * portal session is created at startup and closes when the process exits, at
 * which point kglobalaccel marks the component inactive and the key does
 * nothing. So an application that quits when its window is closed has a global
 * hotkey that only works while you are looking at the window — which is the
 * exact opposite of the point.
 *
 * Keeping the process alive after the window closes is therefore part of the
 * hotkey feature, not a separate nicety. And once the window can be closed
 * without quitting, something has to be able to bring it back and to quit
 * properly — hence the tray icon. The two are one change.
 *
 * A radio that keeps receiving with its window closed is also just correct
 * behaviour for this kind of application.
 */
#ifndef SVXCONNECT_QT_TRAYICON_H
#define SVXCONNECT_QT_TRAYICON_H

#include <QObject>
#include <QIcon>

#include "core/svxcore.h"

class QSystemTrayIcon;
class QMenu;
class QAction;

class TrayIcon : public QObject {
    Q_OBJECT

public:
    explicit TrayIcon(svx_app *app, QObject *parent = nullptr);

    bool isAvailable() const;

    /* Called from the 100 ms model tick. Cheap: it only touches the icon and
     * tooltip when the state it renders has actually changed. */
    void tickModel();

    /* Shown once, the first time the window is closed, so "it did not quit"
     * is an explanation rather than a surprise. */
    void notifyStillRunning();

signals:
    void showWindowRequested();
    void quitRequested();

private:
    void rebuildMenu();
    QIcon iconFor(bool connected, bool transmitting) const;

    svx_app         *m_app  = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu           *m_menu = nullptr;
    QAction         *m_connectAction = nullptr;
    QAction         *m_pttAction     = nullptr;

    /* What the icon currently depicts, so a 10 Hz tick does not reassign the
     * same QIcon forever — on some StatusNotifierItem hosts that causes a
     * visible flicker. */
    bool m_shownConnected = false;
    bool m_shownTx        = false;
    bool m_stateInit      = false;
    QString m_tip;
};

#endif
