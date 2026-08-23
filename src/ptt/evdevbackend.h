/* SPDX-License-Identifier: MIT
 * SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
 *
 * Global PTT by reading /dev/input/event* directly.
 *
 * The only backend that observes PHYSICAL key state, which buys three things
 * nothing else can offer:
 *
 *   - A chord using a LOCK key. CapsLock is not a modifier — X11 treats it as
 *     "is the lock on", and the desktop portal silently drops it from a
 *     trigger — but at the evdev layer CapsLock is just KEY_CAPSLOCK going
 *     down and up like any other key, so "CapsLock held AND Enter pressed" is
 *     directly observable.
 *   - Foot switches and USB PTT dongles, which are not keyboards and which no
 *     shortcut API models at all.
 *   - It keeps working when the compositor has focus elsewhere, at a TTY, or
 *     over a locked screen.
 *
 * THE COST, STATED PLAINLY
 * ------------------------
 * Reading a keyboard's event device means reading EVERY keystroke on it,
 * including passwords typed into other applications. That is a real grant and
 * this backend does not pretend otherwise. It is therefore never the default:
 * the portal is, and this is opt-in.
 *
 * For a dedicated pedal the trade is trivial — the device produces one key and
 * nothing else — which is why a udev rule scoped to that one device's
 * VID:PID is the recommended setup, and why "add yourself to the input group"
 * is NOT: that grants every input device on the machine, permanently.
 *
 * The rule uses TAG+="uaccess", which gives an ACL to the user at the active
 * seat only, and follows fast user switching. Not a group.
 */
#ifndef SVXCONNECT_QT_EVDEVBACKEND_H
#define SVXCONNECT_QT_EVDEVBACKEND_H

#include "ptt/pttbackend.h"

#include <QVector>

class QSocketNotifier;

class EvdevBackend : public PttBackend {
    Q_OBJECT

public:
    explicit EvdevBackend(QObject *parent = nullptr);
    ~EvdevBackend() override;

    QString id() const override          { return QStringLiteral("evdev"); }
    QString displayName() const override { return tr("Input device"); }

    PttAvailability probe() const override;
    bool start(const PttBinding &binding) override;
    void stop() override;

    QString activeTrigger() const override;

    /* An input device we could bind to. */
    struct Candidate {
        QString path;        /* prefer the /dev/input/by-id/ symlink       */
        QString name;        /* EVIOCGNAME                                 */
        QString vid, pid;
        bool    readable = false;
        bool    isKeyboard = false;
    };
    static QVector<Candidate> enumerate();

    /* A udev rule scoped to one device, ready to paste. */
    static QString udevRuleFor(const Candidate &c);

    /* Linux key-code name <-> code, for the settings page ("CAPSLOCK", "ENTER"). */
    static QString keyName(int code);
    static int     keyCode(const QString &name);

private slots:
    void onReadable();

private:
    void closeDevice();

    int              m_fd     = -1;
    QSocketNotifier *m_notify = nullptr;
    PttBinding       m_binding;
    bool             m_holdDown = false;
    bool             m_keyDown  = false;
    bool             m_keyed    = false;
    bool             m_grabbed  = false;
};

#endif
