/* SPDX-License-Identifier: MIT
 * SVXConnect-Qt — Copyright (c) 2026 Diëlectricum BV
 *
 * Preferences.
 *
 * Four tabs over the keys the core actually parses, written back to
 * svxconnect.conf through ConfigStore so the terminal client sees the same
 * settings. There is no separate GUI settings file for anything the core
 * understands — one identity, one configuration.
 *
 * WHAT APPLIES IMMEDIATELY, AND WHAT DOES NOT
 * -------------------------------------------
 * Three settings have proper runtime entry points and are applied at once:
 * the input device, the output device and the output volume. Everything else
 * is written to the file and takes effect on restart, and the window's
 * existing file-watcher bar is what tells the user so — the same path a
 * hand-edit takes, which means there is exactly one story to explain.
 *
 * This is not laziness. app_new() keeps the svx_config POINTER, and the
 * talkgroup manager holds indices into its lists; rewriting those fields
 * underneath a live connection is a use-after-free waiting for the next
 * talker. Restarting is cheap and provably correct.
 *
 * FOUR CONTROLS THAT DID NOT EXIST ON macOS
 * -----------------------------------------
 * mic_agc, mic_agc_target_pct, mic_gain and jitter_ms are all parsed,
 * range-checked and acted on by the core, and none of them had any interface
 * anywhere. jitter_ms in particular is the single most useful knob on a lossy
 * WAN path. idle_seconds is a fifth: the macOS app hard-coded 60 seconds as a
 * private constant.
 */
#ifndef SVXCONNECT_QT_PREFERENCESDIALOG_H
#define SVXCONNECT_QT_PREFERENCESDIALOG_H

#include <QDialog>

#include "core/svxcore.h"
#include "settings/configstore.h"
#include "ptt/pttbackend.h"

class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QDialogButtonBox;
class QPushButton;

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    PreferencesDialog(svx_app *app, const QString &configPath, QWidget *parent = nullptr);

    /* PTT bindings live in QSettings, not svxconnect.conf: the core has no
     * keys for them and the terminal client cannot use them. */
    /* What the portal actually bound, for display. The dialog cannot query it
     * itself — the session belongs to PttManager — so the window supplies it. */
    void setCurrentShortcut(const QString &human);

    static PttBinding keyboardBinding();
    static bool       holdMode();

signals:
    /* The PTT binding changed; the window re-applies it to the manager. */
    void pttBindingChanged();

    /* Emitted after a successful save that changed at least one key which
     * needs a restart. The window turns this into its "restart to apply" bar. */
    void restartNeeded();

    /* The talkgroup lists changed, so the sidebar must rebuild its buttons. */
    void talkgroupsChanged();

private slots:
    void onApply();
    void onAccept();
    void onTestTone();
    void refreshDeviceLists();

private:
    QWidget *buildConnectionTab();
    QWidget *buildAudioTab();
    QWidget *buildTalkgroupsTab();
    QWidget *buildPttTab();
    QWidget *buildGeneralTab();
    void     refreshPttStatus();
    /* Hand off to the desktop's own shortcut editor. False if nothing could
     * be launched. */
    bool     openShortcutSettings();
    void     savePtt();

    void load();
    bool commit();          /* stage every control into the store, then save */
    void applyLiveChanges(const QStringList &changed);

    svx_app     *m_app = nullptr;
    ConfigStore  m_store;

    /* Connection */
    QLineEdit *m_callsign  = nullptr;
    QLineEdit *m_email     = nullptr;
    QLineEdit *m_reflector = nullptr;
    QSpinBox  *m_port      = nullptr;
    QLineEdit *m_location  = nullptr;
    QLineEdit *m_latitude  = nullptr;
    QLineEdit *m_longitude = nullptr;
    QLabel    *m_grid      = nullptr;

    /* Audio */
    QComboBox *m_inputDev   = nullptr;
    QComboBox *m_outputDev  = nullptr;
    QLabel    *m_devWarning = nullptr;
    QSpinBox  *m_volume     = nullptr;
    QCheckBox *m_micAgc     = nullptr;
    QSpinBox  *m_micAgcTgt  = nullptr;
    QSpinBox  *m_micGain    = nullptr;
    QSpinBox  *m_jitter     = nullptr;
    QSpinBox  *m_tailTrim   = nullptr;
    QCheckBox *m_rogerBeep  = nullptr;
    QSpinBox  *m_rogerMin   = nullptr;

    /* Talkgroups */
    QLineEdit *m_switchable = nullptr;
    QLineEdit *m_monitored  = nullptr;
    QSpinBox  *m_defaultTg  = nullptr;
    QCheckBox *m_lockOnStart= nullptr;
    QSpinBox  *m_linger     = nullptr;
    QSpinBox  *m_idle       = nullptr;
    QComboBox *m_tgOrder    = nullptr;
    QLabel    *m_tgPreview  = nullptr;

    /* PTT */
    QComboBox *m_pttMode     = nullptr;
    QLabel    *m_pttShortcut = nullptr;
    QLabel    *m_pttPortalStatus = nullptr;
    QLabel    *m_pttNote    = nullptr;

    /* General */
    QSpinBox  *m_txTimeout  = nullptr;
    QComboBox *m_logLevel   = nullptr;
    QLineEdit *m_ctlFifo    = nullptr;

    QDialogButtonBox *m_buttons = nullptr;
};

#endif
