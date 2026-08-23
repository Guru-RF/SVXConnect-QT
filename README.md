# SVXConnect-Debian

A **Qt 6 desktop client** for **SvxLink reflectors** (protocol v3 — mTLS,
AES-GCM, Opus), for **Debian and Ubuntu**. It is the desktop sibling of
[SVXConnect-CLI](https://github.com/Guru-RF/SVXConnect-CLI), a full clone of the
macOS [SVXConnect](https://svxconnect.app) app — plus the one thing the macOS
version was never allowed to ship: **a real global push-to-talk hotkey**, with
genuine key-release, that works while the window is unfocused.

> **Status: in development.** v1 is milestones M0–M4 plus the tray icon, which
> was pulled forward from M5 because the global shortcut is registered by the
> running process — closing the window would otherwise disable it silently.
> Connecting, receiving, transmitting, talkgroup switching, preferences and the
> global hotkey all work against a live reflector. Not yet built: the enhanced
> reflector WebSocket feed, the map, and packaging. See
> [docs/PLAN.md](docs/PLAN.md) for the architecture, the full feature-parity
> matrix against the macOS app, and the open decisions.

## How it fits together

- **It links the CLI's core.** The reflector client, mTLS, AES-GCM, Opus, the
  jitter buffer, the talkgroup state machine, the control FIFO, the run lock
  and the status export are compiled from
  [SVXConnect-CLI](https://github.com/Guru-RF/SVXConnect-CLI) into a static
  library. The protocol is not reimplemented. This is the same decision
  [SVXConnect-PIOS](https://github.com/Guru-RF/SVXConnect-PIOS) made for GTK3.
- **It shares one identity.** Same `~/.config/svxconnect/svxconnect.conf`, same
  `~/.config/svxconnect/pki/`, one enrolment. Configure and enrol with the CLI;
  this reads and writes the same file.
- **Only one client may be connected at a time.** The same certificate and node
  id cannot be used twice at once, so the CLI, the headless service, the Pi GUI
  and this application all take a shared run lock. Start one while another
  holds it and it refuses, naming who is in the way.

## Global push-to-talk

The CLI's README explains why a terminal cannot do hold-to-talk: it delivers
*characters*, not key events, so there is no key-release to hang it on. A
desktop can, and this is the reason the project exists.

Four backends, tried in order, behind one interface:

| Backend | Key release | Works unfocused | Setup |
|---|---|---|---|
| **XDG desktop portal** (`org.freedesktop.portal.GlobalShortcuts`) | yes | yes, Wayland and X11 | none |
| **evdev** — foot switch, USB PTT dongle | yes | yes, even at a TTY | one scoped udev rule — *built, not yet exposed in the interface* |
| **X11 key grab** (`xcb_grab_key`) | yes | yes, on real X11 | none |
| **Control FIFO** — compositor binding | yes, if the compositor sends both edges | yes | edit compositor config |

The default is `LOGO+Return`, which the desktop renders as **Meta+Return**. Set
*System Settings → Keyboard → Key Bindings → Caps Lock behavior → Make Caps Lock
an additional Hyper* and **CapsLock+Enter** becomes exactly that chord — Hyper
and Super share the same X11 modifier — giving a CapsLock push-to-talk with no
`/dev/input` access and no udev rule. CapsLock cannot be bound directly: it is a
lock state, not a modifier, and the portal silently discards it, which would
leave plain Enter bound and transmit on every Enter keypress. SVXConnect refuses
such a binding.

**SVXConnect must be running for the shortcut to work** — it is registered by
the running process — so closing the window hides it to the tray rather than
quitting.

The portal is the default and needs no elevated permission at all. Verified on
Debian 13 / KDE Plasma 6.3.6 / Wayland with `xdg-desktop-portal` 1.20.3: the
interface exposes both `Activated` **and** `Deactivated`, which is exactly the
press and release a transmitter needs.

Where no backend can deliver a release — notably wlroots compositors, which
have no GlobalShortcuts portal backend — PTT falls back to **toggle**, says so
plainly, and offers a ready-to-paste compositor snippet. A transmit watchdog
un-keys on `tx_timeout_sec` (120 s by default) and on any backend loss, because
a missed release means an unattended transmitter.

## Building

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
     qt6-base-dev qt6-base-dev-tools qt6-svg-dev \
     libssl-dev libopus-dev libevdev-dev

git clone --recurse-submodules https://github.com/Guru-RF/SVXConnect-Debian
cd SVXConnect-Debian
cmake -B build -G Ninja
cmake --build build
```

Against a sibling CLI working tree instead of the pinned submodule — the
equivalent of PIOS's `make CLI_DIR=…`:

```sh
cmake -B build -G Ninja -DCLI_DIR=../SVXConnect-CLI
```

There is deliberately **no audio development package to install**: miniaudio
`dlopen()`s `libasound.so.2` / `libpulse.so.0` at runtime.

`SVX_WINDOW_ONLY=1 ./build/svxconnect-qt` opens the window without starting the
core — useful for interface work on a machine with no certificate.

## First run

Install and enrol with the CLI first:

```sh
svxconnect --enroll     # send a CSR, wait for the sysop to sign it
svxconnect-qt
```

## Licence

MIT — see [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES](THIRD-PARTY-NOTICES).

Copyright (c) 2026 Diëlectricum BV. Written by Joeri Van Dooren, ON6URE.

SVXConnect-Qt is built with the **Qt toolkit**, © The Qt Company Ltd and
contributors, used under the **GNU Lesser General Public License version 3**.
Qt is linked dynamically and unmodified; you may replace the Qt libraries with
modified versions and relink. Only LGPL Qt modules are used — this is enforced
at configure time and again against the linked binary in CI, because several Qt
modules (Charts, Graphs, WebEngine and others) are GPL-3.0-only and linking one
would silently make the binary GPLv3.
