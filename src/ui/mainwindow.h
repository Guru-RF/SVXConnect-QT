/* SPDX-License-Identifier: MIT
 * SVXConnect-Qt — Copyright (c) 2026 Diëlectricum BV
 *
 * The window shell: menus, layout, the two timers, and the PTT button.
 *
 * THE REPAINT MODEL, WHICH IS NOT OBVIOUS
 * ---------------------------------------
 * app_set_observer() sounds like the change notification you want. It is
 * nearly inert: app.c fires it only from banner_set, ctl_volume,
 * app_set_volume, app_toggle_output_mute, app_set_input_device,
 * app_set_output_device and app_dismiss_banner. It does NOT fire on connection
 * state change, talker start/stop, or node join/leave, and the talkgroup
 * manager's own `changed` callback is a no-op stub.
 *
 * So polling on a timer is mandatory, not an optimisation — exactly as
 * SVXConnect-PIOS does it. Two timers, deliberately separate, and the split is
 * the whole design:
 *
 *     100 ms  everything textual — status, talkgroups, activity, log serial
 *      33 ms  level meters ONLY, which own their own repaint
 *
 * The rule the macOS app learned the hard way and documented at the top of
 * MainView.swift: never connect a high-rate source to a full relayout.
 * Observing its 30 Hz audio engine from the main view re-ran the entire body,
 * including a ForEach over sixty sessions, thirty times a second.
 */
#ifndef SVXCONNECT_QT_MAINWINDOW_H
#define SVXCONNECT_QT_MAINWINDOW_H

#include <QMainWindow>

#include "core/svxcore.h"

class QLabel;
class QPushButton;
class QPlainTextEdit;
class QTimer;
class QAction;
class QSplitter;
class QFileSystemWatcher;

class PttManager;
class TrayIcon;
class ConnectionBar;
class Sidebar;
class ActivityPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /* `app` may be null, for SVX_WINDOW_ONLY=1 — a layout-only mode borrowed
     * from SVXConnect-PIOS that brings the window up without starting the
     * core. Ten lines, and it makes interface work possible on a machine with
     * no certificate, no reflector and no microphone. Everything below must
     * therefore tolerate a null core. */
    explicit MainWindow(svx_app *app, QWidget *parent = nullptr);
    ~MainWindow() override;

    /* The configuration file actually in use. Resolved in main() — which may
     * have taken it from -c — so it cannot be re-derived here without getting
     * it wrong for anyone who passed that flag. */
    void setConfigPath(const QString &path);

public slots:
    /* Connected to CoreLoop::coreChanged() — a hint, not the primary path. */
    void onCoreChanged();

signals:
    /* The user asked to restart after editing the configuration.
     *
     * Deliberately a signal rather than a restart done here. Re-launching from
     * inside the window would spawn the new process while THIS one still holds
     * the run lock, and the new instance would refuse itself with "already
     * held by another SVXConnect window". main() is the only place that knows
     * the correct order: quit the loop, app_free(), release the lock, and only
     * then exec the replacement. */
    void restartRequested();

protected:
    void keyPressEvent(QKeyEvent *) override;
    /* Closing hides to the tray instead of quitting — the global shortcut is
     * registered by the running process, so quitting would silently disable
     * it. See trayicon.h. */
    void closeEvent(QCloseEvent *) override;
    /* Click-to-dismiss for the two notice bars. An event filter rather than a
     * QLabel subclass: QLabel has no clicked() and two one-line handlers do
     * not justify a widget class each. */
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void tickModel();     /* 100 ms */
    void tickMeters();    /*  33 ms */

    void onPttPressed();
    void onPttReleased();

    void onPreferences();
    void onEditConfig();
    void onConfigFileChanged(const QString &path);
    void onRestartRequested();

private:
    void buildUi();
    void buildMenus();
    void refreshBanner();
    void refreshLog();
    void refreshPttButton();
    void watchConfig();
    void applyPttBindings();

    svx_app    *m_app = nullptr;
    PttManager *m_pttManager = nullptr;
    TrayIcon   *m_tray       = nullptr;
    bool        m_reallyQuit = false;

    ConnectionBar  *m_status   = nullptr;
    Sidebar        *m_sidebar  = nullptr;
    ActivityPanel  *m_activity = nullptr;
    QLabel         *m_banner   = nullptr;
    QWidget        *m_reload   = nullptr;
    QPushButton    *m_ptt      = nullptr;
    QPlainTextEdit *m_log      = nullptr;
    QSplitter      *m_body     = nullptr;

    QAction *m_actShowSidebar = nullptr;
    QAction *m_actShowLog     = nullptr;

    QTimer *m_modelTick = nullptr;
    QTimer *m_meterTick = nullptr;

    QString             m_configPath;
    QFileSystemWatcher *m_configWatch = nullptr;

    quint64 m_lastLogSerial = 0;
    bool    m_lastTxActive  = false;
    bool    m_txInit        = false;
};

#endif
