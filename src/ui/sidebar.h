/* SPDX-License-Identifier: MIT
 * SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
 *
 * The 200 px control column: which talkgroup you are on, the lock, the
 * talkgroup list, the level meters and the output volume.
 *
 * Reads the core directly through the model tick rather than holding its own
 * copy of anything. The talkgroup manager's state — active[], recent[],
 * last_heard[], muted[] — is a public struct, so there is nothing to mirror
 * and nothing to keep in sync.
 *
 * The button list is rebuilt only when the configured talkgroup set actually
 * changes, which is on a config reload, not on a tick. Rebuilding forty
 * widgets ten times a second would be a different kind of bug from the one the
 * macOS app hit, with the same symptom.
 */
#ifndef SVXCONNECT_QT_SIDEBAR_H
#define SVXCONNECT_QT_SIDEBAR_H

#include <QWidget>
#include <QVector>

#include "core/svxcore.h"

class QLabel;
class QToolButton;
class QSlider;
class QVBoxLayout;
class LevelMeter;
class TalkgroupButton;

class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(svx_app *app, QWidget *parent = nullptr);

    /* 100 ms: talkgroup selection, mute, traffic, ages, lock, volume. */
    void tickModel(quint64 nowMs);

    /* 33 ms: meters only. Separate entry point so the fast path touches
     * nothing but two LevelMeter::setLevel() calls. */
    void tickMeters();

    /* Re-read the configured talkgroups and rebuild the button list. Call on
     * startup and after a settings change, never on a tick. */
    void rebuildTalkgroups();

private:
    void buildUi();
    void applyLockVisuals(bool locked);

    svx_app *m_app = nullptr;

    QLabel      *m_activeTg  = nullptr;
    QLabel      *m_tgInfo    = nullptr;
    QLabel      *m_preempt   = nullptr;
    QToolButton *m_lock      = nullptr;
    QVBoxLayout *m_tgLayout  = nullptr;
    QWidget     *m_tgHost    = nullptr;
    LevelMeter  *m_micMeter  = nullptr;
    LevelMeter  *m_spkMeter  = nullptr;
    QToolButton *m_mute      = nullptr;
    QSlider     *m_volume    = nullptr;

    QVector<TalkgroupButton *> m_buttons;

    /* Signature of the talkgroup set the buttons were built from, so
     * rebuildTalkgroups() can be called freely and only does work when the
     * configuration genuinely changed. */
    QString m_tgSignature;

    bool m_lockInit  = false;
    bool m_lockState = false;

    float m_micVu = 0.0f;
    float m_spkVu = 0.0f;
};

#endif
