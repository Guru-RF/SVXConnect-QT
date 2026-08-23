/* SPDX-License-Identifier: MIT
 * SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
 *
 * Global push-to-talk: one interface, several backends.
 *
 * This is the feature the macOS app was never allowed to ship — its
 * HotkeyManager still carries the comment explaining that
 * NSEvent.addGlobalMonitorForEvents was flagged under App Store guideline
 * 2.4.5 — and the one a terminal cannot offer at all, because a terminal
 * delivers characters, not key events, so there is no release to detect.
 *
 * WHAT MAKES THIS HARDER THAN A NORMAL GLOBAL SHORTCUT
 * ----------------------------------------------------
 * A shortcut needs one edge: the activation. Push-to-talk needs BOTH edges,
 * and the release is the safety-critical one. A missed press is an annoyance;
 * a missed release is an unattended transmitter sitting on the air. Every
 * backend below is therefore judged first on whether it can report a release
 * at all, and PttManager treats "unkey" as the safe default in every error
 * path.
 *
 * THE FOUR BACKENDS
 * -----------------
 *   portal   org.freedesktop.portal.GlobalShortcuts. Works on Wayland and
 *            X11, needs no elevated permission, and — verified on Plasma
 *            6.3.6 with xdg-desktop-portal 1.20.3 — emits both Activated and
 *            Deactivated. The default.
 *
 *   evdev    Reads /dev/input/event* directly. The only backend that can see
 *            physical key state, so the only one that can do a chord with a
 *            LOCK key such as CapsLock, and the only one that works with a
 *            foot switch or a USB PTT dongle. Costs a udev rule.
 *
 *   x11      xcb_grab_key. Only on a real X11 session; on Wayland an XWayland
 *            grab sees only keys already routed to XWayland, which fails
 *            silently rather than loudly.
 *
 *   fifo     The core's existing control FIFO, driven by a compositor key
 *            binding. Already works with no code at all; it is listed so the
 *            interface can report on it and the settings page can explain it.
 *
 * A MEASURED WARNING ABOUT preferred_trigger
 * ------------------------------------------
 * The portal's preferred_trigger is a HINT. Tested against
 * xdg-desktop-portal-kde 6.3.5, with a distinct app id per probe so nothing
 * was served from kglobalaccel's cache:
 *
 *     CTRL+SHIFT+t  ->  "Ctrl+Shift+T"     honoured
 *     CTRL+Return   ->  "Ctrl+Return"      honoured
 *     F12           ->  "F12"              honoured
 *     SUPER+p       ->  ""                 rejected outright (the spec's
 *                                          modifier is LOGO, not SUPER)
 *     CAPS+Return   ->  "Return"           CapsLock SILENTLY DROPPED
 *
 * That last row is a live footgun. Asking for CapsLock+Enter and not checking
 * the reply leaves you bound to bare Enter — every time the operator presses
 * Enter in any application, anywhere, the transmitter keys. PortalBackend
 * therefore validates what came back and refuses a binding that lost its
 * modifiers, rather than trusting the request.
 */
#ifndef SVXCONNECT_QT_PTTBACKEND_H
#define SVXCONNECT_QT_PTTBACKEND_H

#include <QObject>
#include <QString>

/* What the user bound.
 *
 * Keyboard bindings are expressed in the freedesktop Shortcuts syntax the
 * portal takes: modifiers CTRL / ALT / SHIFT / LOGO joined with '+', then an
 * xkbcommon keysym name without its XKB_KEY_ prefix — "CTRL+SHIFT+t".
 *
 * Device bindings name an /dev/input node and up to two Linux key codes. Two,
 * because a chord is the only way to use an otherwise-useful key as PTT
 * without losing it: CapsLock+Enter transmits, Enter alone still means Enter.
 */
struct PttBinding {
    enum Kind { Keyboard, Device };

    Kind    kind = Keyboard;

    QString trigger;              /* Keyboard: "CTRL+SHIFT+t"           */

    QString devicePath;           /* Device: prefer /dev/input/by-id/... */
    int     holdCode = 0;         /* Device: modifier held, 0 = none     */
    int     keyCode  = 0;         /* Device: the key that keys the radio */
    bool    grab     = false;     /* Device: EVIOCGRAB. Pedals only.     */

    bool isValid() const
    {
        return kind == Keyboard ? !trigger.isEmpty()
                                : (!devicePath.isEmpty() && keyCode != 0);
    }
};

/* Whether a backend can be used here, and if not, what the user must do. */
struct PttAvailability {
    enum State { Available, NeedsSetup, Unavailable };

    State   state = Unavailable;
    QString reason;          /* shown verbatim in the settings table       */
    QString instructions;    /* paste-able fix: a udev rule, a config line */
    bool    hasRelease = false;   /* false => toggle only; say so loudly   */
};

class PttBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~PttBackend() override = default;

    virtual QString id() const = 0;            /* "portal" | "evdev" | "fifo" */
    virtual QString displayName() const = 0;

    /* Cheap, side-effect-free, and safe to call repeatedly — the settings
     * table re-runs it on a timer so a udev rule applied in another window
     * shows up without reopening the dialog. */
    virtual PttAvailability probe() const = 0;

    virtual bool start(const PttBinding &binding) = 0;
    virtual void stop() = 0;

    /* What the backend ACTUALLY bound, which for the portal may differ from
     * what was asked for. Display this, never the requested trigger. */
    virtual QString activeTrigger() const { return QString(); }

signals:
    void pressed();
    void released();

    /* The backend can no longer guarantee it will see a release: the portal
     * session closed, the device was unplugged, the grab was stolen.
     * PttManager unkeys immediately on this — a transmitter that cannot be
     * stopped by its own button must not stay keyed. */
    void lost(const QString &why);

    /* The bound trigger changed under us (the user reassigned it in the
     * desktop's own shortcut settings). */
    void triggerChanged(const QString &human);
};

#endif
