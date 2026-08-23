# SVXConnect-Debian — Definitive Implementation Plan

**Target:** a Qt 6 / C++ desktop client for SvxLink v3 reflectors on Debian & Ubuntu, a full clone of SVXConnect-OSX plus a real global PTT hotkey.
**Repo:** [`/home/ure/Git/Guru-RF/SVXConnect-Debian`](/home/ure/Git/Guru-RF/SVXConnect-Debian) (M0 and M1 are built and running in the working tree; `main` still has no commits — everything below is uncommitted).
**Licence:** MIT (Debian DEP-5 name: `Expat`).

---

## Decisions already made

These three are settled by the repo owner. **Do not re-litigate them; implement them.**

### D-1. Copyright and credits (settles §8 Q16, which was previously flagged blocking)

| Artefact | Text |
|---|---|
| `LICENSE`, `debian/copyright` (`Files: *`), AppStream `<project_license>` | **`Copyright (c) 2026 Diëlectricum BV`** — MIT / DEP-5 `Expat`. The company name carries the diaeresis: `Di` + `ë` (U+00EB) + `lectricum`. |
| About dialog, AppStream `<developer_name>` | **Joeri Van Dooren, ON6URE** |

**Consequence, and it is a task, not a note:** [`SVXConnect-CLI/LICENSE`](../../SVXConnect-CLI/LICENSE) and [`SVXConnect-PIOS/LICENSE`](../../SVXConnect-PIOS/LICENSE) currently read `Copyright (c) 2026 Joeri Van Dooren`. Both must be updated to `Copyright (c) 2026 Diëlectricum BV` so the four repos agree, and SVXConnect-CLI's `THIRD-PARTY-NOTICES` header must be updated in the same commit. This lands as **task M0-T1**, before this repo's own `LICENSE` is written — otherwise `debian/copyright`'s stanza for the vendored `third_party/svxconnect-cli/*` (§6.2) would have to describe a holder the vendored tree itself contradicts. Do **not** append `, ON6URE` to any copyright line: the callsign belongs in the credits, not in the legal holder.

### D-2. v1 scope is M0–M4 only

**v1 = repo skeleton + core loop + main window + preferences + global PTT hotkey.** Everything else is *Beyond v1*.

Not in v1: the enhanced-reflector WebSocket feed (M6), the map (M7), local history persistence and QTH/geolocation (M8), the enrolment wizard (M9), diagnostics/i18n polish (M10), packaging (M11), theming polish (M12).

Three consequences of dropping the feed that the rest of this document states plainly wherever they bite:

- **There is no Reflector activity pane in v1.** The Activity panel ships with **Local + Recent** sections only.
- **There is no other-node data at all in v1**, so no map is possible — not in v1, and not later either, unless the feed lands first. The map depends on the feed, not the other way round.
- The macOS **`24h data` badge**, the **session list** and the **feed-driven `lastHeard`** do not exist in v1. `lastHeard` comes from `tgm_last_heard()`, which is the clock the core's own linger/idle/preemption logic runs on (Appendix A / R2).

Nothing is deleted from the *plan*, but be clear about what this document does and does not carry. §3.4's Reflector rows, §3.7 (tray), §3.12 (Map tab), §3.13 (Map pane) and the feed-dependent rows of §3.14/§3.15 keep their headings here as markers, and their delivery is specified in §7's **M5–M8** rows with the acceptance criteria intact. **The full macOS-parity tables for them are not reproduced in this document** — there is no §9. That detail is expensive to re-derive and it has to come back out of the macOS recon when M6–M8 are actually scheduled; the milestone rows name the specific behaviours to go looking for, which is what makes that re-derivation tractable rather than open-ended.

Marking convention used throughout: **[v1]** and **[beyond v1]** on headings, and a `v1?` column or an explicit note where a single table mixes both.

### D-3. Tray / single instance / URL scheme is post-v1 — but the lock is not

The tray icon, the 260 px popup and the `svxconnect://` URL **handler** are M5, i.e. beyond v1.

**In v1 regardless, because they are correctness and not features:**

- the **run lock** (`svx_lock_acquire` before `app_new()`, released after `app_free()`),
- the **distinct owner kind `"gui-qt"`** (never `"gui"` — PIOS already uses that and raises via `SIGUSR1`, not `QLocalSocket`; §1.4),
- **argv parsing before the lock check**, so a forwarded `svxconnect://` URL is never silently dropped even though v1 has nothing to do with it yet. v1 parses it, logs it, and exits cleanly with a "not supported in this version" message rather than losing it.

---

## 1. Architecture

### 1.1 The layering decision — CONFIRMED: link the C core, do not reimplement **[v1]**

**Verdict: link `SVXConnect-CLI`'s C core as a static library and write only the presentation, the second data plane, and the PTT layer in C++.** Reimplementing the protocol in Qt/C++ is rejected outright.

The reasoning is not "reuse is nice". It is that the C core is *materially better than the Swift app it is replacing* in five places that are expensive and dangerous to re-derive:

| Capability | C core | macOS Swift app |
|---|---|---|
| UDP replay protection (monotonic authenticated GCM counter, `n_replayed`) | yes (`crypto.c:157-199`) | **absent** — every well-formed packet accepted |
| Packet-loss inference from the counter, feeding `loss_pct` | yes | absent |
| Opus PLC / concealment before the following frame | yes (`jitter.c:59-72`) | absent — a lost packet is a hole |
| Real jitter buffer with prefill/gate/drift catch-up | yes (`jitter.c`) | leans on `AVAudioPlayerNode` |
| Mic AGC + DC blocker + dB pre-gain, per-transmission codec reset | yes (`codec.c`) | absent |

Add to that: SRV resolution (`net.c`), mTLS with the correct TLS 1.2 pin and no `close_notify`, the *correct* RX IV construction (the Swift `CryptoManager.decrypt` builds a 10-byte IV and only works by accident), the talkgroup state machine with three documented corrections over the Swift original, and the run-lock / status-file / control-FIFO coordination that lets the GUI coexist with `svxconnect` on one machine.

**Also explicitly rejected: "drive a headless `svxconnect` over the control FIFO".** It avoids the run-lock conflict, but the only feedback channel is a one-line status file — no levels, no talker list, no stats, no logs. You would be building a remote control, not a client.

**What the core does *not* give you, and must be new Qt code.** In v1 this is much smaller than the draft assumed, because the feed and the map are out:

| New Qt code | v1? |
|---|---|
| Writing the config file back — the core has `config_load`/`config_set` but **no `config_save`** | **yes** |
| The whole presentation layer, models, delegates, meters, preferences | **yes** |
| `PttManager` and its four backends | **yes** |
| The "enhanced reflector" WebSocket feed at `wss://reflector.<domain>/` — the *only* source of other nodes' coordinates, 24 h sessions and authoritative `isTalker`. `grep -riE 'websocket|wss://' SVXConnect-CLI/src/` returns zero hits | beyond v1 (M6) |
| Portal JSON fetch (`https://portal.<domain>/{talkgroups,callsigns}.json`) | beyond v1 (M6) |
| Map, clustering/spiderfy overlay, camera logic | beyond v1 (M7) |
| Persisted local talker history (the core keeps 16 entries in RAM, no persistence) | beyond v1 (M8) |
| Callsign enrichment (QRZ) | dropped, §8 Q2 |

### 1.2 Layer diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│  ui/          QMainWindow, sidebar, activity panes, prefs            │  C++17
│               [beyond v1: tray, map pane, session delegate]          │
├───────────────┬──────────────────┬───────────────┬───────────────────┤
│  model/       │  net/            │  ptt/         │  settings/        │
│  Qt models    │  [beyond v1:     │  4 backends   │  conf reader +    │
│  over core    │   QWebSocket     │  behind one   │  WRITER, QSettings│
│               │   feed, portal]  │  interface    │  for GUI-only keys│
├───────────────┴──────────────────┴───────────────┴───────────────────┤
│  core/        CoreLoop (event-loop integration) · LogBridge          │
│               svxcore.h (C++-clean shim over app.h) · DevList        │
├──────────────────────────────────────────────────────────────────────┤
│  svxcore (static lib) — 24 .c files from third_party/svxconnect-cli  │  C gnu11
│  protocol · mTLS · AES-GCM · Opus · jitter · TG manager · FIFO ·     │
│  run lock · status file · PKI · config parser                        │
└──────────────────────────────────────────────────────────────────────┘
```

**Hard rule: nothing above `core/` calls `app_*`, `rc_*` or `tgm_*` directly.** Everything goes through `model/` façade objects, so there is exactly one place that knows the core is C and single-threaded.

### 1.3 The C++ inclusion problem, and its fix **[v1]**

No CLI header has `extern "C"` guards, and `app.h` is **not parseable by g++ at all**: `app.h → audio/codec.h → audio/dev.h → common/ring.h` uses `_Atomic` (`ring.h:32-33`, `dev.h:78-79`), and g++ 14 rejects it with `error: '_Atomic' does not name a type`.

Verified C++-clean (wrap in `extern "C" {}` and they compile): `common/config.h`, `common/status.h`, `common/lock.h`, `common/log.h`, `common/util.h`, `common/pki.h`, `ctl/ctlfifo.h`, `reflector/client.h`, `reflector/enroll.h`, `tg/tgmanager.h`.
Verified broken: `common/ring.h`, `audio/dev.h`, `audio/codec.h`, `audio/jitter.h`, `src/app.h`.

**Decision: hand-write `src/core/svxcore.h`** — a C++-clean shim that includes only the ten clean headers and re-declares the `app_*` prototypes, `app_log_line`, `svx_devinfo` and the device-enumeration entry points (`svx_audio_list`, `svx_audio_resolve`, `svx_dev_name`, `svx_mic_status`). All of those touch only C++-clean types.

**The trap the draft walked into.** `svx_devinfo` (`dev.h:44`) and `app_log_line` (`app.h:37`) are **anonymous** structs:

```c
/* audio/dev.h:44 */ typedef struct { char id[256]; char name[256]; int is_default; } svx_devinfo;
/* app.h:37       */ typedef struct { int level; char text[240]; } app_log_line;
```

C11 permits repeating a `typedef` only when it names the *same* type, and two anonymous struct definitions are **different types**. A translation unit that sees both the upstream header and a shim that re-declares them dies with `error: conflicting types for 'svx_devinfo'; have 'struct <anonymous>'` — and `tools/shim_check.c` is deliberately exactly such a translation unit, so the drift guard as drafted could never compile. **Only function prototypes may be safely duplicated**; a repeated compatible prototype is always legal.

**Fix: make the two definitions mutually exclusive using the upstream include guards, and prove they agree with a layout assertion compiled from both sides.**

#### `src/core/svxcore.h`

```c
#pragma once
/* The ONLY C-core header the C++ side may include.
 *
 * app.h and audio/dev.h are not parseable by g++ (they use _Atomic), so this
 * shim includes the ten C++-clean headers and re-declares, by hand, the
 * app_* / svx_audio_* surface the GUI needs.
 */

/* System headers first, OUTSIDE extern "C": the CLI headers pull in <stdint.h>
   and friends, and while glibc's C headers are safe under extern "C", not
   putting them there costs nothing and removes the question entirely. */
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The ten verified C++-clean CLI headers. None of them use _Atomic. */
#include "common/config.h"
#include "common/lock.h"
#include "common/log.h"
#include "common/pki.h"
#include "common/status.h"
#include "common/util.h"
#include "ctl/ctlfifo.h"
#include "reflector/client.h"
#include "reflector/enroll.h"
#include "tg/tgmanager.h"

/* ---------------------------------------------------------------------------
 * Types re-declared from headers g++ cannot read.
 *
 * WHY THE GUARDS: svx_devinfo and app_log_line are ANONYMOUS structs upstream,
 * and two anonymous struct definitions are distinct types even when they are
 * spelled identically -- so a C TU that sees BOTH audio/dev.h and this file
 * fails with "conflicting types". Reusing the upstream include guards makes the
 * definitions mutually exclusive: in a C++ TU only this copy exists, in
 * tools/shim_check.c only the upstream copy does. tools/svxcore_layout.inc then
 * proves the two spellings have identical size and offsets.
 *
 * Copy the guard macro names and the opaque-handle spelling VERBATIM from
 * audio/dev.h and src/app.h when this file is first written; if upstream ever
 * renames a guard, shim_check.c fails immediately, which is the point.
 * ------------------------------------------------------------------------- */
#ifndef SVX_AUDIO_DEV_H
#define SVX_RATE   16000
#define SVX_FRAME  320
typedef struct svx_dev svx_dev;                      /* opaque handle */
typedef struct { char id[256]; char name[256]; int is_default; } svx_devinfo;
typedef enum { SVX_DEV_OK = 0, SVX_DEV_STOPPED, SVX_DEV_REROUTED,
               SVX_DEV_LOST } svx_dev_event;
typedef enum { SVX_MIC_DENIED = -1, SVX_MIC_UNDETERMINED = 0,
               SVX_MIC_GRANTED = 1 } svx_mic_state;
#endif

#ifndef SVX_APP_H
typedef struct svx_app svx_app;                      /* opaque handle */
typedef struct { int level; char text[240]; } app_log_line;
#endif

/* --- app_* prototypes. Duplicating a compatible prototype is always legal. --- */
svx_app *app_new(const svx_config *cfg, int no_tx);
void     app_free(svx_app *a);
void     app_set_owner_kind(svx_app *a, const char *kind);
void     app_start(svx_app *a);                      /* returns void (app.h:50) */
int      app_poll_fds(svx_app *a, struct pollfd *p, int max);
int      app_next_timeout_ms(svx_app *a, uint64_t now);
void     app_service(svx_app *a, uint64_t now);
int      app_should_quit(const svx_app *a);
void     app_ptt(svx_app *a, ctl_tristate v);
/* ... the remaining ~30 app_* prototypes, verbatim from app.h ... */
void     app_set_observer(svx_app *a, void (*fn)(void *), void *user);
int      app_log_snapshot(const svx_app *a, app_log_line *out, int max);
uint64_t app_log_serial(const svx_app *a);

/* --- audio device enumeration (the only _Atomic-free part of dev.h) ---
   NOTE the argument order: `capture` comes FIRST in both, and it is an
   IS-CAPTURE flag, not the "playback" flag the draft assumed. Getting it
   backwards enumerates the wrong direction and silently lists the wrong
   devices in both pickers. Verified against dev.h:60,64,73. */
int           svx_audio_list(int capture, svx_devinfo *out, int max);
int           svx_audio_resolve(int capture, const char *want, svx_devinfo *out);
const char   *svx_audio_backend_name(void);
const char   *svx_dev_name(svx_dev *d);
svx_mic_state svx_mic_status(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
```

#### The two drift-guard translation units (both compiled by the normal build)

`tools/svxcore_layout.inc` — literal constants, included from both sides:

```c
/* Included from a C TU that sees the UPSTREAM definitions and from a C++ TU
   that sees only the shim's copies. If both compile, the two spellings agree.
   Literal numbers on purpose: a shared macro would drift with the thing it is
   supposed to be checking. */
#ifdef __cplusplus
#  define SVXC_ASSERT(c, m) static_assert(c, m)
#else
#  define SVXC_ASSERT(c, m) _Static_assert(c, m)
#endif

SVXC_ASSERT(sizeof(svx_devinfo)                == 516, "svx_devinfo size drifted");
SVXC_ASSERT(offsetof(svx_devinfo, name)        == 256, "svx_devinfo.name drifted");
SVXC_ASSERT(offsetof(svx_devinfo, is_default)  == 512, "svx_devinfo.is_default drifted");
SVXC_ASSERT(sizeof(app_log_line)               == 244, "app_log_line size drifted");
SVXC_ASSERT(offsetof(app_log_line, text)       ==   4, "app_log_line.text drifted");
```

`tools/shim_check.c` — compiled **as C**, part of the `svxcore` target:

```c
/* Upstream headers FIRST: they define SVX_APP_H / SVX_AUDIO_DEV_H, so svxcore.h skips
   its own struct copies and only its PROTOTYPES are compared against app.h's.
   If a prototype ever drifts, gcc says "conflicting types" here instead of the
   shim silently lying to the C++ side about a signature. */
#include "app.h"
#include "audio/dev.h"
#include "core/svxcore.h"
#include <stddef.h>
#include "svxcore_layout.inc"      /* checks the UPSTREAM layout */

int svxconnect_shim_check_ok = 1;
```

`tools/shim_layout_check.cpp` — compiled **as C++**, part of the app target:

```cpp
/* The mirror image: only the shim is visible here, so this checks the SHIM's
   copies of the structs against the same literals. Both TUs green == the two
   definitions are layout-compatible. */
#include "core/svxcore.h"
#include <cstddef>
#include "svxcore_layout.inc"

int svxconnect_shim_layout_check_ok = 1;
```

`-D_Atomic=` on the C++ TUs is the escape hatch if the shim ever becomes tiresome, but it is a struct-layout landmine and must not be the default. The permanent fix is upstream patch 14 (§8 Q7): `extern "C"` guards, an `SVX_ATOMIC` macro and **tagged** typedefs in the public headers. When that lands, `src/core/svxcore.h`, `tools/shim_check.c`, `tools/shim_layout_check.cpp` and `tools/svxcore_layout.inc` are all deleted in one commit.

### 1.4 Event-loop integration — precise **[v1]**

The core is written around `poll()`. `app.h:52-53` states the contract: *"`app_service()` must be called every time round the loop even when `poll()` returned nothing, because it also drives the timers."* PIOS folded this into a custom `GSource`; Qt gets a `QSocketNotifier` registry plus one single-shot `QTimer`, both funnelling into **one** slot.

Four facts drive the design, and getting any of them wrong produces a specific, known failure:

1. **The fd set is dynamic.** `rc_poll_fds()` (`client.c:462-484`) always returns the connect-worker wake pipe, and adds the TLS fd and UDP fd only while connected; `app_poll_fds()` adds the control-FIFO fd. Maximum 4 today; size the array at 16 like PIOS and `ui.c`. The `events` mask is dynamic too — `POLLOUT` appears on the TLS fd only when `tls_want_write()`, and never alone.
2. **The timer must be an absolute deadline, re-armed every pass** — not "ask again how long". PIOS's `appsource.c:65-73` documents the failure mode verbatim: while transmitting, `app_next_timeout_ms()` never reaches 0 (the core keeps asking for a 5 ms poll to drain the mic ring), so a naive implementation only ever services on an inbound fd event — *receive works because packets arrive, transmit never gets serviced and nothing is ever sent.* `QTimer::start(ms)` is a real deadline, so this is correct by construction.
3. **`QSocketNotifier` is level-triggered and must never be left enabled on a closed fd**, or the event loop spins at 100 % on `POLLNVAL`.
4. **Notifiers must never be destroyed and recreated per pass.** This was the draft's most serious defect. `deleteLater()` defers to the event-loop level *at which it was called*: open a modal Preferences dialog or a `QMessageBox` (a nested `QDialog::exec()`), let the link drop and reconnect inside it, and the pending `DeferredDelete` events do not run until the nested loop exits — while `reconcile()` has already created a replacement notifier on the same fd number, because fd reuse after `close()`/`socket()` is the common case, not the exotic one. Qt then prints `QSocketNotifier: Multiple socket notifiers for same socket <fd> and type Read` and behaviour is undefined. **The fix is a per-fd registry, created on first sight and thereafter only enabled and disabled.** A disabled notifier on a closed fd is inert and costs nothing.

#### `src/core/coreloop.h`

```cpp
#pragma once
#include <QHash>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <array>
#include <poll.h>
#include "core/svxcore.h"

class CoreLoop;
/// Namespace-scope declaration, BEFORE the class. A friend declaration alone
/// does not make the name findable by ordinary lookup, and there is no ADL on
/// void*, so app_set_observer(&svxc_observer_trampoline) would not compile
/// without this line.
void svxc_observer_trampoline(void *user);

/// Runs the SVXConnect core inside the Qt event loop.
/// GUI-thread only; see LogBridge for the one cross-thread path.
class CoreLoop : public QObject {
    Q_OBJECT
public:
    explicit CoreLoop(svx_app *app, QObject *parent = nullptr);
    ~CoreLoop() override;

    void kick();                       ///< call once, immediately after app_start()

signals:
    void coreChanged();                ///< coalesced redraw hint, always QUEUED (see below)
    void quitRequested();              ///< app_should_quit() went true

private slots:
    void serviceOnce();

private:
    void reconcile();
    void armTimer();
    void noteSpin();
    friend void svxc_observer_trampoline(void *);

    enum { R = 0, W = 1 };
    using FdPair = std::array<QSocketNotifier *, 2>;   ///< [R] and [W] for one fd

    svx_app *m_app;
    QTimer   m_timer;
    /// Keyed by fd NUMBER. Entries are created once and then only enabled and
    /// disabled; they are never deleted from inside a service frame. QHash
    /// value-initialises a missing value, so both pointers start null.
    QHash<int, FdPair> m_notes;
    bool     m_dirty     = false;
    bool     m_inService = false;
    bool     m_pending   = false;
    int      m_spin      = 0;
    qint64   m_spinSince = 0;
};
```

#### `src/core/coreloop.cpp` — the load-bearing parts

```cpp
#include "core/coreloop.h"
#include <QMetaObject>
#include <QStringList>

void svxc_observer_trampoline(void *user) {
    // Fires SYNCHRONOUSLY on the GUI thread from inside app_service() and from
    // inside app_set_volume()/app_toggle_output_mute()/banner_set()/... .
    // Never emit from here: a slot that called back into app_* would re-enter
    // the core mid-operation. Just mark dirty; serviceOnce() queues one emit.
    static_cast<CoreLoop *>(user)->m_dirty = true;
}

CoreLoop::CoreLoop(svx_app *app, QObject *parent) : QObject(parent), m_app(app) {
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);   // the 5 ms TX cadence must not be coalesced
    connect(&m_timer, &QTimer::timeout, this, &CoreLoop::serviceOnce);
    app_set_observer(m_app, svxc_observer_trampoline, this);
}

CoreLoop::~CoreLoop() {
    app_set_observer(m_app, nullptr, nullptr);      // the app outlives us in main()
    for (auto it = m_notes.begin(); it != m_notes.end(); ++it)
        for (auto *n : it.value()) if (n) n->setEnabled(false);
}

void CoreLoop::kick() { serviceOnce(); }

void CoreLoop::serviceOnce() {
    // Never re-enter -- but never DROP the activation either. A notifier that
    // fired while we were inside app_service() has real data behind it; setting
    // a pending flag and re-running costs nothing, while returning silently
    // waits for the timer (up to 20 ms) to rediscover it.
    if (m_inService) { m_pending = true; return; }
    m_inService = true;

    do {
        m_pending = false;
        noteSpin();
        m_timer.stop();

        // UNCONDITIONAL. This is the whole contract. It drains the control FIFO,
        // runs rc_service() (firing every reflector callback synchronously on
        // this thread), ticks the jitter buffer and the TG manager, pumps the mic
        // ring into Opus and onto the wire, and exports the status file.
        app_service(m_app, now_ms());

        if (app_should_quit(m_app)) { m_inService = false; emit quitRequested(); return; }

        reconcile();      // the fd set and the events mask may both have changed
        armTimer();       // absolute deadline, every pass
    } while (m_pending);

    // Queue the redraw hint so it lands OUTSIDE this service frame. A slot that
    // opens a modal dialog spins a nested event loop; emitting synchronously here
    // would let the 5-20 ms timer re-enter serviceOnce() underneath the frame that
    // is about to run reconcile()/armTimer() again. m_inService stays true until
    // the emit is queued.
    const bool dirty = m_dirty;
    m_dirty = false;
    if (dirty)
        QMetaObject::invokeMethod(this, [this] { emit coreChanged(); }, Qt::QueuedConnection);

    m_inService = false;
}

void CoreLoop::reconcile() {
    struct pollfd np[16];
    int nn = app_poll_fds(m_app, np, 16);
    if (nn < 0) nn = 0;

    // Compute the DESIRED set explicitly. Never "re-enable everything I still
    // hold": with a per-fd registry that invariant is simply false, because the
    // registry outlives the fds.
    QHash<int, short> want;
    want.reserve(nn);
    for (int i = 0; i < nn; ++i)
        want[np[i].fd] = short(want.value(np[i].fd, 0) | np[i].events);

    for (auto it = want.cbegin(); it != want.cend(); ++it) {
        const int   fd = it.key();
        const short ev = it.value();
        FdPair &pair = m_notes[fd];            // value-initialised -> {nullptr, nullptr}

        if (!pair[R]) {
            // The Read notifier is created UNCONDITIONALLY. The kernel reports
            // POLLERR/POLLHUP regardless of `events`, which is how a Read notifier
            // covers what PIOS gets from its unconditional G_IO_ERR|G_IO_HUP; and a
            // hypothetical POLLOUT-only fd would otherwise never be serviced at all.
            // Q_ASSERT was considered and rejected: it compiles out under
            // QT_NO_DEBUG, i.e. in exactly the build where a hang would matter.
            if (!(ev & POLLIN))
                log_warn("coreloop: fd %d has no POLLIN (events=0x%x); "
                         "creating a Read notifier anyway", fd, unsigned(ev));
            pair[R] = new QSocketNotifier(fd, QSocketNotifier::Read, this);
            connect(pair[R], &QSocketNotifier::activated, this, &CoreLoop::serviceOnce);
        }
        if ((ev & POLLOUT) && !pair[W]) {      // appears/vanishes with tls_want_write()
            pair[W] = new QSocketNotifier(fd, QSocketNotifier::Write, this);
            connect(pair[W], &QSocketNotifier::activated, this, &CoreLoop::serviceOnce);
        }
        pair[R]->setEnabled(true);
        if (pair[W]) pair[W]->setEnabled((ev & POLLOUT) != 0);
    }

    // Everything we hold that is NOT wanted goes quiet. Disabled, not deleted:
    // we may be inside one of these notifiers' own activated() emission, and the
    // fd number will very likely come back on the next reconnect.
    for (auto it = m_notes.begin(); it != m_notes.end(); ++it) {
        if (want.contains(it.key())) continue;
        for (auto *n : it.value()) if (n) n->setEnabled(false);
    }
    // No eviction path is needed: app_poll_fds() returns at most 4 descriptors and
    // the kernel reuses the lowest free fd number, so the registry stays tiny. If
    // it ever exceeds 32 entries, log once at WARN -- that is a core bug, not a
    // reason to start deleting objects from inside a slot.
}

void CoreLoop::armTimer() {
    // now_ms() is sampled HERE, after app_service() has returned -- not at frame
    // entry. app_service() can block for milliseconds (status_export writes a
    // file; the log sink can hit the disk), and computing the deadline from the
    // entry timestamp would over-sleep by exactly that much, stalling the 5 ms TX
    // cadence. Keep this comment: it is the only thing standing between a future
    // refactor and a transmit path that mysteriously drops audio.
    int to = app_next_timeout_ms(m_app, now_ms());   // documented 0..1000
    if (to < 0) to = 1000;                           // never sleep forever
    m_timer.start(to);
}

void CoreLoop::noteSpin() {
    const qint64 t = qint64(now_ms());
    if (t - m_spinSince > 100) { m_spinSince = t; m_spin = 0; return; }
    if (++m_spin < 200) return;

    // Name the fds AND what the kernel currently says about them. A bare
    // "spinning" line is unactionable, and this only ever fires on a real core
    // bug (an fd the core stopped draining), so the extra poll() is free.
    struct pollfd snap[16];
    int ns = app_poll_fds(m_app, snap, 16);
    if (ns > 0) poll(snap, nfds_t(ns), 0);           // timeout 0: just read revents
    QStringList detail;
    for (int i = 0; i < ns; ++i)
        detail << QStringLiteral("fd=%1 events=0x%2 revents=0x%3")
                      .arg(snap[i].fd).arg(snap[i].events, 0, 16).arg(snap[i].revents, 0, 16);
    log_warn("coreloop: %d services in %lld ms -- %s", m_spin, (long long)(t - m_spinSince),
             qPrintable(detail.join(QStringLiteral(", "))));

    m_spin = 0;
    m_spinSince = t;
}
```

**Spin-guard remedy, deliberately mild.** The guard logs and keeps going; it does not disable anything. The control FIFO is opened `O_RDWR|O_NONBLOCK` (`ctlfifo.c:60`) and `ctl_drain()` always empties it, so a runaway there is a core bug you want to *see*, not one you want papered over by a backoff that hides it.

**`now_ms()` is the only clock.** It comes from `common/util.h` and is monotonic. Never substitute `QDateTime`, `QElapsedTimer` or `time(2)` for anything fed to `app_next_timeout_ms()` / `app_service()`.

**The idle desktop is a permanent 50 Hz wakeup, by design.** `app_next_timeout_ms()` clamps the answer to **≤20 ms whenever `audio_ready`** — not only when connected — and to ≤5 ms while transmitting (`app.c:665-668`). So a launched, *disconnected*, idle GUI still services 50 times a second because the playback device is open and the jitter buffer must be ticked. That is the core's contract and PIOS behaves identically. Say so here so that nobody later "optimises" it into a broken jitter tick. On a Pi it is measurable; if it ever needs fixing, it is fixed upstream in `app_next_timeout_ms()`, not in this file.

#### Startup / shutdown order **[v1]** — corrected

Three defects in the draft's sketch: `w.show()` does not put a window on screen, `CoreLoop` was constructed after `app_start()` so the observer installed late, and `cfg` was a bare stack local in `main()` while the prose insisted it must not be.

```cpp
// src/main.cpp
int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");
    signal(SIGPIPE, SIG_IGN);

    QApplication qapp(argc, argv);

    // RE-ASSERT after QApplication. Constructing it loads a platform theme
    // plugin, and the GTK3 one (qt6-gtk-platformtheme, which GNOME sessions
    // load) calls gtk_init() -> setlocale(LC_ALL, ""). Under nl_BE/fr_FR that
    // puts the comma back, and nodeinfo_build_json() formats the coordinates
    // with snprintf("%.7f") (nodeinfo.c:42-43). They are embedded as JSON
    // STRINGS -- "pos":{"lat":"%s","long":"%s",...} at nodeinfo.c:57 -- so the
    // frame stays well-formed JSON and nothing is rejected: the reflector
    // simply receives "51,0500000" where it expects a number, and your node is
    // placed wrongly or not at all. Silent on both sides, which is worse than
    // a parse error.
    setlocale(LC_NUMERIC, "C");
    {
        char probe[32];
        snprintf(probe, sizeof probe, "%.7f", 51.05);
        if (strchr(probe, ','))                 // runtime check, survives QT_NO_DEBUG
            log_err("LC_NUMERIC is not \"C\" (%%.7f gives \"%s\") -- "
                    "MsgNodeInfo will be rejected by the reflector", probe);
    }

    // PIOS main.c:251's escape hatch: build the window with no core at all.
    // Ten lines, invaluable for layout work, and it must NOT take the run lock.
    if (qgetenv("SVX_WINDOW_ONLY") == "1") {
        MainWindow w(nullptr, nullptr, nullptr);
        w.show();
        return qapp.exec();
    }

    // Parse argv BEFORE the lock check. `xdg-open svxconnect://ptt/toggle`
    // launches a SECOND process with the URL in argv, and the "already running"
    // branch has to FORWARD it, not drop it. (v1 has no URL handler; it parses,
    // logs and reports the URL rather than losing it -- see D-3.)
    const AppArgs args = AppArgs::parse(qapp.arguments());

    // ConfigStore OWNS the single mutable svx_config for the whole process.
    // app.c:583 stores the pointer without copying, and app_set_volume /
    // app_set_input_device / app_set_output_device const_cast and MUTATE it in
    // place. A stack local in main() is fine -- it outlives app_free() -- but a
    // COPY or a snapshot inside ConfigStore is not: the core's mutations would be
    // lost on the next save. app_new() gets a pointer to the store's own member.
    ConfigStore store;
    store.load(args.configPath);                    // config_defaults + config_load

    const int lvl = log_level_from_name(store.config().log_level);
    if (lvl >= 0) log_set_level(lvl);               // -1 on a typo would clamp to
                                                    // LOG_ERR and silence the app
    LogBridge::install(&store);                     // BEFORE app_new(): the connect
                                                    // worker logs from its own thread

    RunLock lock;
    switch (lock.acquire(store.config().lock_file)) {          // owner kind "gui-qt"
    case RunLock::Acquired:    break;
    case RunLock::HeldByUs:    return SingleInstance::forward(args) ? 0 : 1;
    case RunLock::HeldByPios:  RunLock::raisePios(lock.holderPid());
                               Dialogs::piosOwnsTheRadio(); return 1;
    case RunLock::HeldByCli:   Dialogs::cliOwnsTheRadio(lock.holderPid()); return 1;
    case RunLock::Error:       Dialogs::lockFileUnwritable(store.config().lock_file); return 1;
    }

    svx_app *a = app_new(&store.mutableConfig(), /*no_tx=*/0);
    if (!a) return 1;
    app_set_owner_kind(a, "gui-qt");     // NOT "gui": PIOS owns that. char[16] upstream.

    // CoreLoop BEFORE app_start(). app_start() (app.c:615) runs audio_start(),
    // tx_open() and rc_start(), and rc_start() reaches set_state() -> hl_state()
    // SYNCHRONOUSLY on this thread (client.c:80-84), so the first state change
    // and the first log lines happen inside the call. (On Linux app_start()
    // itself never banner_set()s: tx_open()'s RX-ONLY banner is inside
    // #if defined(__APPLE__) (app.c:381) and the silent-mic banner lives in
    // tx_stop(), app.c:317/320. The ordering rule stands anyway -- an observer
    // installed afterwards misses whatever the first frame had to say.)
    CoreLoop loop(a);
    MainWindow w(a, &store, &loop);
    w.show();

    // show() only POSTS events -- nothing is on screen until the event loop runs.
    // Calling the BLOCKING app_start() (miniaudio enumeration + init, tens to
    // hundreds of ms) here would paint a white rectangle for that whole time.
    // Defer it by one event-loop turn instead.
    QTimer::singleShot(0, &w, [a, &loop] { app_start(a); loop.kick(); });

    const int rc = qapp.exec();

    PttManager::instance().forceUnkey(a);   // never leave a transmitter keyed
    app_free(a);                            // FIRST: clears the status file, joins the worker
    lock.release();                         // THEN
    return rc;
}
```

`SIGINT`/`SIGTERM` are handled through a `signalfd` + `QSocketNotifier` that calls `QCoreApplication::quit()` — never `_exit`, or `app_free()` never runs and you leave a stale status file, a held lock, **and possibly a keyed transmitter**. The teardown path above must be reachable from the signal path; that is a test at M4.

**Hard rule, stated here and repeated in `enrolwizard.h`: `enroll_run()` is NEVER called in-process.** The symbol *is* linked into this binary (it is part of `svxcore`), so this is a discipline, not a compile error. `enroll.c:229-233` installs process-wide `SIGINT`/`SIGTERM` handlers, which would silently replace the signalfd path above and take the app's teardown with them. Enrolment is always a `QProcess` running `/usr/bin/svxconnect --enroll` (§3.14, and §7's M9 row). `svxconnect --enroll` does **not** take the run lock (verified: `main.c:268-271` calls `enroll_run` with no `acquire_run_lock`), so the wizard works while the GUI holds it.

### 1.5 Repaint model — two timers, never the observer **[v1]**

`app_set_observer()` sounds like the change signal you want. It is nearly inert: `notify()` (`app.c:83-85`) fires only from `banner_set`, `ctl_volume`, `app_set_volume`, `app_toggle_output_mute`, `app_set_input_device`, `app_set_output_device` and `app_dismiss_banner`. It does **not** fire on connection state change, talker start/stop, or node join/leave, and the TG manager's `changed` callback is a literal no-op stub (`app.c:444`).

**Polling on a timer is mandatory, not an optimisation.** Two timers, exactly as PIOS does:

| Timer | Interval | Drives |
|---|---|---|
| `m_modelTick` | 100 ms | connection state, TG rows, active/recent lists, banner, volume, stats, relative-time labels, log serial, device-event drain |
| `m_meterTick` | 33 ms | VU meters only — a dedicated `LevelMeter` widget calling `update()` |

The macOS app's hardest-won lesson transfers verbatim: **never connect a high-rate source to a full relayout.** `MainView.swift:8-13` documents that observing the 30 Hz audio engine re-ran the whole body including a `ForEach` over 60 sessions. In Qt: the meter widgets own their own repaint; list updates go through `QAbstractItemModel::dataChanged(idx, idx, {roles})` with a narrow role list, never `beginResetModel()`; relative-time labels ("12s ago") are recomputed inside the delegate on the 100 ms tick, not by rebuilding rows.

**VU ballistics are required, not cosmetic** — and there is exactly **one** filter stage. `app_mic_level()`/`app_spk_level()` are a single buffer's peak: jumpy while live, and **frozen at the last value once the stream stops**. PIOS's `window.c:601-607` decay fixes that. Cascading the macOS "attack τ 10 ms / release τ 400 ms" on top of it, as the draft did, gives a meter that visibly trails speech and never falls — `× 0.75` per 33 ms tick is already τ ≈ 115 ms. The draft's snippet also used an undefined `x` and shared one accumulator between two meters.

```cpp
// SidebarPanel::onMeterTick() -- 33 ms. TWO independent accumulators, ONE filter.
const float micRaw = app_tx_active(m_app)     ? app_mic_level(m_app) : 0.0f;
const float spkRaw = app_output_muted(m_app)  ? 0.0f                 : app_spk_level(m_app);

m_micVu = (micRaw > m_micVu) ? micRaw : m_micVu * 0.75f;   // instant attack, 0.75 decay
m_spkVu = (spkRaw > m_spkVu) ? spkRaw : m_spkVu * 0.75f;
if (m_micVu < 0.001f) m_micVu = 0.0f;
if (m_spkVu < 0.001f) m_spkVu = 0.0f;

m_micMeter->setLevel(m_micVu);      // stores + update(); no maths, no second filter
m_spkMeter->setLevel(m_spkVu);
```

```cpp
// LevelMeter::paintEvent() -- the dB CURVE only. No smoothing here. Ever.
static float barFraction(float v)
{
    if (v <= 0.0f) return 0.0f;
    const float db = 20.0f * std::log10(v);          // macOS's curve
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);   // [-60, 0] dB -> 0..1
}
```

**Never put dB maths in an RT callback**, and never add a second smoother in the widget.

### 1.6 The one genuine cross-thread hazard: logging **[v1]**

`handshake_run()` runs on the connect **worker** thread (`client.c:142`, and `handshake.h:5-12` says so) and calls `log_info()`/`log_dbg()` at `handshake.c:52,72,107,139,172,183,208,211`. `log.c` has **no locking whatsoever** — `g_level`, `g_file`, `g_sink`, `g_sink_user` are bare file statics.

**Decision: install our own thread-safe sink with `log_set_sink()` before `app_new()`, and do NOT call `app_capture_log()`.**

This also fixes a core bug locally: `emit()` returns immediately after calling the sink (`log.c:82-85`), so `app_capture_log()` + `log_open_file()` means the file gets *nothing*, despite the comment at `app.c:505-506` claiming otherwise.

**The sink contract is not what it looks like.** `log.h:36` / `log.c:78-85`: the sink receives the **bare formatted message body** — no timestamp, no level tag, **no trailing newline**. The draft's `fputs(line, g_file)` would therefore produce one unbroken multi-megabyte line in `cfg.log_file` and in the journal. The sink must synthesise `emit()`'s own `"%H:%M:%S [%s] %s\n"` format itself.

```cpp
// src/core/logbridge.cpp -- called from the GUI thread AND from the connect
// worker. Nothing here touches a QObject or a QWidget.
struct LogLine { int level; QString text; };          // text = the COMPOSED line

static QMutex                  g_mx;
static QVector<LogLine>        g_ring;                 // capped at 2000
static QAtomicInteger<quint64> g_serial;
static FILE                   *g_file   = nullptr;     // g_mx guards the POINTER
static bool                    g_redact = true;

static void svxc_log_sink(int level, const char *body, void *)
{
    // `body` is the BARE message: no timestamp, no level, no '\n'. Synthesise
    // emit()'s format or the file becomes one endless line.
    const QString msg = g_redact ? LogBridge::redact(QString::fromUtf8(body))
                                 : QString::fromUtf8(body);

    char stamp[16];
    const time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);                  // thread-safe (not async-signal-safe; fine)
    strftime(stamp, sizeof stamp, "%H:%M:%S", &tmv);

    const QString    composed = QString::asprintf("%s [%s] ", stamp,
                                                  log_level_name(level)) + msg;
    const QByteArray out      = composed.toUtf8() + '\n';

    {
        QMutexLocker lk(&g_mx);
        g_ring.append({level, composed});
        if (g_ring.size() > 2000) g_ring.remove(0, g_ring.size() - 2000);
        // g_file is read INSIDE the lock: fwrite() is thread-safe on glibc, but
        // the pointer load races LogBridge::setFile() and the rotation path.
        if (g_file) {
            fwrite(out.constData(), 1, size_t(out.size()), g_file);
            fflush(g_file);
        }
    }
    g_serial.fetchAndAddRelaxed(1);
    fwrite(out.constData(), 1, size_t(out.size()), stderr);   // keep journald useful
}
```

The log pane polls `LogBridge::serial()` on the 100 ms tick and calls `LogBridge::snapshot()` when it changed. Zero queued signals, zero `QObject` from a foreign thread. Redaction (§8 Q14) happens **inside the sink**, so it covers the connect worker's lines too.

**Rotation must not fight a held `FILE*`.** `logrotate`'s default `create` renames the file; our held descriptor keeps writing to the unlinked inode and the "rotated" log grows to nothing. Two defences, both shipped:

- `debian/svxconnect-qt.logrotate` uses **`copytruncate`** (plus `missingok notifempty compress rotate 4 weekly`).
- `SIGHUP` is caught on the same signalfd as `SIGINT`/`SIGTERM` and reopens the file under `g_mx`, so a user who edits the snippet to `create` still works.
- The in-app size cap (default 8 MB, checked under `g_mx` every 256 lines) rotates by copy-and-truncate — `ftruncate(fileno(g_file), 0)` after copying — never by rename, for the same reason.

**LogBridge and upstream patch 6 must not double-write.** Q7's patch ("wire `log_open_file`, make `emit()` write through to the file") would, landed naively, put every line in `cfg.log_file` twice: once from `emit()` and once from our sink. **The contract is: `LogBridge` owns `cfg.log_file`; the GUI never calls `log_open_file()`; and the upstream patch performs its write-through only when `g_sink == NULL`.** That is the right upstream design regardless — a front end that installs a sink has taken responsibility for the output — and it makes the two changes independent.

**Everything else is main-thread.** `app.h:11-14` is explicit. The only blocking main-thread calls are `app_start()` (miniaudio init), `app_set_output_device()` (full audio-context teardown, 1–2 s), `svx_audio_list()` and `pki_cert_info()`. Wrap the device switch in `QGuiApplication::setOverrideCursor(Qt::WaitCursor)` and disable the combo; do **not** move it to a worker thread — the core is not thread-safe.

### 1.7 Upstream policy

Land fixes in `SVXConnect-CLI`, never fork. Carry them as a submodule pin plus, at most, a `patches/` dir consumed by CI until the pin moves. The prioritised list is §8 Q7. Two of them are v1 blockers, not opportunistic: **patch 1 (`tgm_push_monitor()` export)** gates M3's Talkgroups tab (§3.10), and **patch 2 (device-event polling)** gates the Audio tab's honesty about hot-swap (§3.15).

### 1.8 The locale rule, in one place

Because it silently breaks the wire protocol and nothing catches it:

1. `setlocale(LC_NUMERIC, "C")` **before** `QApplication`, and again **after** it (§1.4).
2. A runtime `snprintf("%.7f", 51.05)` comma check at startup, logged at ERROR — not a `Q_ASSERT`, which compiles out in the build users run.
3. Every coordinate written to the config or to JSON goes through `QString::number(v, 'f', 7)` or `snprintf("%.7f")`, never `QLocale::system()`.
4. Every coordinate *read* from a `QLineEdit` uses a `QDoubleValidator` with `setLocale(QLocale::c())`.
5. `QString::arg(int)` for talkgroup numbers — it does not group digits. Never `QLocale::toString(int)`, which renders TG 91000 as "91,000".

---

## 2. Repo layout

Files marked `[b1]` are Beyond v1 — created empty or absent in v1, listed here so the tree does not have to be re-argued at M6.

```
SVXConnect-Debian/
├── CMakeLists.txt                     Top level: options, svxcore lib, both app targets, install
├── LICENSE                            MIT, Copyright (c) 2026 Diëlectricum BV        (D-1)
├── THIRD-PARTY-NOTICES                Qt/LGPL + opus + OpenSSL + miniaudio + icons + SvxLink provenance
├── README.md                          Screenshot, install (two-wget bootstrap), roadmap, licence
├── .gitmodules                        third_party/svxconnect-cli -> Guru-RF/SVXConnect-CLI, branch main
├── .gitignore
├── .clang-format                      LLVM base, 4-space, 100 col — matches CLI house style
│
├── cmake/
│   ├── SvxCore.cmake                  Builds the 24 core .c files; excludes main.c and ui/ui.c
│   ├── SvxCliBinary.cmake             The ncurses `svxconnect` binary — main.c + ui/ui.c + svxcore
│   ├── LicenceGuard.cmake             SVX_GPL_ONLY_MODULES + svx_find_qt6() — the guard IS find_package
│   └── Version.cmake                  SVXCONNECT_VERSION from git describe, baked into About
│
├── licenses/
│   ├── LGPL-3.0-only.txt              Required by LGPLv3 §4b outside Debian
│   ├── GPL-3.0-only.txt               Also required by §4b — people ship only the LGPL and are wrong
│   ├── Apache-2.0.txt                 OpenSSL 3
│   ├── BSD-3-Clause.txt               libopus
│   ├── MIT-0.txt, Unlicense.txt       miniaudio is "Unlicense OR MIT-0"; ship both, elect MIT-0
│   └── Lucide-LICENSE.txt             Lucide's own file VERBATIM (ISC AND retained Feather MIT)
│
├── third_party/
│   └── svxconnect-cli/                git submodule — the C core. VENDORED into the .orig tarball (§6.2)
│
├── tools/
│   ├── shim_check.c                   C TU: upstream headers + shim -> prototype drift guard
│   ├── shim_layout_check.cpp          C++ TU: shim only  -> struct-layout drift guard
│   ├── svxcore_layout.inc             The shared static_asserts (§1.3)
│   ├── licence_guard.sh               objdump/ldd + AppDir scan; run as a POST_BUILD step and in CI
│   ├── qt_copyright_check.sh          Parses /usr/share/doc/libqt6*/copyright on the build container
│   ├── strings_check.py               Every tr() literal must exist in resources/strings/STRINGS.md
│   └── make-icons.sh                  Renders the app SVG into hicolor PNG sizes
│
├── data/
│   ├── SVXConnect.desktop     App id for the GlobalShortcuts portal; MUST match Registry.Register
│   ├── SVXConnect.metainfo.xml  AppStream; <project_license>MIT</project_license> (§5.4)
│   ├── SVXConnect.svg         Scalable app icon
│   ├── hicolor/{16..512}/apps/*.png   Rasterised icons
│   ├── polkit/guru.rf.SVXConnect.policy   ONE action: install-ptt-rule (§4.4)
│   ├── udev/70-svxconnect-ptt.rules.example   Template, /usr/share/… — NEVER a conffile in /etc
│   └── logrotate/svxconnect-qt        copytruncate (§1.6)
│
├── resources/
│   ├── icons.qrc                      Lucide SVGs, light+dark
│   └── strings/STRINGS.md             THE user-facing string inventory (§3.17) — M0.5
│
├── src/
│   ├── main.cpp                       locale ×2, SIGPIPE, SVX_WINDOW_ONLY, argv, ConfigStore, lock,
│   │                                  app_new, CoreLoop, window, deferred app_start, exec, teardown
│   ├── appargs.h/.cpp                 argv parsing, BEFORE the lock (§1.4)
│   │
│   ├── core/
│   │   ├── svxcore.h                  C++-CLEAN shim: ten clean headers + re-declared app_*/svx_audio_*
│   │   ├── coreloop.h/.cpp            §1.4 — per-fd notifier registry + QTimer, the whole poll contract
│   │   ├── logbridge.h/.cpp           §1.6 — thread-safe sink, prefix synthesis, ring + file + stderr
│   │   ├── runlock.h/.cpp             svx_lock_acquire/who/release; "gui-qt"; PIOS-aware raise path
│   │   ├── singleinstance.h/.cpp      QLocalServer under XDG_RUNTIME_DIR, TYPED messages   [v1: parse only]
│   │   └── devlist.h/.cpp             svx_audio_list/resolve -> QVector<AudioDevice>, presence check
│   │
│   ├── model/
│   │   ├── corestate.h/.cpp           Façade: the ONLY thing that calls app_*/rc_*/tgm_*. Emits Qt signals.
│   │   ├── talkgroupmodel.h/.cpp      QAbstractListModel over switchable+monitored, two sort orders
│   │   ├── activetalkermodel.h/.cpp   QAbstractListModel over tg_manager::active[]
│   │   ├── recenttalkermodel.h/.cpp   tgm recent[]  (+ persisted history at M8)
│   │   ├── sessionmodel.h/.cpp        [b1] WS-feed 24 h sessions, capped at 60
│   │   ├── nodemodel.h/.cpp           [b1] WS-feed nodes -> map pins
│   │   └── history.h/.cpp             [b1] ~/.local/share/SVXConnect/local-history.json
│   │
│   ├── net/
│   │   ├── reflectorfeed.h/.cpp       [b1] QWebSocket, 5 s probe, 15 s reconnect, 1 s domain debounce
│   │   ├── feedschema.h/.cpp          [b1] snapshot / node_upsert (partial merge!) / talk_start / talk_stop
│   │   ├── mapinfofetcher.h/.cpp      [b1] portal talkgroups.json + callsigns.json, JSON-validated
│   │   ├── qrzclient.h/.cpp           [b1] OPTIONAL, disabled — see §8 Q2
│   │   └── locationprovider.h/.cpp    [b1] Manual QTH primary; GeoClue2 optional; ipwho.is optional
│   │
│   ├── ptt/
│   │   ├── pttbackend.h               The abstract interface (§4.2)
│   │   ├── pttmanager.h/.cpp          Probe, ordering, watchdog, arbitration, forceUnkey()
│   │   ├── portalbackend.h/.cpp       GlobalShortcuts on a DEDICATED QDBusConnection (§4.3)
│   │   ├── x11backend.h/.cpp          xcb_grab_key + QAbstractNativeEventFilter, 4 modifier masks
│   │   ├── evdevbackend.h/.cpp        libevdev + QSocketNotifier, autorepeat + SYN_DROPPED, EVIOCGRAB
│   │   ├── fifobackend.h/.cpp         Documentation-only: the core's ctl_fifo already works
│   │   ├── chordcapture.h/.cpp        Single-chord key grabber (QKeySequenceEdit is unusable on 6.4)
│   │   └── keysymmap.h/.cpp           nativeVirtualKey -> xkb_keysym_get_name -> Shortcuts syntax
│   │
│   ├── settings/
│   │   ├── configstore.h/.cpp         OWNS the mutable svx_config; WRITES svxconnect.conf, comments kept
│   │   ├── guisettings.h/.cpp         QSettings for GUI-only keys (geometry, showMap, …)
│   │   └── pki.h/.cpp                 pki_build_path/file_exists/cert_info/check_pair + wipe-all
│   │
│   ├── ui/
│   │   ├── mainwindow.h/.cpp/.ui      Root shell, menus, window state, always-on-top probe, map toggle
│   │   ├── statusbar.h/.cpp           State dot, state text, RX/TX pkt/s, error, ID, nodes, QTH
│   │   ├── sidebar.h/.cpp             Active TG + lock, TG button list, meters, volume+mute
│   │   ├── talkgroupbutton.h/.cpp     Checkable button: stars, mute glyph, traffic dot, last-heard, menu
│   │   ├── levelmeter.h/.cpp          paintEvent + QLinearGradient(green,green,yellow,red), 8 px, r=2
│   │   ├── activitypanel.h/.cpp       v1: Local + Recent sections, headers, empty states
│   │   ├── talkerdelegate.h/.cpp      Row: icon, callsign, TG line, badges, time — QStyledItemDelegate
│   │   ├── sessiondelegate.h/.cpp     [b1] Feed row: 7 px dot, callsign, city, TG capsule, time-ago
│   │   ├── pttbutton.h/.cpp           Hold + latch (§3.5), explicit colours (never palette)
│   │   ├── mapplaceholder.h/.cpp      The splitter pane the map lands in at M7 — REAL in v1 (§3.1)
│   │   ├── diagnosticsbar.h/.cpp      Tick/Gap/Peak stall/Pins + Reset  (+ WS/s, Cam/s at M6/M7)
│   │   ├── logpane.h/.cpp             Polls LogBridge::serial(); QPlainTextEdit, level colouring
│   │   ├── timefmt.h/.cpp             THREE formatters (§3.4) — fmt_age() covers none of them
│   │   ├── trayicon.h/.cpp            [b1] QSystemTrayIcon + frameless 260 px popup
│   │   ├── aboutdialog.h/.cpp         Credits (D-1), version, build date, Qt attribution, licence tabs
│   │   ├── enrolwizard.h/.cpp         [b1] QProcess `svxconnect --enroll`, reads STDERR, cancel
│   │   ├── icons.h/.cpp               QRC-backed SF-Symbol replacements; QIcon::fromTheme as fallback
│   │   └── prefs/
│   │       ├── prefsdialog.h/.cpp     QTabWidget, tabs in QScrollAreas, 560x480
│   │       ├── connectiontab.*        Domain/port/callsign/email, cert mismatch guard, QTH, certs
│   │       ├── audiotab.*             Devices, mic AGC/target/gain, jitter_ms, beep, duck, trim, test tone
│   │       ├── talkgroupstab.*        Switchable/monitored, default_tg, lock_on_start, linger, idle, order
│   │       ├── ptttab.*               Backend table, chord capture, learn-mode, Test PTT, scripting docs
│   │       ├── generaltab.*           Close-to-tray, verbose log, always-on-top, autostart [b1]
│   │       └── maptab.*               [b1] Enhanced explainer, radius, portal auto-update, JSON editors
│   │
│   └── map/                           [b1] mapwidget, tilecache, nodeoverlay, camera, stackedpopup
│
├── packaging/
│   ├── debian/{control,rules,copyright,changelog,install,logrotate,…}   ALL THREE binaries
│   ├── appimage/{AppImageBuilder.yml,excludelist,compliance.sh}         §5.5 — not optional
│   └── apt-repo/{conf/distributions,preferences,publish.sh}             reprepro to GitHub Pages
│
├── tests/
│   ├── test_configstore.cpp           Round-trip svxconnect.conf, comments and quoting preserved
│   ├── test_timefmt.cpp               The three time formatters (§3.4)
│   ├── test_coreloop.cpp              fd-set churn: no duplicate notifier, no spin on a closed fd
│   ├── test_keysymmap.cpp             Qt key event -> "CTRL+SHIFT+t"
│   ├── test_feedschema.cpp            [b1] partial merge, qth.long, lenient coercion, '/'-synthesis
│   ├── test_clustering.cpp            [b1] grid clustering == the reference spiderfy layout
│   └── test_maidenhead.cpp            [b1] clamped at lat=±90, lon=±180 (the Swift version traps)
│
└── .github/workflows/
    ├── build.yml                      matrix: {bookworm, trixie, noble, resolute} x {amd64, arm64}
    ├── deb.yml                        vendored orig tarball + dpkg-buildpackage + reprepro publish
    ├── appimage.yml                   ubuntu-24.04 (glibc 2.39 floor) + compliance.sh
    ├── licence.yml                    licence_guard.sh + qt_copyright_check.sh + REUSE lint (own repo)
    └── keyexpiry.yml                  monthly re-sign; fails under 12 months of key life (§6.3)
```

### 2.1 `CMakeLists.txt` — the shape **[v1]**

Four defects in the draft are fixed here: `pkg_check_modules` was called before `find_package(PkgConfig)`, `APP_SOURCES` was never defined, `Positioning`/`Test` were allowlisted in prose but never found, and the licence guard was a second hand-maintained copy of the module list that a contributor could bypass simply by adding a `find_package` line.

```cmake
cmake_minimum_required(VERSION 3.22)
project(svxconnect-qt VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(SVX_WITH_FEED        "Enhanced-reflector WebSocket feed (M6, beyond v1)"  OFF)
option(SVX_WITH_MAP         "Map pane (M7, beyond v1; requires SVX_WITH_FEED)"   OFF)
option(SVX_WITH_POSITIONING "GeoClue2 via QtPositioning (M8, beyond v1)"         OFF)
option(SVX_BUILD_CLI        "Also build the ncurses svxconnect binary"           ON)

# MUST come before include(cmake/SvxCore.cmake): that file calls
# pkg_check_modules(), which does not exist until PkgConfig has been found.
find_package(PkgConfig REQUIRED)
find_package(Threads   REQUIRED)      # Threads::Threads, never a bare "pthread"
find_package(OpenSSL   REQUIRED)

# Dev override, exactly like PIOS's `make CLI_DIR=../SVXConnect-CLI`:
#   cmake -B build -DCLI_DIR=../SVXConnect-CLI
set(CLI_DIR "${CMAKE_SOURCE_DIR}/third_party/svxconnect-cli" CACHE PATH "SVXConnect-CLI core")
if(NOT EXISTS "${CLI_DIR}/src/app.c")
  message(FATAL_ERROR "SVXConnect-CLI core not found at ${CLI_DIR}\n"
                      "  run 'git submodule update --init'  or  -DCLI_DIR=../SVXConnect-CLI\n"
                      "  (in a Debian source package the tree is VENDORED, not a submodule)")
endif()

include(cmake/Version.cmake)
include(cmake/LicenceGuard.cmake)     # SVX_GPL_ONLY_MODULES + svx_find_qt6()
include(cmake/SvxCore.cmake)          # -> target svxcore
if(SVX_BUILD_CLI)
  include(cmake/SvxCliBinary.cmake)   # -> target svxconnect (ncurses)
endif()

# ONE list, ONE call. svx_find_qt6() IS the find_package wrapper, so there is no
# second copy for a contributor to bypass (§5.3).
set(SVX_QT_MODULES Core Gui Widgets Network DBus Svg)
if(SVX_WITH_FEED)        list(APPEND SVX_QT_MODULES WebSockets)  endif()
if(SVX_WITH_POSITIONING) list(APPEND SVX_QT_MODULES Positioning) endif()
if(BUILD_TESTING)        list(APPEND SVX_QT_MODULES Test)        endif()
svx_find_qt6(${SVX_QT_MODULES})

pkg_check_modules(XKB REQUIRED IMPORTED_TARGET xkbcommon)   # keysym names (§4.4)

# Explicit, not GLOB: a globbed source list silently drops a file added on
# another branch until someone re-runs cmake.
set(APP_SOURCES
    src/main.cpp
    src/appargs.cpp
    src/core/coreloop.cpp  src/core/logbridge.cpp  src/core/runlock.cpp
    src/core/singleinstance.cpp  src/core/devlist.cpp
    src/model/corestate.cpp src/model/talkgroupmodel.cpp
    src/model/activetalkermodel.cpp src/model/recenttalkermodel.cpp
    src/ptt/pttmanager.cpp src/ptt/portalbackend.cpp src/ptt/x11backend.cpp
    src/ptt/fifobackend.cpp src/ptt/chordcapture.cpp src/ptt/keysymmap.cpp
    src/settings/configstore.cpp src/settings/guisettings.cpp src/settings/pki.cpp
    src/ui/mainwindow.cpp src/ui/statusbar.cpp src/ui/sidebar.cpp
    src/ui/talkgroupbutton.cpp src/ui/levelmeter.cpp src/ui/activitypanel.cpp
    src/ui/talkerdelegate.cpp src/ui/pttbutton.cpp src/ui/mapplaceholder.cpp
    src/ui/diagnosticsbar.cpp src/ui/logpane.cpp src/ui/timefmt.cpp
    src/ui/aboutdialog.cpp src/ui/icons.cpp
    src/ui/prefs/prefsdialog.cpp src/ui/prefs/connectiontab.cpp
    src/ui/prefs/audiotab.cpp src/ui/prefs/talkgroupstab.cpp
    src/ui/prefs/ptttab.cpp src/ui/prefs/generaltab.cpp
    tools/shim_layout_check.cpp)          # the C++ half of the drift guard (§1.3)

if(SVX_WITH_FEED)
  list(APPEND APP_SOURCES src/net/reflectorfeed.cpp src/net/feedschema.cpp
                          src/net/mapinfofetcher.cpp src/model/sessionmodel.cpp
                          src/ui/sessiondelegate.cpp src/ui/prefs/maptab.cpp)
endif()

add_executable(svxconnect-qt ${APP_SOURCES} resources/icons.qrc)
target_link_libraries(svxconnect-qt PRIVATE
    svxcore Qt6::Widgets Qt6::Network Qt6::DBus Qt6::Svg
    PkgConfig::XKB Threads::Threads)
if(SVX_WITH_FEED)        target_link_libraries(svxconnect-qt PRIVATE Qt6::WebSockets)  endif()
if(SVX_WITH_POSITIONING) target_link_libraries(svxconnect-qt PRIVATE Qt6::Positioning) endif()

# Optional evdev PTT backend (Layer 2)
pkg_check_modules(EVDEV IMPORTED_TARGET libevdev)
if(EVDEV_FOUND)
  target_sources(svxconnect-qt PRIVATE src/ptt/evdevbackend.cpp)
  target_compile_definitions(svxconnect-qt PRIVATE SVX_HAVE_EVDEV)
  target_link_libraries(svxconnect-qt PRIVATE PkgConfig::EVDEV)
endif()

# The CMake allowlist can only see the names handed to svx_find_qt6(). THIS sees
# the real link graph, which is what the licence actually depends on (§5.3).
add_custom_command(TARGET svxconnect-qt POST_BUILD
    COMMAND "${CMAKE_SOURCE_DIR}/tools/licence_guard.sh" "$<TARGET_FILE:svxconnect-qt>"
    COMMENT "Checking the link graph for GPL-3.0-only Qt modules"
    VERBATIM)

include(GNUInstallDirs)
install(TARGETS svxconnect-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(SVX_BUILD_CLI)
  install(TARGETS svxconnect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES data/SVXConnect.desktop      DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/SVXConnect.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/polkit/guru.rf.SVXConnect.policy
              DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(PROGRAMS tools/install-ptt-rule
              DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/svxconnect-qt)
```

`cmake/SvxCore.cmake`:

```cmake
# Requires find_package(PkgConfig REQUIRED), find_package(OpenSSL REQUIRED) and
# find_package(Threads REQUIRED) to have run in the top-level list file already.
file(GLOB_RECURSE CORE_SRC "${CLI_DIR}/src/*.c")
list(FILTER CORE_SRC EXCLUDE REGEX "/src/main\\.c$")      # the only int main()
list(FILTER CORE_SRC EXCLUDE REGEX "/src/ui/ui\\.c$")     # the only <ncurses.h>
list(APPEND CORE_SRC "${CMAKE_SOURCE_DIR}/tools/shim_check.c")   # C half of the drift guard

add_library(svxcore STATIC ${CORE_SRC})
set_target_properties(svxcore PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)  # gnu11, NOT c11
target_include_directories(svxcore
    PUBLIC  "${CLI_DIR}/src" "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/tools"
    PRIVATE "${CLI_DIR}/third_party")                                    # miniaudio.h
target_compile_definitions(svxcore PUBLIC _GNU_SOURCE)                   # or strcasestr fails
target_compile_options(svxcore PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation)
set_source_files_properties("${CLI_DIR}/src/audio/dev_miniaudio.c" PROPERTIES COMPILE_OPTIONS
    "-Wno-unused-function;-Wno-unused-variable;-Wno-sign-compare;-Wno-unused-but-set-variable")

pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
# PkgConfig::OPUS only. PkgConfig::PkgConfig is not a usable link target here.
target_link_libraries(svxcore PUBLIC
    OpenSSL::SSL OpenSSL::Crypto PkgConfig::OPUS
    resolv m Threads::Threads ${CMAKE_DL_LIBS})
```

`cmake/SvxCliBinary.cmake` — this exists because `debian/rules` cannot be three lines while the CLI has its own hand-written `Makefile` (§6.2, §8 Q19):

```cmake
set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE    TRUE)
find_package(Curses REQUIRED)

add_executable(svxconnect "${CLI_DIR}/src/main.c" "${CLI_DIR}/src/ui/ui.c")
set_target_properties(svxconnect PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_include_directories(svxconnect PRIVATE "${CLI_DIR}/src" ${CURSES_INCLUDE_DIRS})
target_link_libraries(svxconnect PRIVATE svxcore ${CURSES_LIBRARIES})
```

*Verified on this machine:* all 24 core `.c` files compile clean with gcc 14.2.0 under those flags and link into a C++ binary against `-lssl -lcrypto -lresolv -lopus -lm -lpthread -ldl` — no ncurses, no GTK. `-lresolv` is required for `res_query`/`ns_initparse` in `common/net.c`. **No ALSA/PulseAudio `-dev` package is needed**: miniaudio `dlopen()`s `libasound.so.2` / `libpulse.so.0` at runtime (that is what `${CMAKE_DL_LIBS}` is for) — which is exactly why `dpkg-shlibdeps` cannot see them and you must write those `Depends:` by hand (§6.2).

---

## 3. Feature parity matrix

Effort: **T** trivial (<1 h) · **S** small (<1 d) · **M** medium (1–3 d) · **L** large (>3 d) · **R** needs a decision first.

**Scope reminder (D-2):** §3.7 (tray), §3.12 (Map tab) and §3.13 (Map pane) are out of v1 and are kept below as headings only; §3.4's Reflector rows and §3.14/§3.15's feed rows likewise. Their delivery lives in §7's M5–M8 rows. This document does not reproduce their macOS-parity tables — see D-2.

### 3.1 Window shell & chrome **[v1]**

| macOS feature | Qt implementation | Eff |
|---|---|---|
| Root `VStack(spacing:0)`: diag strip / status bar / [sidebar ǀ activity] / map | `QMainWindow` central `QWidget` + `QVBoxLayout(spacing 0)`; `QFrame(HLine/VLine)` dividers; `QHBoxLayout` body | S |
| Default 560×420, single non-tabbing window | `resize(560,420)`; one instance enforced by the run lock (§1.4) | T |
| Min size flips 560→320 with the sidebar | `setMinimumWidth()` in the Show-Sidebar slot | T |
| Animated window resize on map toggle (`+1` for handle, y-origin compensation) | `QPropertyAnimation` on `"geometry"`; **drop the y compensation** — Qt origin is top-left | S |
| Map pane gating (`showMap && callsign && enhancedReflector`) | `QWidget::setVisible()`; fixed height, not a stretch factor | T |
| Map resize handle 6 px + drag, clamp 120…700, persisted | `QSplitter` with a 6 px QSS handle; persist `saveState()` in QSettings; `setFixedHeight` on drag-end so the tile renderer does not re-project every frame | S |
| Debug body-timing instrumentation | `QElapsedTimer` in the 100 ms tick under `#ifdef QT_DEBUG` + `qCDebug(logUi)` | T |
| `MarqueeText` (dead code, zero call sites) | **Do not port** | — |

**What the v1 window looks like with no map.** The splitter, the `showMap` QSettings key, the View → Show Map action, the 6 px handle and the height persistence are all **real in v1**; the pane they reveal is `MapPlaceholder`, a `QWidget` painting the app icon at 40 % opacity above the caption *"The map needs the enhanced-reflector feed, which is not in this version."* Shipping a menu item that toggles nothing is worse than shipping an honest placeholder, and at M7 the map widget is swapped in with **no shell change at all** — which is precisely why the placeholder earns its place.

### 3.2 Connection status bar **[v1]**

| macOS feature | Qt implementation | Eff |
|---|---|---|
| 10×10 state dot, red/green/orange | `QLabel` 10×10, QSS `border-radius:5px`, colour via dynamic property + `style()->polish()` | T |
| State text, 13 exact strings | `rc_state_name()` gives 4, and they are **lowercase**: `"idle"`, `"connecting"`, `"connected"`, `"reconnecting"` (`client.c:70-78`). Note `RC_BACKOFF` renders as `"reconnecting"`, not `"backoff"`. Title-case them in the UI; §8 Q8 takes 4 + `rc_last_error()` detail | S |
| RX / TX pkt/s pills with tooltips and colour rules | `rc_get_stats()` on the 100 ms tick — `rx_pps`/`tx_pps` are already refreshed at 1 Hz by the core, and it also gives `loss_pct`, `rx_lost`, `rx_replayed`, `rx_auth_fail` the Swift app never had | T |
| "Waiting for sysop to sign certificate…" | Only reachable through the enrolment wizard (M9); the string stays in `STRINGS.md` for M9 | T |
| Last-error text, red, elided | `app_banner()` / `rc_last_error()`; `QFontMetrics::elidedText` | T |
| Location / grid label, `" · "` joined, "no location" fallback | `QLabel` from `cfg.location` + `maidenhead()` | T |
| `ID: n` and `n nodes` | `rc_client_id()`, `rc_node_count()` | T |
| Connect / Disconnect button + missing-email alert + cert probe | `app_toggle_connect()`; `QMessageBox` guard using `pki_file_exists()` | T |
| Status bar chrome (`controlBackgroundColor`) | QSS `background: palette(base)`, margins 12/6 — palette roles, never hex | T |
| **Certificate message received mid-session** (NEW) | The core's `handle_frame()` handles only Heartbeat / TalkerStart / TalkerStop / NodeJoined / NodeLeft / Error / ProtoVerDowngrade; `AUTH_CHALLENGE(10)`, `CSR_REQUEST(16)`, `MSG_CLIENT_CERT(18)` and `CA_INFO(19)` fall through to `default: log_dbg("ignoring …")`, so a sysop-driven mid-session renewal is invisible. **v1 minimum: a banner** — *"The reflector sent a certificate message. Reconnect to pick up a renewed certificate."* Upstream patch 9 adds the `on_cert_message` callback that makes it precise | S |

### 3.3 Control sidebar **[v1]**

| macOS feature | Qt implementation | Eff |
|---|---|---|
| Fixed 200 px column, spacing 12, padding 12 | `setFixedWidth(200)` + `QVBoxLayout` | T |
| Active TG display: `—` / `None` / `Monitoring` / `TG n`, title2 mono bold | `QLabel`, state function; `tgm_selected()` + `rc_get_state()` | T |
| Lock toggle with tooltips | `QToolButton(checkable)`, two SVG icons, `app_toggle_lock()` | T |
| **Lock state persistence** (NEW) | macOS persists `tgLocked` (`SettingsManager.swift:175-177`) and restores it. The core reads `cfg->lock_on_start` **once**, at `tgmanager.c:225`, and never writes it back. `ConfigStore` writes `lock_on_start` on every `app_toggle_lock()`, debounced 1 s. Verified at M2 by restarting the app | S |
| Active TG info sub-label from `tgInfoJSON` | `QHash<QString,QString>` parsed **once on settings change**, not per paint (the Swift re-parses every render) | T |
| Preemption notice | `tgm_preempt_banner()` — the core gives a 5 s expiring banner and the TG you came *from*; strictly better than macOS's non-expiring `preemptedBy` | T |
| "Talkgroups (right-click to mute)" hint | `QLabel` | T |
| TG button: title, bold when active, red strikethrough when muted, 0.55 opacity | `QPushButton(checkable)` + QSS `:checked`; `QFont::setStrikeOut` | S |
| Priority stars (one per `+`) | N small SVG stars from `config_tg_priority()` | T |
| Mute glyph + 8 px traffic dot with 2 px halo | SVG icon + QSS-styled dot from `tgm_talker_on()` | T |
| Last-heard sub-line (`12s` / `4m ago` / `2h ago`) | `TimeFmt::tgLastHeard()` (§3.4) over `tgm_last_heard()`. **Not `fmt_age()`** — it emits `12s\|4m\|2h\|3d\|--` and covers none of the three macOS formats | S |
| Right-click mute context menu | `Qt::CustomContextMenu` + `QMenu`; `app_toggle_mute(tg)` | T |
| Ordering: numeric / last-heard, nil→distantPast, id tiebreak | `std::stable_sort` in the model; `cfg.tg_order` (`numeric\|lastheard`) — note macOS persists `"lastHeard"` camelCase and the core compares case-insensitively; it works by luck. Ordering uses `tgm_last_heard()` in **both** modes (Appendix A / R2) | S |
| Audio meters (mic + speaker) with icons | `LevelMeter` widget, 33 ms tick, **single-stage** ballistics per §1.5 | M |
| Level meter rendering (rounded track, full-width gradient) | `paintEvent`: `QPainterPath::addRoundedRect(r,2,2)`, `QLinearGradient(0,0,w,0)` green→green→yellow→red spanning the **full track**, height 8 | S |
| Volume slider + 4-state speaker icon | `QSlider(0..100)` → `app_set_volume()`; `app_toggle_output_mute()` — **unify on the pre-mute-restore semantics; the sidebar's hard `0↔1` toggle is a macOS bug** | S |

### 3.4 Activity panel **[v1 — Local + Recent only]**

> **The Reflector section is not in v1.** It needs the WebSocket feed (D-2), so it arrives with **M6**. Its rows, the `24h data` badge, the 60-session cap and the 40 % dividers are macOS-recon detail that this document does not carry — re-derive them from `MainView.swift` when M6 is scheduled. With no feed there is also no other-node data at all, so nothing else can substitute for it.

| macOS feature | Qt implementation | Eff |
|---|---|---|
| Section gating: Local always; Reflector when enhanced+feed; Recent when !enhanced | **v1: Local always, Recent always.** The macOS gate hid Recent whenever "enhanced" was on, which in v1 would leave an empty panel. When the feed lands (M6) the gate becomes: Reflector when enhanced **and the feed is up**, otherwise Recent — closing the macOS gap where an enhanced-but-down feed showed nothing | S |
| Section header (11 pt semibold uppercase, kerning 0.5, capsule badge) | `QLabel` + `QFont::setLetterSpacing(AbsoluteSpacing, 0.5)`; badge QSS `border-radius:8px` | T |
| Local list, empty states ("Not connected" / "No active traffic") | `QListView` + `ActiveTalkerModel` + `QStackedWidget` for the placeholder | M |
| Recent list + badge (`local 48h` / `local 5d` / `local 8w`) | v1: `RecentTalkerModel` over the core's in-RAM `tgm recent[]` (16 entries, not persisted). The badge reads `local (session)` until M8 adds `history.json` and the retention preference | M |
| `TalkerRow`: icon, callsign+QRZ link, city/TG line, TG badge, priority badge, time | `QStyledItemDelegate::paint`, `Qt::ElideRight`, row padding 8/6, `border-radius:6`. **With QRZ dropped (§8 Q2) and no feed, line 2 is always `TG n`** — so v1 ships the row **single-line**: callsign, TG badge, priority badge, time. The delegate keeps a `secondLine` role that renders when non-empty, so M6/M8 restore the two-line layout without touching the model | M |
| Priority badge `+`/`++` orange on 10 % orange | Painted in the delegate from `config_tg_priority()` | T |
| Hover pointing-hand + click-to-select-TG + tooltip | `setMouseTracking`, `QStyle::State_MouseOver`, `clicked()` → `app_tg_select()` | T |
| `QRZNameLink` mailto | Not in v1 (§8 Q2) | — |
| Three time-label variants (duration / stopped / session-ago) | **`src/ui/timefmt.*` — three formatters, and `fmt_age()` is none of them** (see below) | S |
| Row hover highlight (green 0.18/0.08) + every tg>0 clickable | Delegate hover state | S |
| Empty-state component (icon 60 % opacity + caption) | Reusable `QWidget` in a `QStackedWidget` page | T |
| `visibleTalkers` (currently unfiltered) | Keep the mute filter in the model, so a muted TG's talker never appears | T |

**`TimeFmt` — three formatters, tested (`tests/test_timefmt.cpp`).** `fmt_age()` (`util.c:189-197`) emits `12s | 4m | 2h | 3d | --` and matches none of the macOS labels:

| Formatter | Used by | Output |
|---|---|---|
| `TimeFmt::talkerStopped(ms)` | Local rows, stopped talkers | `"12s ago"` / `"4m ago"` / `"2h ago"` |
| `TimeFmt::tgLastHeard(ms)` | TG button sub-line | `"12s"` under a minute, then `"4m ago"` / `"2h ago"` — the mixed form is deliberate on macOS; keep it |
| `TimeFmt::sessionAgo(startMs, endMs)` | [b1] Reflector rows | `"Now"` while active, `"—"` when `endMs` is absent, else `s/m/h/d` with **no** suffix |
| `TimeFmt::duration(ms)` | Live talker rows | `mm:ss` |

### 3.5 PTT **[v1]**

| macOS feature | Qt implementation | Eff |
|---|---|---|
| Big PTT button, mic icon, red when TX, **explicit** colours | `QPushButton` with QSS `border-radius:6px`, hard-coded red / accent, white text, 36 px SVG. Explicit colours matter for the same reason as on macOS: an unfocused window must not desaturate the "hot" cue | T |
| Both macOS PTT buttons `togglePTT()` | **Deliberate divergence, recorded here and in `STRINGS.md`:** the on-screen button is **hold *and* latch**. Press-and-hold keys and unkeys on release; a press shorter than 250 ms **latches** (keys and stays keyed) and the next click unkeys. That preserves the macOS mouse-only "tap to key, tap to unkey" workflow while giving the hold behaviour a radio operator expects, and it is one control with one behaviour rather than §3.5 and §3.7 disagreeing. The button's label switches to `TRANSMITTING — click to stop` while latched | S |
| Space / Escape | Space = **toggle**, Escape = unconditional off. PIOS's finding stands: key auto-repeat under Wayland makes true in-window keyboard hold unreliable | T |
| Guard chain: disconnected 3 beeps + reconnect; TG 0 3 beeps; busy 2 beeps | **The core already does all four** (`app.c:238-275`), including the receive-only case macOS lacks. `app_ptt()` returns `void` and fails silently, so the UI must read back `app_tx_active()` on the next tick and **reset the latch** so the button does not stick red | T |
| Beep cadence 1=roger 2=busy 3=no-link | Core `tx_beep()`: 125 ms tones, 62 ms gaps. **Match the core** — one vocabulary across CLI and GUI | T |
| Roger beep + min-duration + self-suppression | Core `roger_beep`/`roger_beep_min_sec`. `call_strip_ssid()` stops at **`-`** and does **not** handle `/` (`util.c:128-137`) — so `ON6URE/P` returning from the reflector's AI decoder does not match your own callsign and self-suppression fails. Upstream patch 5 adds `/` (Appendix B / B1) | T |
| TX timeout | Core `tx_timeout_sec` (default 120, range 0…3600). macOS has **none** — a safety win; surface it in Preferences and never default it to 0 | T |
| Global hotkey | **§4 — the whole point of this repo** | L |

### 3.6 Menus, window state, app lifecycle **[v1, except where marked]**

| macOS feature | Qt implementation | v1 | Eff |
|---|---|---|---|
| About panel (credits, links, version, build) | `QDialog`, rich-text `QLabel`, `setOpenExternalLinks(true)`. The exact credits block is fixed by D-1 and by `SVXConnectApp.swift:168-209`: *Developed by* **ON6URE** → on6ure.be, *Distributed by* **RF.Guru** → rf.guru, svxconnect.app, `© 2026 Diëlectricum BV` | yes | S |
| File → Show Certificates | `QDesktopServices::openUrl(QUrl::fromLocalFile(cfg.pki_dir))` | yes | T |
| File → Remove All Certificates… (+ both dialogs) | `QDir::removeRecursively()` + `mkpath()`; **share one implementation with the Preferences button** | yes | T |
| View → Show Toolbar / Sidebar / Map / Diagnostics | Checkable `QAction`s: `Ctrl+Alt+T`, `Ctrl+Shift+S`, `Ctrl+Shift+M`, `Ctrl+Shift+D` (⌃⌘ has no sane Linux analogue) | yes | T |
| View → Always on Top ⇧⌘T | `setWindowFlag(Qt::WindowStaysOnTopHint)` + `show()`. **Frequently ignored on Wayland** — probe once (compare the flag after a round trip), and grey the item with an honest tooltip if it did not take | yes | M |
| Help → Documentation | `QDesktopServices::openUrl("https://svxconnect.app")` | yes | T |
| Preferences ⌘, | `QKeySequence::Preferences` (`Ctrl+,`), non-modal `QDialog` | yes | S |
| Persisted UI toggles | `QSettings` (`~/.config/SVXConnect/SVXConnect.conf`), **same key names as macOS** | yes | S |
| Quit on last window closed | `setQuitOnLastWindowClosed(false)` + first-close notification + a General-tab preference (§8 Q6). In v1 there is no tray, so `false` means the window is the only way back — therefore **v1 ships `true`** and the preference appears with the tray at M5. State it in `STRINGS.md` | partly | T |
| Launch sequence (BuildExpiry, location, always-on-top re-apply) | `QTimer::singleShot(300, …)` for the deferred always-on-top. **Drop BuildExpiry entirely** | yes | T |
| Auto-connect 0.5 s after launch when a cert exists | `app_start()` already calls `rc_start()`; the GUI only provides the *gate*: if `pki_file_exists()` is false, skip the connect and show the enrolment prompt (v1: instructions to run `svxconnect --enroll`; M9: the wizard) | yes | S |
| Microphone permission check | **No Linux analogue** — `mic_perm.c:27` returns `SVX_MIC_GRANTED` unconditionally. The core's silent-mic detector (>20 TX frames with zero non-silence → banner) is the real substitute and is *better* than a permission dialog | yes | T |
| Single instance / raise | The run lock is v1 (D-3). The `QLocalServer`, the typed messages and the raise path are M5 — but the **client** half is v1, because a second launch must not hang: see §3.14 | partly | S |
| URL scheme `svxconnect://ptt/{on,off,toggle}` | `x-scheme-handler/svxconnect` in the `.desktop`, forwarding over `QLocalSocket`. **M5.** v1 parses the URL out of argv (D-3), logs it and reports *"URL handling arrives in a later version; use the control FIFO"* rather than dropping it | no | M |
| Focus-yield after URL activation | **Cannot be cloned.** Best effort: never call `raise()`/`activateWindow()` when handling a forwarded URL — which is why the socket message must be **typed** (raise ≠ url) | M5 | T |
| `setupApp()` wiring hub | Entirely internal to `app.c`. Nothing to port | — | — |

### 3.7 Menu-bar extra → system tray **[beyond v1 — M5]**

Deferred to **M5** (§7): `QSystemTrayIcon` plus the frameless 260 px popup, whose parity detail has to be re-derived from the macOS menu-bar extra when M5 is scheduled — it is not reproduced here. The one v1-relevant consequence: with no tray, `setQuitOnLastWindowClosed(true)` in v1 (§3.6), and §3.16 records what a Linux tray cannot clone at all.

### 3.8 Preferences — Connection tab **[v1]**

| macOS feature | Qt implementation | Eff |
|---|---|---|
| Tabbed dialog, fixed 560×480, each tab in a scroll view | `QTabWidget` + `QScrollArea(setWidgetResizable)`, `setFixedSize(560,480)`. Several tabs *are* taller than 480 — scrolling is the normal state. v1 tabs: Connection / Audio / Talkgroups / PTT / General | S |
| Section header style (`.headline` + spacing 18/8) | `QGroupBox` or bold `QLabel` + `QVBoxLayout` | T |
| Reflector domain: strip **all** whitespace on keystroke, commit on Enter/blur | `QLineEdit` + `textEdited` filter (`QSignalBlocker`, restore cursor); commit on `editingFinished` + `returnPressed`. **Do not commit per keystroke** — a commit reconnects | S |
| Port | `QSpinBox(1..65535, default 5300)` — better than the macOS free-text field | T |
| Callsign: uppercase + `[A-Z0-9-]` filter | `QRegularExpressionValidator` + `toUpper()` in `textEdited`. **Do not "fix" it to allow `/`** — `/`-suffixed callsigns only ever come from the reflector's AI decoder | T |
| Email (no validation at all on macOS) | `QLineEdit`, no validator — match the behaviour | T |
| Cert-mismatch guard + "Certificate no longer matches" alert | `QMessageBox` with Revert (RejectRole) / Remove certificate (DestructiveRole). **This was dead code in the draft:** the snapshot keys `certIssuedForCallsign` / `certIssuedForEmail` are written by macOS the moment the reflector returns the signed cert (`ReflectorClient.swift:1264-1265`), and nothing in the plan wrote them. Two writers, both required: (a) the enrol wizard writes both on success (M9), and (b) **on first run, seed them from `pki_cert_info()`'s CN plus the certificate's SAN email**, so a user who enrolled with the CLI before installing the GUI still gets the guard. (b) is v1 | S |
| Remove all certificates + confirm + explainer | Shared with the File menu action | T |
| QTH: manual toggle, city + Look up, lat/lon strings, help text | `QCheckBox`, `QLineEdit`, `QDoubleValidator` with **`setLocale(QLocale::c())`**. Geocoding and the picker sheet are M8 | S |
| 7-decimal POSIX persistence | `QString::number(v,'f',7)` — locale-independent (§1.8) | T |
| **QTH changes need a reconnect** (NEW) | `nodeinfo_build_json()` has exactly one caller in the entire tree — `handshake.c:192`. **`MsgNodeInfo` is sent once per connection and there is no `rc_send_node_info()`**, so a QTH edited while connected never reaches the reflector. v1 behaviour: on a lat/lon/location change, prompt *"Reconnect now so the reflector sees your new location?"* → `app_reconnect()`. Upstream patch 3 adds `rc_send_node_info()` **split out of `crypto_gen_tx_params()`** — calling the existing bundled path would re-key TX and reset the GCM counter mid-session, which is the exact macOS bug the recon warns about | S |
| UDP packet-rate toggle | `QCheckBox` + QSettings | T |
| Log reveal / copy path | `QDesktopServices::openUrl` on the *directory*; `QClipboard`. Ship it **always**, not behind a compile flag — a distro package wants a runtime log level, not a `SVXCONNECT_DEBUG_LOG` build | T |
| SRV explainer caption | `QLabel`; the core already does SRV via `res_query` | T |
| Multi-host fallback (`a:5300,b:5300` → synthetic SRV at priority 100+i) | **Dropped, not "normalised".** The core takes one host and one port; normalising in the UI deletes the failover macOS had. The Connection tab says so in one line, and it is §8 Q7 item 16 | T |

### 3.9 Preferences — Audio tab **[v1]**

The draft's Audio tab had no control for the entire microphone chain, which is the single most common support call in a voice client, and none for `jitter_ms`, which is the most useful knob on a lossy WAN path. Both are core-parsed, core-validated keys.

| Control | Core key | Range / default | Notes | Eff |
|---|---|---|---|---|
| Input / output device pickers keyed by stable id | `input_device`, `output_device` | `"default"` | `QComboBox` from `svx_audio_list()`; `svx_audio_resolve()` to preselect. The core's 4-step resolve (id → exact name → case-insensitive substring → default) beats macOS's UID-only match | M |
| **Absent-device reset** (NEW) | same | — | `svx_audio_resolve()` falls back to the default **silently** and leaves the stale id in the **shared** conf, so the combo shows a device that is not there *and `svxconnect` inherits the same stale value on its next launch*. Match macOS (`SVXConnectApp.swift:592-626`): if the saved id matches no enumerated device, log at WARN, show `<name> (not present)` greyed in the combo, and **write the key back to `"default"`** | S |
| Device change applies immediately | — | — | `app_set_input_device()` / `app_set_output_device()`. Output tears down the whole audio context (1–2 s) — wait cursor, disable the combo, and **never** call it from inside `app_service()` (§3.15) | S |
| "Currently playing to: X" at 1 Hz | — | — | **Not reachable from outside the core today.** `svx_dev_name()` exists (`dev.h:105`) but takes an `svx_dev *`, and `app.c` keeps `play_dev`/`cap_dev` private with no accessor in `app.h`. Needs upstream gap 17 first; until then the tab shows the *configured* device string, not the open one. On PipeWire the open-stream name is the *only* reliable indicator of a default-sink change (§3.15) | S |
| Test tone | — | — | `app_test_tone()` — already forces volume ≥50 % and opens the playback gate. **Free; PIOS never wired it up** | T |
| **Microphone AGC** (NEW) | `mic_agc` | on | `QCheckBox`. Slow attack/release with a soft tanh limiter, deliberately gentle so it does not pump (`codec.h:51-53`) | T |
| **AGC target** (NEW) | `mic_agc_target_pct` | 5…95, default 30 | `QSpinBox` + caption *"30 % is about −22 dBm0."* Disabled when AGC is off | T |
| **Mic gain** (NEW) | `mic_gain` | −20…+40 dB, default 0 | `QSpinBox(suffix " dB")`. FIXED boost applied **before** the AGC and after DC-blocking (`app.c:346-348`); the only boost at all when AGC is off. Caption: *"+6 = 2×, +12 = 4×, +20 = 10×."* | T |
| **Mic self-test** (NEW) | — | — | A `LevelMeter` fed by `app_mic_level()` plus a "Test microphone" toggle that opens capture without transmitting, so the two controls above have something to aim at. The output side already has the test tone | S |
| **Jitter buffer** (NEW) | `jitter_ms` | 40…300, default 80 | `QSpinBox(suffix " ms")` + caption *"Raise on a lossy or distant link; lower for snappier audio."* Also show `app_jitter_state()` (`idle`/`prefill`/`playing`/`off`) live | T |
| Roger beep toggle + min-seconds | `roger_beep`, `roger_beep_min_sec` | on; 0…60 | Core range, wider than macOS's 0…10 (§8 Q9) | T |
| Auto-duck toggle + quiet percent | `auto_duck`, `auto_duck_quiet_pct` | off; 0…50 | **Both are phantom keys — parsed, validated, then never read.** Either implement `codec_duck()` upstream (§8 Q7 item 12) or grey both controls with the caption *"not yet implemented in the audio engine"*. v1: grey them | M |
| Output delay stepper 0…500/50 | — | — | **No core counterpart**, and `jitter_ms` is not one. Omit until `auto_duck` exists; the Audio tab says so | — |
| Tail trim | `tail_trim_ms` | 0…1000 | Exists — **but `jitter_trim_tail()` → `svx_ring_discard()` drops the OLDEST samples, so the squelch tail still plays.** Ship the control, note the limitation, fix upstream with the hold-back FIFO (§8 Q7 item 8) | M |
| TX timeout | `tx_timeout_sec` | 0…3600, default 120 | §3.5. Warn in the caption that 0 disables the safety net | T |
| Reset audio settings + alert | — | — | One `ConfigStore::resetAudio()` emitting each key's change; must also reset volume and mute | S |
| Mono→stereo explainer | — | — | Unnecessary — the core opens playback as mono s16 and lets miniaudio upmix. Keep the caption anyway; users ask | T |

### 3.10 Preferences — Talkgroups tab **[v1]**

| Control | Core key | Notes | Eff |
|---|---|---|---|
| Switchable field, digits+commas | `switchable` | `QRegularExpressionValidator("[0-9,+ ]*")` → `config_set()`. §8 Q10: follow the core and honour `+` here too, and say so in the help text | T |
| Monitored field, digits+commas+`+` | `monitored` | Same validator | T |
| **Default talkgroup** (NEW) | `default_tg` | `QSpinBox(0…4294967295 clamped to int)` — 0 = start monitor-only. **Consumed only in `tgm_init` (`tgmanager.c:227`)**, so the label reads *"applies on next start"*. Warn inline when the value is in neither list, exactly as `config_validate` does | T |
| **Start locked** (NEW) | `lock_on_start` | `QCheckBox`. Also consumed only in `tgm_init` (`tgmanager.c:225`) — but §3.3 now writes it back on every lock toggle, so this control and the sidebar lock are the same state, and "applies on next start" stops being a lie | T |
| Linger | `linger_seconds` | `QSpinBox(10…300, default 30)` — identical range in the core | T |
| **Idle timeout** | `idle_seconds` | `QSpinBox(0…3600, default 60)`, 0 = disabled. Not in the macOS UI (hard-coded 60 s private constant) — a genuine improvement | T |
| Button order | `tg_order` | Two `QRadioButton`s → `numeric` \| `lastheard` | T |
| "Fill from portal" ×2 | — | Beyond v1 — needs `tgInfoJSON` from the portal fetcher (M6). The buttons are absent in v1, not greyed | — |
| Live priority preview list | — | Rebuild a small `QVBoxLayout` on `textChanged` from `tglist_parse()` | S |
| Local history retention 1…365 | — | Beyond v1 (M8) — the GUI-only `localHistoryDays` key has nothing to retain until history is persisted | — |

**The v1 blocker on this tab: a talkgroup added here is inaudible until the next TG switch.**

`push_monitor()` is called from exactly two places — `wire_select()` and `tgm_toggle_mute()` (`tgmanager.c:79,420`). So `config_set("monitored", …)` updates the local priority table and **the reflector is never told**: no `MsgTgMonitor` goes out, and the new talkgroup produces no audio until something else happens to switch TGs. macOS re-sends the monitor list on a 500 ms debounce whenever either list changes (`TalkGroupManager.swift:225-256`); the CLI has no live reload at all because a CLI edit means an app restart.

- **The fix, and a hard prerequisite of M3: upstream patch 1** — export `tgm_push_monitor(tg_manager *)` (today it is `static push_monitor()` at `tgmanager.c:68`) and add a list-apply entry point on `svx_app` that calls it; there is no `app_apply_tg_lists()` in the tree today, so the patch creates one. Ten lines, no behaviour change for the CLI.
- **The workaround, only if the patch has not landed:** after a 500 ms debounce on `switchable`/`monitored` edits, call `app_tg_select(tgm_selected())` to force the `wire_select()` path. This is a **live wire**: if `wire_select()` early-returns when the target TG is unchanged, the workaround is silently inert, and if it does not, it re-sends `MsgSelectTG` and can gate audio for 250 ms. Verify which at implementation time; if it early-returns, the only fallback is `app_tg_select(0)` then `app_tg_select(prev)`, with an audible gap. Ship the patch.

Acceptance for M3 is literal: **add a TG in Preferences and hear it without switching TGs.**

### 3.11 Preferences — PTT tab **[v1]**

Rewritten wholesale; see §4.4. The macOS "External PTT" tab exists only because App Store guideline 2.4.5 forbade a global hotkey. On Linux the tab becomes: backend selection with live status, chord capture, hardware learn-mode, Test PTT, and *below* that the scripting docs (FIFO commands, D-Bus, URL scheme) which were the macOS tab's entire content.

### 3.12 Preferences — Map tab **[beyond v1 — M7]**

Deferred to **M7** (§7), gated on the feed landing at M6. Its controls (home radius, portal auto-update, the `tgInfoJSON`/`callsignInfoJSON` editors) are listed in §3.17's `Map tab [b1]` string group and in Q5's QSettings key list; the full tab layout is macOS-recon detail this document does not carry.

### 3.13 Map pane **[beyond v1 — M7]**

Deferred to **M7** (§7). What v1 ships instead is `MapPlaceholder` (§3.1), so the splitter, the `showMap` key, the View action and the height persistence are all real from M2 and the widget is swapped in at M7 with no shell change. The tile-provider and attribution constraints that bound the design are in §5.6 and Q3/Q4; the pin/clustering/camera detail is macOS-recon detail this document does not carry.

### 3.14 Managers & services **[v1 rows; feed/map/QRZ rows deferred to M6–M8]**

| macOS feature | Qt implementation | v1 | Eff |
|---|---|---|---|
| `SettingsManager` — 36 UserDefaults keys with `didSet` persistence | Split: keys the core owns → `svxconnect.conf` via a **new `ConfigStore::save()`** (read-modify-tmp-rename preserving comments, exactly like PIOS `window.c:288-319`); GUI-only keys → `QSettings`. **The core has no `config_save`** (§8 Q7 item 15). `ConfigStore` owns the one mutable `svx_config` and never a copy (§1.4) | yes | M |
| `config_validate()` as the first-run gate | **Do not use it alone.** It fails when callsign, reflector or `pki_dir` is empty **or both TG lists are empty** — so a user with a perfectly good identity and no talkgroups would be dropped into a first-run wizard instead of the Talkgroups tab. Use `config_validate()` for the hard gate (identity + reflector), plus per-field checks that route to the right tab | yes | S |
| Migration: erase `wsFeedDisabled`, pin `enhancedReflectorEnabled`, erase QRZ plaintext creds | Greenfield — skip. Never reintroduce a plaintext password key | yes | T |
| Auto-unmute on cold start (`outputVolume<=0.001 → preMuteVolume`) | Port it. It is a real support-load reducer | yes | T |
| TG state machine (priority / linger / idle / lock / mute / preemption) | **Core provides, and is the corrected version** — deterministic tie-breaking, monitor-list resend on every path, stale-talker prune, self-beep suppression. Do not port the Swift | yes | — |
| Mute persistence (`mutedTGsJSON`) | Core keeps mutes in RAM only. Persist in QSettings and re-apply after `app_start()` via `app_toggle_mute()` | yes | S |
| Lock persistence (`tgLocked`) | `cfg.lock_on_start`, written back on every toggle (§3.3, §3.10) | yes | S |
| `lastActiveTalkGroup` ("land where you left off") | Not in the core (`default_tg` always wins). §8 Q15: follow the core; offer `restore_last_tg` as an explicit preference, off by default | yes | T |
| Maidenhead | `maidenhead()` in `common/util.c` — already correct, already clamped, already returns empty for (0,0). The Swift version **traps** on `lat<-90`/`lon<-180` | yes | T |
| `AppLogger` (one unrotated file, no levels, no redaction) | `LogBridge` + the core's 4 levels + `cfg.log_file`, **with rotation and redaction** (§1.6, §8 Q14). Neither existing codebase has either, and the Preferences pane invites users to attach the file to bug reports | yes | M |
| `Diagnostics` (100 ms stall detector, 1 Hz counters) | `QTimer(100)` + `QElapsedTimer` measuring its own lateness, thresholds 130/200/500 ms; counters as plain members; keep them running in all builds. v1 strip: **Tick / Gap / Peak stall / Pins / Reset**; `WS/s` arrives with the feed (M6) and **`Cam/s`** with the map camera (M7) — both are in `MainView.swift:1039-1087` and neither has anything to count in v1 | yes | S |
| `TimeClock` 1 Hz shared | Collapses into the 100 ms model tick | yes | T |
| `PermissionCoordinator` | No mic gate on a plain Debian desktop. Substitute: the core's silent-mic banner | yes | T |
| `BuildExpiry` 30-day self-destruct | **Drop entirely.** A package that refuses to launch after 30 days is a release-blocking bug for any distro | yes | — |
| `StatusBroadcaster` Darwin notifications | (a) the core's `status_file` — already written every service tick with a 1 Hz mtime heartbeat, and PIOS's panel widget already reads it; (b) [M5] an optional session-bus service `SVXConnect` with `PttChanged`/`ConnectionChanged`. **A tray applet or panel widget reading the status file is a MONITOR, not an owner: it must never take the lock** (`lock.h:12-13`) | (a) yes | S |
| Single-instance / run lock | See below — this is new, mandatory, and the draft's version collided with PIOS | yes | S |
| `HotkeyManager` (a PTT controller after the hotkey was stripped) | Replaced by `PttManager` (§4) | yes | L |
| `ConnectionState` 13 states | `rc_state` (4, lowercase) + detail string; §3.2, §8 Q8 | yes | S |
| `TalkerInfo` model | `tgm_talker` / `tgm_recent` + a Qt struct for the persisted history (M8) | yes | T |
| Device hot-swap / route change | §3.15 — an upstream patch plus a queued recovery slot, with a `login1 PrepareForSleep` handler for suspend/resume, which on a laptop kills the device far more often than unplugging does. **Recovery ships at M10; v1 detects and banners** | partly | M |
| Autostart / launch-at-login | `~/.config/autostart/SVXConnect.desktop` written by a General-tab checkbox (or the Background portal where available). Pairs with §8 Q6 — for a receive-always client, "close to tray" and "start at login" are the same feature. **M5**, with the tray | no | S |
| `MapInfoFetcher`, `LocationProvider`, QRZ enrichment, feed→talker bridge | M6 (fetcher, bridge — see Q17) · M8 (`LocationProvider`) · QRZ dropped, Q2 | no | — |

**Single instance and the run lock — corrected.** The draft called `app_set_owner_kind(a, "gui")` and raised the incumbent over `QLocalSocket`. Both halves are wrong: **PIOS already takes the lock with kind `"gui"`** (`SVXConnect-PIOS/src/gui/main.c:211`) and raises with `kill(pid, SIGUSR1)` (`main.c:219`), running no `QLocalServer` at all. On a Pi with both installed — which §8 Q1 explicitly contemplates — the second launch would connect to nothing, block for the socket timeout, and then print the wrong error.

```
argv parsed FIRST (D-3), then:

svx_lock_acquire(cfg.lock_file, "gui-qt")        // 6 chars; upstream buffer is char[16]
  0  -> we own the radio. Start QLocalServer:
          QLocalServer::removeServer(name);      // a crash leaves a stale socket
          name = $XDG_RUNTIME_DIR/svxconnect-qt.sock   (never /tmp)
          listen(name);                          // 0600 by construction under XDG_RUNTIME_DIR
 -2  -> "could not create the lock file <path>"  (a real error: permissions, missing dir)
 -1  -> svx_lock_who(path, kind, 16, &pid):      // exactly 16, matching app.c:58
          kind == "gui-qt"  -> our own kind. QLocalSocket, 300 ms timeout:
                                 connected -> send ONE typed JSON line, exit 0
                                 timeout   -> "SVXConnect is already running (pid N) but is
                                              not responding." + [Quit it] / [Cancel]
          kind == "gui"     -> PIOS. kill(pid, SIGUSR1) to raise ITS window, then an
                               informational dialog: PIOS owns the radio.
          kind == "cli" |
                 "headless" -> "The terminal client (pid N) already holds the connection."
```

The socket protocol is **typed**, because "raise the window" and "handle this URL" are different actions and the URL path must not steal focus (§3.6):

```json
{"cmd":"raise"}
{"cmd":"url","url":"svxconnect://ptt/toggle"}
```

In v1 the server half is not built; the *client* half is, because a second launch must produce the right message rather than a hang.

### 3.15 Protocol & audio — all core-provided **[v1]**

Every item below is **already implemented, in C, correctly, and better than the Swift**. The Qt work is zero unless noted.

`SRV resolution` · `MsgNodeInfo JSON` (identical shape; change the `name` literal, T) · `TCP message set` · `MsgAuthResponse` 20 zero bytes · `UDP TX/RX crypto` incl. the first-packet AAD asymmetry · **replay protection** · **loss inference** · `MsgAllSamplesFlushed` handling · UDP socket via `sendto` · heartbeats 5 s / 10 s · `MsgSelectTG` always followed by `MsgTgMonitor` · sorted monitor set · `MsgTalkerStop` matched on callsign only · TLS 1.2 pin, no peer verify, no `close_notify` · reconnect backoff `{3,3,5,10,20,30,60}` · TG re-assert after reconnect · packet counters and `rc_stats` · 16 kHz / 320-sample / 20 ms framing · Opus encoder (complexity 10, FEC, 5 % loss, DTX off) · **PLC** · **jitter buffer + playback gate** · **mic AGC / DC block / dB gain** · **per-transmission codec reset** · device enumeration and 4-step resolve · runtime device switch · lazy mic open · TX pacing by the device clock · TX timeout · PTT guard chain · `MsgUdpFlushSamples` on key-up · control FIFO · status file · run lock · roger/busy/no-link beeps · test tone · volume with pre-mute restore · silent-microphone detection.

Gaps in the core to close (upstream patches, §8 Q7):

| # | Gap | Fix | Eff |
|---|---|---|---|
| 1 | `push_monitor()` is unreachable from a settings edit, so a TG added in Preferences is **inaudible** (§3.10) | Export `tgm_push_monitor()`; call it from the list-apply path | S |
| 2 | `svx_dev_poll_event()` is implemented but **never polled** by `app.c` (only `audiotest.c` calls it), and `play_dev`/`cap_dev` are private, so the GUI cannot fix it from outside | Poll **both** devices in `app_service()`, but only **enqueue** the event — see the design below | M |
| 3 | `MsgNodeInfo` is sent exactly once, at `handshake.c:192`; there is no `rc_send_node_info()` (§3.8) | Add one, **split out of `crypto_gen_tx_params()`** so it does not re-key TX mid-session | S |
| 4 | `jitter_flush()` on the connected→disconnected edge (`app.c:106`) throws away the tail of the over | Call `jitter_end_of_stream()` there instead — it already exists (`jitter.c:136`) and is already called from three other sites (Appendix B / B2) | T |
| 5 | `call_strip_ssid()` stops at `-` and does **not** handle `/` (`util.c:128-137`) | Add `/`. Without it, roger-beep self-suppression fails against the AI decoder's `ON6URE/P` | T |
| 6 | `log_open_file()` is implemented but called from nowhere; `cfg.log_file` is a dead key | Wire it, and make the write-through happen **only when no sink is installed** (§1.6) | T |
| 7 | Opus decode buffer is 1280 samples (80 ms); a 120 ms packet is rejected | Widen to `SVX_FRAME*6` = 1920 = **exactly** Opus's maximum frame duration (Appendix A / R1) | T |
| 8 | Tail trim discards the **oldest** samples, so the squelch tail still plays | Hold-back FIFO in the jitter buffer | M |
| 9 | Frame types 10/16/18/19 fall through to `default: log_dbg("ignoring")` — mid-session cert renewal is invisible (§3.2) | Add an `on_cert_message` callback to `rc_callbacks` | S |
| 10 | `proto_parse_server_info()` discards node callsigns, keeps only a count | Emit the strings; add an `rc_callbacks` entry | S |
| 11 | No 250 ms network-boundary audio gate after a TG switch | ~10 lines in `hl_audio()` | S |
| 12 | `auto_duck` / `auto_duck_quiet_pct` parsed and ignored | Implement `codec_duck()` (port the Swift VAD: on >0.01, off <0.001, α 0.6/0.08, per-sample interpolation across the frame boundary) | M |
| 13 | `rc_callbacks` set once inside `app_new()`, no second observer | Add an observer list | S |
| 14 | Public headers are not C++-clean | `extern "C"`, an `SVX_ATOMIC` macro, tagged typedefs. Deletes §1.3's whole shim | M |
| 15 | No `config_save` | The GUI ships its own `ConfigStore::save()` meanwhile | M |
| 16 | No `reflector_hosts` failover list (§3.8) | Low priority | S |
| 17 | `play_dev`/`cap_dev` are private and `app.h` exposes no accessor, so a front end cannot name the device actually open — "Currently playing to: X" (§3.9) is unbuildable | Add `const char *app_output_device_name(const svx_app *)` and its capture twin, wrapping `svx_dev_name()` | T |

**Device hot-swap: the patch as drafted would thrash, and must not run inside `app_service()`.**

The draft said *"on `STOPPED`/`REROUTED`/`LOST` re-run `app_set_output_device(cfg->output_device)`"*. If the user pinned a device by id and **unplugged** it, re-opening that id fails, `svx_audio_resolve()` falls through to the system default, that device promptly reports another event, and the cycle repeats — each iteration tearing down the entire miniaudio context for 1–2 s (`app.c:771-784`) **inside the GUI-thread service frame, while the capture ring overruns**. Required shape:

1. **`app_service()` only enqueues.** Poll `svx_dev_poll_event()` for **both** `play_dev` and `cap_dev` into a tiny ring, and `notify()`. Add `int app_take_device_event(svx_app *, int *which, int *event)` so the GUI can drain it. `app_service()` itself never re-opens anything.
2. **The GUI drains the queue on the 100 ms tick and recovers from a queued slot** — never from inside `app_service()`, and never from a notifier callback.
3. **Debounce**: coalesce events, act at most once per 2 s.
4. **Backoff**: 3 attempts at 2 s / 4 s / 8 s, then stop and `banner_set()`.
5. **A lost *pinned* device gets a banner, not a silent rewrite**: *"The audio device you selected (<name>) is no longer available. Falling back to the system default."* Fall back to `""`/`"default"` for the session and let the user decide whether to persist it (§3.9's absent-device reset does persist it, deliberately, because that path runs at startup and the stale id would otherwise poison the CLI too).
6. **Capture side stops TX first.** `tx_start()` opens the capture device lazily (`app.c:406-412`); closing it under an active transmission is a use-after-free of the RT callback's ring.
7. **PipeWire honesty:** a "default sink changed" on PipeWire often produces **no miniaudio notification at all** — the PulseAudio compat layer keeps the stream on the old sink. Add a `login1 PrepareForSleep` handler for suspend/resume, and accept that the open stream's own name ("Currently playing to: X") is the only reliable indicator — which is exactly why gap 17 has to land before that line can be honest. **Do not promise hot-swap parity with macOS** anywhere in the UI or the README.

v1 ships steps 1–2 as *detection only*: the banner appears and the log line is written. The recovery machinery is M10.

### 3.16 What CANNOT be cloned, and the Linux substitute

| macOS feature | Why not | Linux substitute |
|---|---|---|
| Menu-bar item showing the **active callsign as text** | `QSystemTrayIcon` has no text label on most Linux DEs; StatusNotifierItem titles are inconsistently rendered | Tooltip + callsign in the popup header (§3.7, M5) |
| Focus-yield after URL activation (`NSApp.deactivate()`) | No reliable equivalent under Wayland; on X11 it needs `XSetInputFocus` games | Never `raise()`/`activateWindow()` when handling a forwarded URL — hence the typed socket message (§3.14) |
| Always-on-top guaranteed | `WindowStaysOnTopHint` is frequently ignored by Wayland compositors | Ship the toggle, probe whether it took, grey it with an honest tooltip |
| QRZ.app bridge (`be.moreorless.qrz`, `qrz://x-callback-url`) | No such app on Linux, and the referenced `../QRZ/INTEGRATION.md` is **not present in any repo**, so the wire contract is unavailable | Drop for v1 (§8 Q2). Consequence: the Local row has no city, which is why §3.4 makes it single-line |
| Free dark map tiles via `NSAppearance(.darkAqua)` | No OSM equivalent | A configurable tile URL template with a user-supplied key — and this is a **compliance** requirement, not a nicety (§5.6) |
| CoreLocation + `CLGeocoder` reverse geocode | GeoClue2 on a desktop needs an agent and is frequently absent; Nominatim has a 1 req/s policy | Manual QTH as the primary path (which is what the CLI does), GeoClue optional and off (M8) |
| Microphone TCC permission flow + System Settings deep link | No permission gate outside a sandbox; no universal "open sound settings" URL | The core's silent-mic banner; plain instructional text |
| `SecPKCS12Import` + keychain purge | Not needed — OpenSSL loads PEM key + cert directly | Delete |
| `BuildExpiry` 30-day self-destruct | User-hostile in a distro package; fires before any window exists | Build date in About and in the log header |
| Darwin notifications (`notifyutil -w`) | No notify(3) | Core `status_file` (already written) + optional D-Bus signals (M5) |
| SF Symbols | Not licensable outside Apple platforms | Bundle Lucide (ISC AND MIT) in a QRC; `QIcon::fromTheme` only as fallback |
| Animated `NSWindow` frame with bottom-left origin | Qt origin is top-left | Animate height only; **drop the y compensation** or the window jumps |
| `MarqueeText` | Dead code, zero call sites | Do not port |
| Sandbox path layout (`~/Library/…`) | — | `QStandardPaths::AppDataLocation` (`~/.local/share/SVXConnect/`), config at `~/.config/svxconnect/`, state at `~/.local/state/svxconnect/`, **and `pki_dir` from `cfg`, never re-derived** |

### 3.17 User-facing string inventory — `resources/strings/STRINGS.md` **[v1, milestone M0.5]**

The draft wrote "explainer", "help text" and "caption" generically and then promised i18n scaffolding with no source strings. **Those captions are the application's documentation.** The recon carries dozens of them verbatim; if they are not written down before the tabs are, they will be reinvented badly and the two clients will describe the same config key in two different ways.

`resources/strings/STRINGS.md` is a table: **key · English text · source (file:line, or NEW) · notes**. It is written at **M0.5, before any tab is coded**, and it is the input to the `.ts` file at M10. Every `tr()` literal in the tree must appear in it; `tools/strings_check.py` enforces that in CI.

Groups and a sample of the exact strings it must carry:

| Group | Examples (verbatim from the recon) |
|---|---|
| Connection tab | `Reflector (SRV domain or host:port)` · `Email (for certificate)` · `Latitude (e.g. 51.1234567)` · `Longitude (e.g. 2.7654321)` · `Set location manually` · `Show UDP audio packet rate in status bar` · `Remove all certificates` · `Remove all certificates?` · `Certificate no longer matches` · `Remove certificate` · `Use this location` · `Drag the map so the pin marks your location` · `Couldn't find that location. Try a more specific address.` |
| Audio tab | `Currently playing to: X` · `Play test tone` · `Play roger beep` · `Only after transmissions of at least N second(s)` · `Lower volume until speech is detected` · `Quiet level: N% of master` · `No delay (real-time)` · `Reset audio settings to defaults` · `Reset audio settings?` · `Incoming mono audio from the reflector is automatically converted to stereo for playback.` · `System Default` |
| Talkgroups tab | `Talkgroups (comma-separated)` · `Use + for priority. Example: 8++,1745+,8000` · `Monitor TGs with priority` · `Sort switchable TGs by` · `Last heard first` · `Fill from portal` · `Fill from portal (no priorities)` · `Keep talker history: N day(s)` |
| Sidebar / activity | `Talkgroups (right-click to mute)` · `Lock: stay on this TG (activity panel still shows other TGs)` · `Unlock: allow auto-switching to higher-priority TGs` · `Unmute TG <id>` · `Click to listen to TG <n>` · `Not connected` · `No active traffic` · `No recent activity` · `No talkgroups configured` · `Preempted by TG ` · `Priority: <+ or ++>` |
| Status / connection | `Waiting for sysop to sign certificate...` · `Email required` · the four core states, title-cased |
| Menus / About | `Show Certificates` · `Show Diagnostics` · `Always on Top` · `Show SVXConnect` · `SVXConnect Documentation` · `About SVXConnect` · `Developed by ` · `Distributed by ` |
| Logging | `Send this file when reporting a bug` · `Copy log path` · `Logging is DISABLED` · `Reveal log in Finder` → **NEW Linux text**: `Open the folder containing the log` |
| Map tab [b1] | `Home radius: N km` · `Update from portal automatically` · `Last updated: ` · `Never updated` · `Recenter on QTH at the configured radius` · `HTTP <code> for <file>` · `Empty/undecodable body for <file>` |
| Explicitly NEW (Linux only), needing review | `Auto-detect via GeoClue, falling back to IP geolocation.` (replaces the CoreLocation wording) · the PTT-tab backend table and its four status sentences (§4.4) · the no-release notice (§4.5) · `The map needs the enhanced-reflector feed, which is not in this version.` · `The desktop client is running; quit it or use its window.` · the device-lost banner (§3.15) · the QTH reconnect prompt (§3.8) |
| Dropped, with a reason recorded | `Microphone access is required` / `Open System Settings` (no TCC on Linux) · `This diagnostic build has expired` (BuildExpiry dropped) |

Divergences from macOS that must be *stated in the UI*, not just in this plan, are also recorded here: the PTT button's hold+latch (§3.5), lock persistence, `lastHeard` ordering (Appendix A / R2), and the switchable-priority behaviour (§8 Q10).

### 3.18 Preferences — General tab **[v1, new]**

Not a macOS tab. It exists because Linux has settings macOS does not.

| Control | Backing | v1 | Notes |
|---|---|---|---|
| Close button minimises to tray | `QSettings quitOnLastWindowClosed` | M5 | §8 Q6. Absent in v1 (no tray); the key is defined so M5 does not migrate anything |
| Start SVXConnect at login | `~/.config/autostart/*.desktop` | M5 | Pairs with the above |
| Verbose diagnostic log (this session only) | runtime | yes | Disables redaction for one session (§8 Q14); resets on restart, never persisted |
| Log level | `cfg.log_level` | yes | `QComboBox` err/warn/info/**debug** → `log_set_level()` live. The four strings are the `log_level` enum's own (`config.c:79`); `log_level_from_name()` also accepts `dbg`, but `config_set()` does **not**, so writing `dbg` back to the conf is rejected. Guard `log_level_from_name()`'s `-1` |
| Log file path + Open folder + Copy path | `cfg.log_file` | yes | The rotation policy is stated inline |
| Always on top | `QSettings alwaysOnTop` | yes | Mirrors the View menu; shows the probe result when the compositor ignored it |
| Release the run lock when disconnected | `QSettings releaseLockWhenDisconnected` | yes, off | §8 Q18 / Appendix A / R4. Off by default, with the warning that Connect can then fail because the CLI took the lock |

---

## 4. Global PTT hotkey **[v1 — M4, the whole point of this repo]**

This is the feature the macOS app could not ship (App Store guideline 2.4.5 flagged `NSEvent.addGlobalMonitorForEvents`). On Linux the restriction does not exist, and `pttKeyCode`/`pttModifierFlags` are already dead keys in the Swift settings waiting for a home.

### 4.0 What shipped, and what was removed

The four-layer design below was implemented and then **cut back to two**, after
M4 was built and tested against a live desktop. What ships is the **portal**
backend plus the **control FIFO**; the evdev and X11 backends are not in the
binary. The rest of §4 is kept because its findings are real and were expensive
to establish, but read it as history where it describes evdev.

**evdev was written, worked, and was deleted.** It was the only layer that could
bind a chord using CapsLock: at the evdev layer CapsLock is just KEY_CAPSLOCK
going down and up, rather than a lock state the shortcut layer discards. The
cost was read access to every key the device produces — for a keyboard, every
keystroke the user types — which is the wrong trade to make inside a radio
client.

It was also the wrong LAYER. The desktop already solves it: the xkb option
`caps:hyper` ("Make Caps Lock an additional Hyper", under KDE's
Keyboard → Key Bindings) turns CapsLock into a real modifier. Hyper and Super
share Mod4, verified with `xmodmap -pm`, so CapsLock+Enter is then simply
`LOGO+Return` — an ordinary portal binding with no elevated permission, no udev
rule and no polkit helper, which additionally makes CapsLock useful as a
modifier in every other application. Confirmed working end to end by the repo
owner.

Consequences elsewhere in this document: the `libevdev-dev` build dependency,
the `SVX_WITH_EVDEV` option, `data/udev/`, `data/polkit/`, the learn-mode UI and
the `install-ptt-rule` helper are all unnecessary and are not built. §6.2's
`Build-Depends` drops `libevdev-dev`.

X11's `xcb_grab_key` was never implemented: the portal covers X11 sessions too,
and the `DISPLAY`-is-set-on-Wayland trap documented in §4.3 made it a liability
for no gain.

### 4.1 The layered design — four backends, one interface

| Layer | Backend | Gives release? | Works when unfocused | Setup cost |
|---|---|---|---|---|
| 1 (default, keyboard) | **XDG `org.freedesktop.portal.GlobalShortcuts`** | **Yes** (`Activated` + `Deactivated`) | Yes, on Wayland and X11 | None, but requires an installed `.desktop` |
| 2 (hardware) | **evdev via libevdev** | **Yes** (`EV_KEY` 1/0) | Yes, always, even at a TTY | One scoped udev rule |
| 3 (X11 only) | **`xcb_grab_key`** | Yes, but debounced | Yes on real X11 | None |
| 4 (documented) | **Control FIFO** (already works) | Yes, if the compositor sends both edges | Yes | User edits compositor config |

Verified live on this class of machine (Debian 13, Plasma 6.3.6, Wayland, xdg-desktop-portal 1.20.3 + `-kde` 6.3.5): `Registry.Register` → `CreateSession` → `BindShortcuts` all returned `code=0`, `trigger_description "Ctrl+Shift+T"`, no permission dialog, persisted to `~/.config/kglobalshortcutsrc` as `[<app_id>] ptt=Ctrl+Shift+T,Ctrl+Shift+T,Push To Talk`. Both `Activated` and `Deactivated` are present in the interface **and** in the `-kde` binary's strings.

**Layer 2 is not a downgrade.** For a foot switch it is strictly better: the portal has no concept of a non-keyboard trigger, and Mumble's portal PR hit exactly "the portal doesn't support mouse button bindings" and "cannot distinguish between multiple devices of the same type."

**Layer 4 is the honest answer for wlroots** (sway, labwc, niri): `xdg-desktop-portal-wlr` ships **no** GlobalShortcuts backend and upstream says adding one "would require a fair bit of development in both". Do not fight it — print a ready-to-paste config block.

### 4.2 The backend interface

```cpp
// src/ptt/pttbackend.h
struct PttBinding {
    enum Kind { Keyboard, Device } kind = Keyboard;
    QString trigger;        // Keyboard: freedesktop Shortcuts spec, e.g. "CTRL+SHIFT+t"
    QString devicePath;     // Device: /dev/input/by-id/... (prefer by-id over by-path)
    int     code = 0;       // Device: KEY_*/BTN_*
    bool    grab = false;   // Device: EVIOCGRAB -- dedicated pedals only, NEVER a keyboard
};

struct PttAvailability {
    enum State { Available, NeedsSetup, Unavailable } state = Unavailable;
    QString reason;         // shown verbatim in the settings table
    QString instructions;   // paste-able fix (udev rule, compositor snippet)
    bool    hasRelease = false;   // false => toggle-only; the UI must say so loudly
};

class PttBackend : public QObject {
    Q_OBJECT
public:
    virtual QString id() const = 0;              // "portal" | "evdev" | "x11" | "fifo"
    virtual QString displayName() const = 0;

    /// Cheap, cached, NON-BLOCKING. Returns the last known result immediately;
    /// a backend whose probe needs I/O (the portal needs a D-Bus round trip)
    /// refreshes asynchronously and emits availabilityChanged(). A blocking
    /// probe on the settings tab's refresh timer would freeze the dialog for
    /// QDBusInterface's default 25 s against a hung portal -- and because
    /// app_service() runs on this same thread, it would stall audio with it.
    virtual PttAvailability availability() const = 0;
    virtual void    refresh() = 0;               // kick an async re-probe

    virtual bool    start(const PttBinding &) = 0;
    virtual void    stop() = 0;
    virtual bool    canCapture() const { return false; }   // learn-mode / key grabber
signals:
    void pressed();
    void released();
    void lost(const QString &why);   // portal session closed, device unplugged, grab stolen
    void availabilityChanged();
    void triggerDescriptionChanged(const QString &human);  // the portal may not honour our request
};
```

`PttManager` owns the list, runs the probes, starts exactly one keyboard backend (plus an evdev backend if a hardware device is also configured), and turns `pressed`/`released` into `app_ptt(a, CTL_ON/CTL_OFF)`.

### 4.3 Runtime detection — the exact order

```
1. If a hardware binding is configured and its device is readable
       -> start EvdevBackend for it.  (independent of the keyboard path)

2. Keyboard path:
   a. probe portal: read the `version` property of
        org.freedesktop.portal.GlobalShortcuts on
        org.freedesktop.portal.Desktop @ /org/freedesktop/portal/desktop
      -- asynchronously, with an explicit 3 s timeout. Present => Available.
   b. if XDG_SESSION_TYPE == "x11" AND QGuiApplication::platformName() == "xcb"
         -> X11Backend also Available.
   c. choose: portal if present, else X11, else Unavailable -> §4.5.
```

**The `DISPLAY` trap.** `DISPLAY` is set on Wayland sessions (it is `:1` on this machine). A probe that checks `DISPLAY` or `platformName()=="xcb"` alone will happily pick the X11 grab on a Wayland desktop, where `XGrabKey` on the XWayland root only sees keys already routed to XWayland — it initialises without error and then silently under-delivers. **Gate on `XDG_SESSION_TYPE=="x11"` as well, and always prefer the portal when present.**

#### The portal call sequence — order is mandatory, and the connection matters

**The whole GlobalShortcuts flow runs on its own `QDBusConnection`.** The spec is explicit that `Registry.Register` *"must be done before any portal method call"* and *"can only be done at most once"* — and since Qt 6.5, `QGuiApplication`'s Unix theme reads `org.freedesktop.portal.Settings` (`ReadOne("org.freedesktop.appearance","color-scheme")`) and subscribes to `SettingChanged` **on `QDBusConnection::sessionBus()`, a process-wide shared connection**, during construction. Any `Register` on that connection is therefore already too late, and `QApplication` is built long before the PTT tab exists. This is the same failure Electron apps hit with xdg-desktop-portal ≥ 1.20.

```cpp
// src/ptt/portalbackend.cpp
// A PRIVATE connection: Qt has already talked to org.freedesktop.portal.Settings
// on the shared session bus, so Registry.Register could never be first there.
QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                    QStringLiteral("svxconnect-portal"));
```

Exact triples — getting the Registry's bus name wrong is a `ServiceUnknown` on first run for **every** non-Flatpak install, and the draft mixed the two up:

| Step | Bus name | Object path | Interface |
|---|---|---|---|
| 0. Register | `org.freedesktop.host.portal` | `/org/freedesktop/host/portal/registry` | `org.freedesktop.host.portal.Registry` |
| 1–4. Shortcuts | `org.freedesktop.portal.Desktop` | `/org/freedesktop/portal/desktop` | `org.freedesktop.portal.GlobalShortcuts` |
| Replies | `org.freedesktop.portal.Desktop` | `/org/freedesktop/portal/desktop/request/<SENDER>/<handle_token>` | `org.freedesktop.portal.Request` |

```
0. if (!QFile::exists("/.flatpak-info"))
       Registry.Register("SVXConnect", {})
   MUST be the first portal call ON THIS CONNECTION, at most once.
   app_id MUST equal the basename of an installed .desktop file, or you get
   "Could not register app ID: App info not found".  Under Flatpak, SKIP it
   (the app id comes from /.flatpak-info and calling it would error).

1. subscribe to org.freedesktop.portal.Request.Response at the path above
   BEFORE issuing the call, or you race the reply.
   <SENDER> = THIS connection's bus.baseService(), leading ':' stripped,
   '.' -> '_'.  NOT sessionBus().baseService(): on a private connection they
   are different names, and the reply would never be seen.

2. CreateSession({handle_token, session_handle_token})
3. BindShortcuts(session, [("ptt", {description:"Push To Talk",
                                    preferred_trigger:"CTRL+SHIFT+t"})], "", {})
   -- may be called ONCE per session.  Rebinding => close and recreate.
4. connect Activated / Deactivated / ShortcutsChanged, matching on
   session_handle AND shortcut_id.  ListShortcuts() re-reads after a change.
```

**Trigger syntax and case.** The freedesktop Shortcuts spec's key identifier is the **keysym name**, which for letters is **lowercase**: `CTRL+SHIFT+t`, not `CTRL+SHIFT+T`. Modifiers are `CTRL`, `ALT`, `SHIFT`, `SUPER`/`LOGO`, joined with `+`. `preferred_trigger` is a **hint**: render the returned `trigger_description` in the UI, never the string you asked for, and re-read it on `ShortcutsChanged`. The `timestamp` has an explicitly undefined base — use it only to order and de-duplicate Activated/Deactivated pairs.

#### evdev specifics

`ev.value` 1 = press, 0 = release, **2 = autorepeat — must be filtered** (the classic hold-to-talk bug). Read via `QSocketNotifier` on the fd, no thread. Prefer `/dev/input/by-id/` symlinks (they survive replug). Watch for hotplug and reopen, or a replugged pedal silently stops working mid-QSO.

```cpp
// src/ptt/evdevbackend.cpp -- one QSocketNotifier slot.
void EvdevBackend::onReadable()
{
    for (;;) {
        input_event ev;
        int rc = libevdev_next_event(m_dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // SYN_DROPPED: the kernel buffer overflowed and we have missed events.
            // Drain the sync stream, then re-read the CURRENT key state -- otherwise
            // our idea of the pedal and the kernel's diverge, and a missed release
            // is a transmitter that never stops. §4.6 calls that non-negotiable.
            while (libevdev_next_event(m_dev, LIBEVDEV_READ_FLAG_SYNC, &ev)
                       == LIBEVDEV_READ_STATUS_SYNC) { }
            const int down = libevdev_get_event_value(m_dev, EV_KEY, m_code);
            if (down != m_down) { m_down = down; down ? emit pressed() : emit released(); }
            continue;
        }
        if (rc == -EAGAIN) break;                  // drained: the notifier is level-triggered
        if (rc < 0) { stop(); emit lost(tr("The PTT device was disconnected.")); return; }

        if (ev.type != EV_KEY || ev.code != m_code) continue;
        if (ev.value == 2) continue;               // autorepeat
        m_down = ev.value;
        ev.value ? emit pressed() : emit released();
    }
}
```

On this machine `/dev/input/event*` is `0660 root:input`, the user is **not** in `input`, and no ACL is set — so probe with `access(path, R_OK)` first, because a pedal that enumerates as a **joystick** already works with no rule at all (`/usr/lib/udev/rules.d/70-uaccess.rules` tags `ENV{ID_INPUT_JOYSTICK}`).

**Never tell users to `usermod -aG input $USER`** — that is a system-wide keylogger grant. Ship a scoped rule generated from the learned device:

```
# /etc/udev/rules.d/72-svxconnect-ptt-local.rules   (written by the polkit helper)
SUBSYSTEM=="input", ATTRS{idVendor}=="XXXX", ATTRS{idProduct}=="YYYY", TAG+="uaccess"
```

`TAG+="uaccess"` gives an ACL to the **active seat user only** and follows fast-user-switching. `EVIOCGRAB` only for a dedicated pedal — grabbing the main keyboard makes the whole desktop appear frozen.

Two packaging rules that follow from this and are easy to get wrong:

- The **example** rule ships read-only under `/usr/share/svxconnect-qt/`, and any rule the *package itself* installs would go to `/usr/lib/udev/rules.d/` — **never `/etc/udev/rules.d/`**, because a file the package installs there becomes a dpkg **conffile** and the app writing to the same path would trigger a conffile prompt on every upgrade.
- The **generated** rule is user-specific, so `/etc/udev/rules.d/` is correct for it — under a **different name** (`72-svxconnect-ptt-local.rules`) so it can never collide with a packaged file.

### 4.4 What the settings UI shows

A **PTT** tab, replacing macOS's "External PTT":

1. **Mode** — `Push-to-talk (hold)` / `Toggle` radio pair.
2. **Backend table**, one row per backend, refreshed from cached async probes on a 2 s timer:

   | Backend | Status | Release | |
   |---|---|---|---|
   | Desktop portal (Wayland/X11) | ● Available — bound as *(the portal's returned `trigger_description`)* | ✔ | [Rebind] |
   | Hardware device | ● Not readable — needs a udev rule | ✔ | [Set up…] |
   | X11 key grab | ○ Not an X11 session | ✔ | |
   | Control FIFO | ● Listening on `~/.local/state/svxconnect/ctl` | ✔ | [Show commands] |

   The portal row shows **what the portal returned**, not what we asked for. Under KDE that happened to be `Ctrl+Shift+T`; under another backend it may be anything, including a chord the user re-bound in the system settings.

3. **Chord capture.** `QKeySequenceEdit::setMaximumSequenceLength()` is **Qt 6.5**, and the build floor is 6.4 (bookworm and noble both ship 6.4.2 — half the CI matrix). Without it the widget records up to four chords, and a four-chord sequence has no freedesktop-Shortcuts representation at all. So capture is hand-written:

```cpp
// src/ptt/chordcapture.cpp -- a read-only QLineEdit that grabs ONE chord.
void ChordCapture::keyPressEvent(QKeyEvent *e)
{
    if (e->isAutoRepeat()) { e->accept(); return; }
    switch (e->key()) {                       // a bare modifier is not a chord
    case Qt::Key_Control: case Qt::Key_Shift:
    case Qt::Key_Alt:     case Qt::Key_Meta:   e->accept(); return;
    default: break;
    }
    m_trigger = svx::triggerFromKeyEvent(e);   // keysymmap.cpp
    setText(m_trigger);
    emit captured(m_trigger);
    e->accept();
}
```

```cpp
// src/ptt/keysymmap.cpp
// QKeySequence::toString() gives Qt's human strings ("Ctrl+Shift+T", "PgDown"),
// which are NOT keysym names and have no place in a portal request. Qt sets
// nativeVirtualKey() to the xkb keysym on both the xcb and the wayland plugins,
// so xkb_keysym_get_name() gives exactly the spelling the Shortcuts spec wants
// -- lowercase for letters. Needs libxkbcommon (see Build-Depends, §6.2).
QString svx::triggerFromKeyEvent(const QKeyEvent *e)
{
    char name[64];
    if (xkb_keysym_get_name(xkb_keysym_t(e->nativeVirtualKey()), name, sizeof name) <= 0)
        return {};                              // caller shows "unsupported key"
    QStringList parts;
    const auto m = e->modifiers();
    if (m & Qt::ControlModifier) parts << QStringLiteral("CTRL");
    if (m & Qt::AltModifier)     parts << QStringLiteral("ALT");
    if (m & Qt::ShiftModifier)   parts << QStringLiteral("SHIFT");
    if (m & Qt::MetaModifier)    parts << QStringLiteral("LOGO");
    parts << QString::fromLatin1(name);         // "t", "F12", "space", ...
    return parts.join(QLatin1Char('+'));
}
```

`tests/test_keysymmap.cpp` covers `Ctrl+Shift+T → "CTRL+SHIFT+t"`, bare `F12 → "F12"`, and the unsupported-key path.

4. **Hardware learn-mode**: "Press your foot switch now" — enumerate readable `/dev/input/event*`, capture the first `EV_KEY`, show `name` / VID:PID, then offer **[Copy udev rule]** and **[Install rule…]**.

   **[Install rule…] goes through polkit with a fixed helper, never a raw `pkexec`.** `pkexec` with no action file is an arbitrary-command admin prompt, and a helper that takes "write this content to this path" is a privilege-escalation primitive wearing a hat. The shape is:

   - `data/polkit/guru.rf.SVXConnect.policy` declares **one** action, `guru.rf.SVXConnect.install-ptt-rule`, `allow_active=auth_admin_keep`, annotated `org.freedesktop.policykit.exec.path = /usr/libexec/svxconnect-qt/install-ptt-rule`.
   - `tools/install-ptt-rule` accepts **exactly two arguments**, each validated against `^[0-9a-fA-F]{4}$`, writes the fixed template to `/etc/udev/rules.d/72-svxconnect-ptt-local.rules`, and runs `udevadm control --reload && udevadm trigger`. No path argument, no content argument, no shell.
   - If the polkit action is missing (an AppImage, a hand build), the button is replaced by **[Copy this command]** and the exact `sudo tee` line. Never fall back to a bare `pkexec`.

5. **Test PTT** — a big button that arms the chosen backend and displays the **measured press→release duration in milliseconds**, so a user can prove hold-to-talk works *before* keying a transmitter. This is the single most valuable control on the page.
6. **Scripting** (the old macOS content, corrected for Linux): the FIFO vocabulary (`ptt on|off|toggle`, `tg <n>|next|prev`, `lock on|off|toggle`, `mute`, `unmute`, `volume 0-100`, `status`, `quit`), the D-Bus signals [M5], `xdg-open 'svxconnect://ptt/toggle'` [M5], and the compositor snippets.

### 4.5 When no backend can give key-release

Show a **persistent, non-dismissable notice** on the PTT tab and a one-time dialog on first run:

> **Hold-to-talk is not available on this desktop.**
> Your compositor (sway) does not provide the desktop portal's global-shortcut service, so SVXConnect cannot see when you *release* a key. PTT is running in **toggle** mode: press once to transmit, press again to stop. Transmission stops automatically after 120 s.
>
> To get true hold-to-talk, either:
> • bind a key in your compositor to the control FIFO — [Copy sway config] / [Copy labwc config] / [Copy Hyprland config]
> • or use a USB foot switch — [Set up hardware PTT]

```
# sway — --no-repeat is MANDATORY or autorepeat re-fires the press action
bindsym --no-repeat           F12 exec sh -c 'echo "ptt on"  > ~/.local/state/svxconnect/ctl'
bindsym --no-repeat --release F12 exec sh -c 'echo "ptt off" > ~/.local/state/svxconnect/ctl'
```

with a caveat printed underneath: sway's `--release` does **not** fire if another key is pressed before the bound key is released (swaywm/sway#6456) — which is precisely why the watchdog below is mandatory.

### 4.6 The TX watchdog — non-negotiable

A missed release means an **unattended transmitter**. Four defences, all required:

1. **`tx_timeout_sec` (core, default 120 s)** — already implemented, hard-unkeys with a 2-beep. Surface it in Preferences and never let it be 0 by default.
2. **Force-unkey on backend loss.** `app_ptt(a, CTL_OFF)` on: portal `Session.Closed`; D-Bus `NameOwnerChanged` for `org.freedesktop.portal.Desktop`; evdev read returning `-ENODEV`/`-EPIPE` or a `SYN_DROPPED` resync that shows the key up; `QGuiApplication::applicationStateChanged` away from active while TX is held via a windowed path.
3. **Treat "unkey" as the safe default in every error path.** If `PttManager` is ever unsure of the state, it stops transmitting.
4. **Force-unkey on teardown.** `PttManager::forceUnkey()` runs in `main()` before `app_free()` **and** from the `SIGINT`/`SIGTERM` signalfd path (§1.4). The `EVIOCGRAB` dies with the fd, but a transmission does not die with the process fast enough to be polite. M4's acceptance includes: `SIGTERM` mid-transmission unkeys before exit.

---

## 5. Licensing **[v1 for §5.1–§5.4; §5.5 bites at M11, §5.6 at M7]**

### 5.1 Decisions — and the one sentence the draft got wrong

- **Repo licence: MIT**, `Copyright (c) 2026 Diëlectricum BV` (settled in [D-1](#decisions-already-made)). Debian's DEP-5 name for MIT is **`Expat`**, and MIT is **not** in `/usr/share/common-licenses`, so `debian/copyright` must quote it in full. The About dialog and AppStream `<developer_name>` credit **Joeri Van Dooren, ON6URE**; the copyright line never carries the callsign.
- **Qt is taken under the LGPL-3.0-only arm, and that is not a preference — it is the only arm open.** Qt's open-source offer is `LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`. The **GPL-2.0-only arm is closed** because this binary links OpenSSL 3, which is Apache-2.0, and Apache-2.0 is one-way compatible with GPLv3 but **incompatible with GPLv2**. The **GPL-3.0-only arm is open but self-defeating**: taking it relicenses the Combined Work to GPLv3 and makes `LICENSE` a lie. That leaves exactly one. Write this reasoning down wherever the election is recorded, because it is the first thing a future maintainer will try to "simplify".
- **Never write `LGPL-3.0+`, `LGPLv3+` or `LGPL-3.0-or-later`.** Qt's LGPL is `LGPL-3.0-only`. Every SPDX identifier, every notice string, every `debian/copyright` short name must say `only`.
- **Dynamic linking only.** No static Qt, no vendored Qt, no `RPATH` into a private Qt prefix, no `-Wl,-Bstatic` near a `libQt6*`. `ldd build/svxconnect-qt | grep libQt6` must show separate DSOs, and `tools/licence_guard.sh` asserts it on every build.
- **The obligation is channel-scoped.** The draft said, flatly, *"you never convey Qt at all — Debian does — so there is no Qt source obligation, no written offer, nothing to host."* That is true of the `.deb` and false of the AppImage, **in the same document**, and it was the single largest error in the plan. `linuxdeploy-plugin-qt` exists to copy `libQt6Core.so.6`, `libQt6Gui.so.6`, `libQt6Widgets.so.6`, `libQt6DBus.so.6` and `plugins/platforms/libqxcb.so` into the AppDir. Copying them is conveying them.

| Channel | Qt conveyed? | LGPLv3 route | What is owed |
|---|---|---|---|
| `.deb` from `apt.svxconnect.app` | **No** — `Depends:` resolves to the distribution's `libqt6core6t64`, `libqt6gui6`, `libqt6widgets6`, `libqt6dbus6` | **§4d1** | §4a prominent notice · §4b both texts (discharged on Debian by the Policy 12.5 referral, §5.4) · §4c Qt's copyright in the About box · §4d1's "suitable shared library mechanism", which dynamic linking against versioned SONAMEs already is. **No Qt source, no written offer, nothing to host.** |
| Ubuntu PPA | No | §4d1 | as above |
| Debian proper | No | §4d1 | as above |
| Source tarball / `git clone` | No — the builder links against their own Qt | not a conveyance of a Combined Work in binary form | ship both licence texts anyway; there is no `/usr/share/common-licenses` guarantee outside a distro |
| **AppImage** | **Yes** — the Qt DSOs and the QPA plugins are inside the artefact | **§4d0** | everything above **plus** the Minimal Corresponding Source for Qt at exactly the bundled version, **plus** this application "in a form suitable for, and under terms that permit, the user to recombine or relink" — object files or a documented relink kit. Or a written offer valid **≥ 3 years**. GPLv3 §6 arrives through §4d0. Apache-2.0 §4(a)–(d) and libopus's BSD-3 notice must travel **inside the AppDir**, not only in a `.deb`'s `/usr/share/doc`. |

§4d1(a) requires "a copy of the Library **already present on the user's computer system**". A copy the AppImage brought with it is not already present; there is no reading under which a bundle qualifies for 4d1. The choice is therefore binary: drop the AppImage, or accept redistributor status and automate the compliance. **This plan keeps the AppImage and automates it** — see §5.5, which is a build job, not a paragraph of good intentions.

- **The Qt module allowlist**, enforced in CMake and again against the real link graph (§5.3):

  **Allowed** — `Core`, `Gui`, `Widgets`, `Network`, `DBus`, `Svg`, `Concurrent`, `Test`, and, gated behind `SVX_WITH_FEED` / `SVX_WITH_POSITIONING` beyond v1, `WebSockets` and `Positioning`. v1 links **six**: `Core Gui Widgets Network DBus Svg` — and after `--as-needed` the shipped binary links only four: `libQt6Core`, `libQt6Gui`, `libQt6Widgets`, `libQt6DBus`. `libQt6Svg` never appears because Svg reaches the app as the QRC icon loader's *image-format plugin*, not as a link-time dependency; `libQt6Network` is dropped too, because nothing in the M0/M1 tree references a symbol from it yet — it comes back the moment the first `QNetworkRequest` does. Verified at M0 against the built artefact with `ldd`.

  **Forbidden, hard build failure** — `Charts`, `DataVisualization`, `Graphs`, `Grpc`, `CanvasPainter`, `Quick3D`, `Quick3DPhysics`, `QuickTimeline`, `HttpServer`, `Mqtt`, `Coap`, `NetworkAuth`, `QmlCompiler`, `VirtualKeyboard`, `WaylandCompositor`, `WebEngineCore`, `WebEngineWidgets`, `WebEngineQuick`, **`Bodymovin`**.

  Two things about that list that the draft got wrong and that will be got wrong again:

  1. **The CMake target for Qt Lottie is `Qt6::Bodymovin`, not `Qt6::Lottie`.** Debian and Ubuntu ship the runtime as `libqt6bodymovin6` and the DSO is `libQt6Bodymovin.so.6`. A deny-list entry spelled "Lottie" matches nothing, in either the CMake list or the `ldd` regex — it is a guard that reads as protection and provides none.
  2. **Qt's own licensing page omits Charts and Data Visualization**, and it omits them because both are **deprecated**, not because they became LGPL. Their own module pages still say GPLv3, and Debian's `qt6-charts` 6.8.2-2 declares `License: GPL-3+`. A comment saying so lives in `cmake/LicenceGuard.cmake` beside the entries, because otherwise the next contributor reads the omission as permission.

  `WebSockets` and `Positioning` are **almost certainly** LGPL-3.0-only addons, but they are marked *unverified* until `tools/qt_copyright_check.sh` (§5.3) has actually run against the packages on a build container. The draft asserted them as verified fact; they were not, and they are not needed until M6/M8 anyway.

### 5.2 The traps this avoids

**There is no LGPL charting module in Qt 6 at all.** Qt Charts, Qt Data Visualization and their successor Qt Graphs are all GPL-3.0-only — `qt/qtcharts`'s upstream `LICENSES/` contains GPL-3.0-only, BSD-3-Clause and the commercial reference and **no LGPL text whatsoever**. Linking one silently converts the binary to GPLv3 while `LICENSE` still says MIT. Nothing in a normal build tells you it happened: it compiles, links, runs and ships.

**Substitute:** draw it. The `LevelMeter`'s single-stage ballistics (§3.3), any level history, and the TX timeline are `QPainter` in `paintEvent`; anything scrollable is `QGraphicsScene` / `QGraphicsView` (both qtbase, both LGPL). That is a few hundred lines, and it is what the macOS app converged on anyway for performance — its `LevelMeter` ended up as a single `Canvas` draw.

**QtWebEngine is out.** On Debian trixie `libqt6webenginecore6` is **58.9 MB compressed, ~186 MB installed**, and `qt6-webengine`'s `debian/copyright` is **3,722 lines, 362 `Files:` stanzas, roughly 68 distinct licences** including LGPL-2, LGPL-2.1, MPL-1.1, APSL-2.0, AFL-2.0, EPL-1.0, MS-PL and SGI-B-2.0. That attribution set would have to be reproduced and kept current in **every bundled build** (§5.5), for a client whose entire job is a TLS socket and an Opus stream. For in-app help and the licence tabs use `QTextBrowser` (QtWidgets, LGPL); for anything else `QDesktopServices::openUrl()`.

**QtMultimedia is out** — and *not* for licence reasons; it is LGPL-3.0-only and legally fine. It is out because miniaudio plus libopus already do capture, playback and codec, and because Qt 6.11's QtMultimedia documents an FFmpeg 7.1.3 dependency. Dropping it drops the entire FFmpeg / GStreamer / PipeWire licence question. (If a bundle ever did include QtMultimedia, an FFmpeg configured `--enable-gpl` or `--enable-nonfree` would make the bundle GPL or undistributable — one more reason §5.5's job inspects what is actually in the AppDir.)

**Qt Wayland is split inside one repo, and only half of it is dangerous.** `qt/qtwayland` ships both `LGPL-3.0-only.txt` and `GPL-3.0-only.txt`. The **client** side — `libqt6waylandclient`, the QPA plugin the app loads when it runs on a Wayland session, which is exactly what happened on this machine at M1 — is LGPLv3. Only the **Compositor** API is GPL-3.0-only. Running under Wayland is fine; `find_package(Qt6 COMPONENTS WaylandCompositor)` is not.

**Qt Qml Compiler is GPLv3-only, and the exception does not cover the library.** `qmllint` and `qmlcachegen` are GPLv3 with the Qt GPL exception 1.0, which permits non-GPL *output*; the `QmlCompiler` **library** carries no such exception. Using Qt Widgets rather than QML sidesteps the question entirely, which is one more reason this project is Widgets.

**Two things never to add, in any channel.** No EULA, no anti-reverse-engineering clause, no click-through restriction, and no signature or attestation check that would stop a user swapping in a modified `libQt6*.so`. LGPLv3 §4's chapeau voids compliance if your terms "restrict modification of the portions of the Library contained in the Combined Work and reverse engineering for debugging such modifications". MIT is safe precisely because it says nothing.

**And one that bites PIOS, not this repo.** §4e Installation Information applies where GPLv3 §6 would require it — a "User Product" whose installation is locked down. A `.deb` on a PC never triggers it. A Raspberry Pi appliance image with a read-only rootfs, verified boot or no root access **would**, and would owe the keys or instructions needed to install a modified Qt. Keep PIOS images user-modifiable and the question never arises. Recorded here because PIOS and this repo now share a copyright holder and a licence story.

### 5.3 Enforcement — three layers, and the CI step that could never have worked

The draft had two hand-maintained copies of the module list and a CI job that was impossible to run. Both are fixed, and both fixes are in the tree already.

**Layer 1 — `cmake/LicenceGuard.cmake`. The guard *is* the `find_package` wrapper.** One list, one call. There is no second `find_package(Qt6 …)` line anywhere for a contributor to add `Charts` to and sail past the check:

```cmake
macro(svx_find_qt6 MINVER)
    cmake_parse_arguments(SVXQT "" "" "COMPONENTS" ${ARGN})
    foreach(m IN LISTS SVXQT_COMPONENTS)
        # Quoted. The unquoted form relies on if()'s auto-dereference and
        # breaks the day a module name collides with a defined variable.
        if("${m}" IN_LIST SVX_GPL_ONLY_QT_MODULES)
            message(FATAL_ERROR "Qt6::${m} is GPL-3.0-only …")
        endif()
    endforeach()
    find_package(Qt6 ${MINVER} REQUIRED COMPONENTS ${SVXQT_COMPONENTS})
    set(SVX_QT_COMPONENTS "${SVXQT_COMPONENTS}")
endmacro()
```

**It must be a `macro`, not a `function`, and this is load-bearing.** `find_package()` sets `Qt6_VERSION`, `Qt6_VERSION_MAJOR` and the `QT_KNOWN_POLICY_*` flags in the scope it runs in. Inside a `function()` those die at the closing paren, and the very next line fails with the memorable and completely unhelpful

```
CMake Error at /usr/lib/x86_64-linux-gnu/cmake/Qt6Core/Qt6CoreMacros.cmake:
  Can not determine Qt version.
```

from `qt_standard_project_setup()`. A macro is textually inlined, so `find_package` runs in the caller's scope and everything downstream sees Qt normally. The consequence — `cmake_parse_arguments`'s `PARSE_ARGV` form is function-only, so the plain signature is mandatory here — is noted in the file. Found the hard way while building M0 (Appendix B / B4).

**Layer 2 — `tools/licence_guard.sh`, against the real link graph.** The CMake guard can only see the names it was handed. This sees what actually linked:

```sh
FORBIDDEN='libQt6(Charts|Graphs|DataVisualization|WebEngine[A-Za-z]*|VirtualKeyboard|WaylandCompositor|Quick3D|HttpServer|Mqtt|Coap|NetworkAuth|QmlCompiler|Bodymovin)'
ldd "$BIN" | grep -Ei "$FORBIDDEN"                      # link-time
find "$BUNDLE" -name 'libQt6*' | grep -Ei "$FORBIDDEN"  # dlopen()ed plugins
ldd "$BIN" | grep -qiE 'libncurses'                     # src/ui/ui.c leaked into svxcore
```

It takes an optional bundle directory as `$2` precisely because **the AppImage job must run it against `AppDir/usr/bin/svxconnect-qt`, not the dev build** — the bundled Qt is what ships, and a GPL-only module that arrives as a `dlopen`ed Qt plugin never appears in `ldd` output at all. The ncurses arm is not about licensing: it is the tripwire for `src/ui/ui.c` creeping back into `svxcore`, which would make `dpkg-shlibdeps` add a `libncursesw6` dependency to a GUI that never draws a terminal. It is wired as a `POST_BUILD` custom command, so it runs on every build and not only in CI. **Verified passing against the M0 binary.**

**Layer 3 — `tools/qt_copyright_check.sh`, which replaces a step that could not exist.** The draft's `licence.yml` said *"REUSE lint on the pinned Qt modules"* and §5.3 said *"assert `LGPL-3.0-only.txt` is present in each linked module's upstream `LICENSES/`"*. There is no pinned Qt module — Qt is an apt dependency, not a submodule — and REUSE lints a source tree, not `/usr/lib`. You cannot inspect `qtwebsockets/LICENSES/` from CI without cloning Qt's git, which nobody is going to do on every build.

What *is* cheap, real, and about the exact binaries being linked: read what Debian records for them, on the container that built them.

```sh
#!/bin/sh
# tools/qt_copyright_check.sh <binary>
set -eu
BIN="${1:-build/svxconnect-qt}"; rc=0
for lib in $(ldd "$BIN" | sed -n 's|.*=> \(/[^ ]*libQt6[^ ]*\) .*|\1|p'); do
    pkg=$(dpkg -S "$(readlink -f "$lib")" | cut -d: -f1)
    cp="/usr/share/doc/$pkg/copyright"
    [ -f "$cp" ] || { echo "FAIL no copyright file for $pkg" >&2; rc=1; continue; }
    # Debian records Qt's dual offer as "License: LGPL-3 or GPL-2" -- it drops
    # the GPL-3 arm entirely. A GPL-3.0-only module has NO LGPL stanza at all.
    if grep -qE '^License: LGPL-3( |,|$)' "$cp"; then
        echo "ok   $pkg"
    else
        echo "FAIL $pkg has no LGPL-3 stanza in $cp" >&2
        grep -h '^License:' "$cp" | sort -u >&2
        rc=1
    fi
done
exit "$rc"
```

*Verified on this machine (trixie).* The `ldd → readlink -f → dpkg -S` chain resolves `/lib/x86_64-linux-gnu/libQt6Widgets.so.6` to `libqt6widgets6` and the other three likewise. `libqt6core6t64`, `libqt6gui6`, `libqt6widgets6`, `libqt6network6`, `libqt6dbus6` each carry **98** `License: LGPL-3 or GPL-2` stanzas; `libqt6svg6` carries three plus a bare `License: LGPL-3`. `libqt6networkauth6` — a known GPL-3.0-only module — carries **three bare `License: GPL-3` stanzas and not one LGPL line**, and the script fails on it. That is the discriminator, and it is why the regex must accept `LGPL-3 or GPL-2` and `LGPL-3` and `LGPL-3 or GPL-2, and HPND-sell-variant`.

**The one false-positive trap:** every Qt binary package, including the LGPL ones, also carries ~49 `License: GPL-3 with Qt-1.0 exception` stanzas (build tools and documentation). A naive `grep GPL-3 && fail` fails every package including QtCore. Test for the **presence of LGPL**, never for the absence of the string "GPL-3".

Keep REUSE lint — scoped to **this** repository's own tree, where it belongs, and where `SPDX-License-Identifier: MIT` headers are already on every file.

### 5.4 Files to ship

| Path | Content | Lands |
|---|---|---|
| `LICENSE` | MIT verbatim, `Copyright (c) 2026 Diëlectricum BV` | M0 — **after** task M0-T1 fixes `SVXConnect-CLI/LICENSE` and `SVXConnect-PIOS/LICENSE`, which still say `Joeri Van Dooren` (D-1) |
| `THIRD-PARTY-NOTICES` | The GUI's own. Qt 6 (LGPL-3.0-only, dynamically linked, relink statement, **and the channel table of §5.1 in prose**) · libopus BSD-3 · OpenSSL 3 Apache-2.0 · miniaudio, vendored via the CLI, `Unlicense OR MIT-0`, **`Copyright 2026 David Reid`** · Lucide `ISC AND MIT` · the `dlopen`ed alsa-lib / libpulse (LGPL-2.1-or-later, neither a build dep nor distributed) · the SVXConnect-CLI core stanza · **the `Code provenance` section, carried verbatim** | M0 |
| — | **Carry the provenance stanza. It is the one part that cannot be dropped.** The CLI's notices state that the reflector v3 protocol, TLS, AES-GCM and Opus layers are the author's own prior C work under MIT and that **no code from upstream SvxLink (GPL-2.0) was copied**. The GUI statically links that same code into `libsvxcore.a`, so the claim now has to be made by this package too. Rewriting the notices "for the GUI" and losing it would be the worst possible edit | M0 |
| — | **Drop the ncurses stanza — but only from `svxconnect-qt`.** The `svxconnect` binary package, built from the *same source*, does link `libncursesw` and needs its own notices file. The drop is binary-package-scoped, not source-package-scoped: `debian/svxconnect.install` ships the CLI's file, `debian/svxconnect-qt.install` ships this one | M11 |
| — | **Fix the miniaudio year upstream too.** `SVXConnect-CLI/THIRD-PARTY-NOTICES` says `Copyright (c) 2025 David Reid`; the vendored `third_party/miniaudio.h` is v0.11.25, dated 2026-03-04, and its MIT-0 alternative reads `Copyright 2026 David Reid`. Fixing it only here leaves two notices files in one source package disagreeing about the same header. Same commit as M0-T1 | M0 |
| `licenses/LGPL-3.0-only.txt`, `licenses/GPL-3.0-only.txt` | **Both.** LGPLv3 §4b says "Accompany the Combined Work with a copy of the GNU GPL **and** this License" — LGPLv3 is written as a set of additional permissions on top of GPLv3, and shipping only the LGPL text is the single most common LGPL compliance failure | M0 |
| — | On Debian, **Policy 12.5's referral convention** — pointing at `/usr/share/common-licenses/LGPL-3` and `/usr/share/common-licenses/GPL-3`, both present on trixie — is the accepted discharge, and lintian expects it. Say it that way. It is a **convention**, universally accepted and almost certainly fine, not a provision of the licence; the draft stated it as settled law. Everywhere that directory does not exist — AppImage, source tarball, any bundle — ship the full texts | M0 |
| `licenses/Lucide-LICENSE.txt` | Lucide's `LICENSE` **verbatim**, not a generic ISC text. Lucide is ISC **plus** an explicitly retained copyright for the parts inherited from Feather (Cole Bemis, 2013–2022, MIT). A bare `ISC.txt` drops a required attribution. Record the SPDX as `ISC AND MIT` | M0 (icons land at M2) |
| `licenses/{Apache-2.0,BSD-3-Clause,MIT-0,Unlicense}.txt` | Dependency texts for the non-Debian channels. miniaudio is `Unlicense OR MIT-0`; this project **elects MIT-0**, matching the CLI, and `THIRD-PARTY-NOTICES` states the election in a sentence rather than leaving the reader to guess. Ship `Unlicense.txt` as well so the offered alternative is legible | M0 |
| `README.md` § Licensing | §4a "prominent notice": names Qt, states LGPL-3.0-only, links the texts, and states the channel split of §5.1 in two lines | M0 |
| About dialog → **Licences** tab | §4a + §4c + the relink statement in one paragraph: *"SVXConnect is built with the Qt toolkit, © The Qt Company Ltd and contributors, used under the GNU Lesser General Public License version 3. Qt is linked dynamically; you may replace the Qt libraries with modified versions. Full texts: Help → Licences."* §4c is a separate obligation from §4a and it bites because the About box **displays copyright notices during execution** — Qt's must appear among them | M2 (About dialog) |
| `packaging/debian/copyright` | DEP-5. `Files: *` → `License: Expat`, quoted in full. `Files: debian/*` → Expat. **`Files: third_party/svxconnect-cli/*` → Expat** and **`Files: third_party/svxconnect-cli/third_party/miniaudio.h` → `Unlicense or MIT-0`**, because the CLI tree is *vendored into the `.orig` tarball* (§6.2) and DEP-5 describes what is in the source package. **Do not list Qt, OpenSSL, opus or ncurses**: Policy 12.5 requires a verbatim copy of *this package's own* licence; dependencies ship their own copyright files | M11 |
| — | The header `Comment:` gets a **one-line pointer** to `/usr/share/doc/svxconnect-qt/THIRD-PARTY-NOTICES`, and nothing more. **A DEP-5 `Comment:` field is the wrong home for §4a's prominent notice** — it is free text a reviewer may or may not read and lintian ignores it entirely. `THIRD-PARTY-NOTICES` is installed to `/usr/share/doc/svxconnect-qt/` by `debian/svxconnect-qt.install`, which is what gives the packaged, offline, no-GUI reader the notice. The About tab is the braces; `debian/install` is the belt | M11 |
| `data/SVXConnect.metainfo.xml` | AppStream. **`<project_license>MIT</project_license>`** — required, and validators and reviewers will flag it if it disagrees with `debian/copyright`. `<developer_name>Joeri Van Dooren, ON6URE</developer_name>`. `appstreamcli validate` is an M12 gate | M12 |
| `packaging/appimage/compliance.sh` | §5.5. Not optional; if it is removed, the AppImage is removed with it | M11 |

**Trademark.** "Qt" is a registered trademark of The Qt Company Ltd. Say "built with the Qt toolkit". Do not ship the Qt logo, do not imply endorsement, and do not prefix or suffix the project name with "Qt" — which is exactly why the *binary* is `svxconnect-qt` (a Debian-style variant suffix, like `-gtk` or `-nox`) while the *product* is SVXConnect.

### 5.5 Bundling obligations — `packaging/appimage/compliance.sh`

This exists because §5.1's table has one row that owes real artefacts. It runs inside `appimage.yml`, after the AppDir is assembled and before `appimagetool`, and **the job fails if it fails**. Four steps:

1. **Determine what was actually bundled.** `readlink -f AppDir/usr/lib/libQt6Core.so.6` → `strings … | grep -m1 '^6\.[0-9]\+\.[0-9]\+'`, or simply `dpkg-query -W libqt6core6t64` on the build container, which for `ubuntu-24.04` gives 6.4.2. Record the exact version and the exact package versions in `AppDir/usr/share/doc/svxconnect-qt/BUNDLED-VERSIONS`.
2. **Publish the Minimal Corresponding Source.** Fetch `qtbase-everywhere-src-<ver>.tar.xz` plus a tarball for **every other Qt repo whose DSO is in the AppDir** (`qtsvg` for the image plugin, `qtwayland` for the client QPA plugin, and so on — enumerate from `find AppDir -name 'libQt6*.so*'`, do not hand-maintain a list), and attach them to the same GitHub Release as the AppImage. If the container's Qt is a distro build rather than an upstream release, publish the distro source (`apt-get source qtbase-opensource-src`) so the source matches the binary, patches included. Also record the configure flags.
3. **Ship the relink material.** §4d0 wants this application "in a form suitable for … relink". The practical form is `svxconnect-qt-<ver>-relink.tar.xz` containing the app's object files (or `libsvxconnect-qt-app.a` plus `libsvxcore.a`), the **exact** `g++` link line the build used, and a three-line README. It is small, it is generated, and it is what makes the difference between compliance and a paragraph claiming compliance.
4. **Put the notices inside the AppDir.** `licenses/LGPL-3.0-only.txt`, `licenses/GPL-3.0-only.txt`, `licenses/Apache-2.0.txt`, `licenses/BSD-3-Clause.txt`, `licenses/MIT-0.txt`, `licenses/Unlicense.txt`, `licenses/Lucide-LICENSE.txt`, `LICENSE` and `THIRD-PARTY-NOTICES` → `AppDir/usr/share/doc/svxconnect-qt/`. There is no `/usr/share/common-licenses` in an AppImage, so §4b cannot be discharged by reference here.

Then `tools/licence_guard.sh AppDir/usr/bin/svxconnect-qt AppDir` runs against the bundle, for the reason in §5.3, and the download page links the source and relink artefacts next to the AppImage itself.

An alternative to all of the above is a **written offer valid for at least three years**, which is a three-year obligation attached to a hobby project and is worse than the script.

### 5.6 Third-party service terms — a distribution blocker, not a footnote

Nothing in §5 or §6 originally resolved what happens when an API key is embedded in a public `.deb` and a public AppImage. It is a compliance question with a packaging answer, and it is why **Q4's default changed**.

| Service | Terms as they bear on redistribution | Decision |
|---|---|---|
| `tile.openstreetmap.org` | The OSM Tile Usage Policy prohibits bulk and application use outright | **Off the table.** Never a default, never a preset |
| CARTO basemaps (`dark_all`) | Free tier requires an account and enforces **per-account** request limits | One embedded key means every user of the package shares one quota; the first busy weekend revokes it **for everyone simultaneously**. Not shippable |
| Stadia Maps | Explicitly prohibits anonymous/unauthenticated use and requires **per-property** API keys | Embedding one in redistributable open-source software violates the terms outright |
| Nominatim (Q11 geocoding) | Usage policy requires an identifying `User-Agent` and caps automated use at ~1 req/s | Acceptable for one geocode per QTH edit. The `User-Agent` is **not optional** and must name the app and version |
| `ipwho.is` (Q11 fallback) | **Unverified.** Nobody has checked whether the free tier permits use from redistributed desktop software at the aggregate volume this would generate | Behind a second unchecked box, and **contingent on that check**. If the free tier does not clearly permit it, drop the fallback entirely rather than shipping an unexamined assumption |

**Therefore:** ship **no bundled tile key**, and ship the map **disabled by default**. `mapTileTemplate` is a `QSettings` string the user or the reflector sysop fills in; the key itself goes in **QtKeychain**, in neither config file. Provider presets pre-fill the *template* and never the key. With no template configured the map pane stays disabled and **says why**, which is an explicit M7 acceptance criterion. A reflector sysop pointing their community at their own tile server is the intended path, not a workaround.

The **attribution overlay is mandatory and non-dismissable** wherever tiles render — ODbL requires "© OpenStreetMap contributors" and every commercial provider requires its own line on top of that. It is listed in M7's deliverables for that reason.

**QRZ** is dropped for v1 (Q2), and the hooks stay disabled. If it ever returns: a QRZ.com XML session key goes in QtKeychain and **never** a plaintext password key in `QSettings` or `svxconnect.conf`.

---

## 6. Distribution **[beyond v1 — M11]**

### 6.1 Decisive answer

- **Primary: a native `.deb` from a self-hosted signed apt repository at `apt.svxconnect.app`, shipping BOTH the CLI and the GUI.** Packaging the CLI is the single highest-value move in this section: it replaces "hope the user ran `make install`" with a dependency `apt` enforces, and it retires the `/usr/local/bin/svxconnect` copy that currently shadows any packaged binary.
- **Secondary: an AppImage** on GitHub Releases, for Fedora / Arch / openSUSE / Mint reach — with §5.5's compliance job attached to it permanently.
- **Tertiary: an Ubuntu PPA** from the same source package (`debuild -S -sa`, `dput ppa:guru-rf/svxconnect`). Launchpad builds amd64 and arm64 natively and signs the archive with its own key. This is what QLog does alongside Flathub.
- **Eventual: Debian proper**, via the Debian Hamradio Maintainers team (`debian-hams@lists.debian.org`, ~130 team packages) — ITP against `wnpp`, packaging on Salsa, RFS to `debian-mentors`, a DD sponsor, then the NEW queue. This is a **credibility play, not a launch channel**: a package uploaded to unstable today reaches *stable* users with Debian 14 forky in 2027, and after that you cannot push updates to them. Keep the self-hosted repo running permanently regardless of archive status — the reflector protocol will move faster than Debian stable.
- **Rejected: Flatpak/Flathub and Snap.**

**Why Flatpak is rejected — lead with the path split, because it is the part that does not decay.** `config_defaults()` builds `pki_dir` from **raw `getenv("HOME")`** (`config.c:107`), while `config_state_dir()` and `config_default_path()` honour `XDG_STATE_HOME` / `XDG_CONFIG_HOME` (`config.c:145`, `config.c:160`). Flatpak redirects the XDG variables into `~/.var/app/<id>/` and leaves `$HOME` pointing at the real home. So under a sandbox the config, the control FIFO, the lock and the status file land **inside** the sandbox while the PKI private key and certificate are looked for **outside** it. That is not a permissions problem you can grant your way out of; it is one component's state silently bifurcating. Fixing it means changing `config.c`, which changes it for the CLI and PIOS too.

On top of that, Flathub's own requirements are explicit: *"Applications that rely on host components or complicated post installation setups for core functionality will not be accepted."* A GUI whose core function is to share `~/.config/svxconnect/` — including a PKI private key — `~/.local/state/svxconnect/ctl` and `svxconnect.lock` with a host-installed CLI is precisely that.

Two things **not** to claim while rejecting it, because both are wrong and both invite someone to "fix" the wrong problem:

- **FIFOs work fine across the sandbox.** `--filesystem=` is a real bind mount; `mkfifo`, `open` and `flock` on a bind-mounted host path behave normally. The failure is the `$HOME`-vs-XDG split above plus Flathub policy, **not** FIFO mechanics. Saying otherwise invites `--filesystem=home` and a false sense of having solved it.
- **"Flathub does not accept `--device=input`" is withdrawn.** The draft stated it as current policy. It is not verifiable as such (Appendix A / R5) — the primary source is a Discourse thread whose objection was flatpak-version compatibility (`--device=input` needs flatpak ≥ 1.15.6; older hosts silently widened it to `--device=all`), a constraint that has largely evaporated, and no current authoritative Flathub policy statement either way was found. The accurate sentence, if the question ever returns, is *"as of the last public discussion, Flathub builders rejected `--device=input` on compatibility grounds; re-check before relying on this"* — and the rejection does not need it at all.

**Why Snap is rejected.** The `home` interface **excludes top-level hidden directories by design**, so `~/.config` and `~/.local` are unreachable without `personal-files` — a super-privileged, manually-reviewed interface documented for the case where "the snap is the clear owner of the target directory", which is the exact opposite of a directory co-owned with a host CLI. `$HOME` is additionally remapped to `~/snap/<name>/current`. `audio-record` and `raw-input` are both `auto-connect: no`, so PTT and microphone both need a manual `snap connect`. And it is effectively Ubuntu-only. Notably **no** comparable ham application — WSJT-X, fldigi, JS8Call, QLog, GridTracker — ships a snap.

**Also considered and available as a fallback: openSUSE Build Service.** OBS takes one source package and produces signed `.deb` repositories for Debian 12/13 and Ubuntu 22.04/24.04/26.04 across amd64 and arm64 (and `.rpm` for Fedora/openSUSE), which is the standard answer to "I want signed apt repos for both distros without running `reprepro` myself". It overlaps the primary recommendation almost entirely; adopt it only if the decision is to not run `reprepro` at all.

### 6.2 The Debian source package

One source package, **three** binaries: `svxconnect` (the C/ncurses CLI and headless mode), `svxconnect-qt` (this repo), `svxconnect-archive-keyring`.

#### The `.orig` tarball must be vendored — git submodules do not survive `dpkg-source`

The CLI arrives here as `.gitmodules` → `third_party/svxconnect-cli`. **`git archive`, `dpkg-source`, `pristine-tar` and Launchpad's PPA builders all ignore submodule contents.** Left alone, `svxconnect_0.1.0.orig.tar.xz` contains an empty directory and the build dies on this project's own error message:

```
The SVXConnect-CLI core was not found at: .../third_party/svxconnect-cli
```

which is at least a good failure rather than a silent one, but it is a failure on every PPA and every `dpkg-buildpackage` outside a git checkout. `deb.yml` therefore builds the tarball explicitly, and this step is not optional:

```sh
# packaging/orig-tarball.sh <version>
git submodule update --init --depth 1
tar --xz --exclude-vcs --exclude=build --exclude=debian \
    --transform "s,^\.,svxconnect-$1," \
    -cf "../svxconnect_$1.orig.tar.xz" .
```

`--exclude-vcs` removes `.git` and the submodule's `.git` file; the CLI's working tree stays. Two consequences: `debian/copyright` gains stanzas for `third_party/svxconnect-cli/*` (Expat) and `third_party/svxconnect-cli/third_party/miniaudio.h` (`Unlicense or MIT-0`), per §5.4 — DEP-5 describes what is *in the source package* — and **task M0-T1's copyright fix must land upstream first**, or the vendored tree contradicts the stanza describing it.

#### `debian/control`

```
Source: svxconnect
Section: hamradio
Priority: optional
Maintainer: Joeri Van Dooren <ure@moreorless.be>
Rules-Requires-Root: no
Standards-Version: 4.7.2
Homepage: https://svxconnect.app
Vcs-Git: https://github.com/Guru-RF/SVXConnect-Debian.git
Vcs-Browser: https://github.com/Guru-RF/SVXConnect-Debian
Build-Depends: debhelper-compat (= 13),
               cmake (>= 3.22), ninja-build, pkgconf | pkg-config,
               qt6-base-dev, qt6-base-dev-tools, qt6-svg-dev, libgl-dev,
               libssl-dev, libopus-dev,
               libxkbcommon-dev,
               libncursesw5-dev | libncurses-dev

Package: svxconnect
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}, libasound2t64 | libasound2
Recommends: libpulse0, pipewire-pulse | pulseaudio
Description: SvxLink v3 reflector client (terminal)
 ...

Package: svxconnect-qt
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends},
         svxconnect (= ${binary:Version}),
         libasound2t64 | libasound2
Recommends: libpulse0, pipewire-pulse | pulseaudio, xdg-desktop-portal
Suggests: xdg-desktop-portal-kde | xdg-desktop-portal-gnome
Description: SvxLink v3 reflector client (Qt desktop)
 ...

Package: svxconnect-archive-keyring
Architecture: all
Multi-Arch: foreign
Depends: ${misc:Depends}
Description: GnuPG archive key for the SVXConnect apt repository
 ...
```

Seven things in there are load-bearing:

1. **`${binary:Version}`, not `${source:Version}`, and `=`, not `>=`.** `${source:Version}` is the idiom for depending on an `Architecture: all` package. `svxconnect` is `Architecture: any` and is built in lockstep with `svxconnect-qt` from the same source, and the two share a config file, a FIFO vocabulary, a lock, a status-file format **and the C core the GUI statically links**. Lockstep is what that contract needs; this settles Q13.
2. **`dpkg-shlibdeps` cannot see a `dlopen()`.** miniaudio loads `libasound.so.2` and `libpulse.so.0` at runtime, so `${shlibs:Depends}` misses both, on both binary packages. Ship it without the hand-written entries and the package installs cleanly on a minimal system and then **has no audio at all** — the worst possible packaging bug, because it looks like a success. This is confirmed by the M0 link line: the binary links neither `libasound` nor `libpulse` directly.
3. **The `pkg-t64 | pkg` alternative form is mandatory.** The 64-bit `time_t` transition renamed `libasound2` to `libasound2t64`: trixie and noble have the `t64` name, bookworm and jammy the old one. A single-name dependency makes half the CI matrix uninstallable.
4. **`Architecture: any`**, with the *shipped* architecture set living in three places that must agree and must be changed in one commit: the CI matrix in `deb.yml`, `Architectures:` in `packaging/apt-repo/conf/distributions`, and `Architectures:` in the published `.sources` file. Advertising `armhf` in one and building it in none produces `apt` "Failed to fetch" for exactly the 32-bit Pi audience this project courts (Q20).
5. **`Rules-Requires-Root: no`**, `Section`, `Priority`, `Standards-Version`, `Homepage`, `Vcs-*` and a per-binary `Description` are all present because `lintian --pedantic` is an explicit M11 gate and the draft's sketch omitted every one of them. Also required and equally absent from the draft: `debian/watch`, `debian/upstream/metadata`, `debian/changelog` in real Debian format, and `debian/svxconnect-qt.lintian-overrides` for whatever is consciously accepted.
6. **The `Maintainer:` address is published in every `.deb` and in the `Packages` index.** If a personal address is not wanted there, use a role address before the first upload, not after.
7. **`qt6-websockets-dev` and `qt6-positioning-dev` are absent** from `Build-Depends`, because `SVX_WITH_FEED` and `SVX_WITH_POSITIONING` are OFF for v1 (D-2). They are added in the same commit that turns those options on, not before.

#### `debian/rules` cannot be three lines

The draft said it was `#!/usr/bin/make -f`, `%:`, `dh $@ --buildsystem=cmake`. That is wrong for this source package, and the reason is structural: **the same source builds a CMake GUI and a `Makefile`-based ncurses CLI**, and `dh --buildsystem=cmake` configures at the top level and never touches the CLI's `Makefile`. `cmake/SvxCore.cmake` produces `libsvxcore.a` — the **library**, not the `svxconnect` **binary** — because `src/main.c` and `src/ui/ui.c` are deliberately filtered out of it (§2.1, and verified at M0: 24 core `.c` files in, those two out).

The resolution is `cmake/SvxCliBinary.cmake` (§2.1), which adds a `svxconnect` executable target re-including exactly those two files and linking `-lncursesw` — so one CMake tree builds all three artefacts and `debian/rules` stays short, but not three lines:

```make
#!/usr/bin/make -f
export DEB_BUILD_MAINTAINER_OPTIONS = hardening=+all

%:
	dh $@ --buildsystem=cmake --builddirectory=build

override_dh_auto_configure:
	dh_auto_configure -- \
	    -GNinja \
	    -DSVX_BUILD_CLI=ON \
	    -DCLI_DIR=$(CURDIR)/third_party/svxconnect-cli

override_dh_auto_test:
	dh_auto_test || [ "$(DEB_BUILD_OPTIONS)" != "${DEB_BUILD_OPTIONS#nocheck}" ]

override_dh_installsystemd:
	# no unit: the run lock, not systemd, arbitrates who owns the connection
```

This is Q19's default — one CMake tree — and it is **contingent on a byte-for-byte behaviour comparison against the CLI's own `make`**: the same `-D` defines, the same optimisation flags, the same ncurses variant (`ncursesw`, not `ncurses`). If they differ, fall back to real `override_dh_auto_build` / `override_dh_auto_install` blocks that drive the `Makefile` for the CLI half. Do not resolve the difference by silently changing the CLI's build.

#### The install files, and where the udev rule goes

```
# debian/svxconnect.install
usr/bin/svxconnect
usr/share/doc/svxconnect/THIRD-PARTY-NOTICES        # the CLI's, WITH the ncurses stanza

# debian/svxconnect-qt.install
usr/bin/svxconnect-qt
usr/libexec/svxconnect-qt/install-ptt-rule
usr/share/applications/SVXConnect.desktop
usr/share/metainfo/SVXConnect.metainfo.xml
usr/share/polkit-1/actions/guru.rf.SVXConnect.policy
usr/share/icons/hicolor/*/apps/SVXConnect.png
usr/share/svxconnect-qt/72-svxconnect-ptt.rules.example
THIRD-PARTY-NOTICES     /usr/share/doc/svxconnect-qt/     # §5.4 / C2 -- the offline §4a notice
```

**Packaged udev rules go in `/usr/lib/udev/rules.d/`, never `/etc/udev/rules.d/`.** A rule the package installs under `/etc` becomes a dpkg **conffile**, and then the app writing to the same path triggers a conffile prompt on every upgrade. In practice this package installs **no** active rule at all: it ships an **example** read-only under `/usr/share/svxconnect-qt/`, and the *generated*, device-specific rule the polkit helper writes is user state and correctly belongs in `/etc/udev/rules.d/` — under the distinct name `72-svxconnect-ptt-local.rules` so it can never collide with anything packaged (§4.3).

And the helper behind that write is a **fixed program taking two validated arguments**, `/usr/libexec/svxconnect-qt/install-ptt-rule`, declared by one polkit action with `org.freedesktop.policykit.exec.path`. A polkit action that writes caller-supplied content to a caller-supplied path under `/etc/udev/rules.d/` is a privilege-escalation primitive, and a bare `pkexec` with no action file is an arbitrary-command admin prompt. Neither ships (§4.4).

#### Naming, and the collisions to avoid

| Thing | This package | PIOS (verified in `SVXConnect-PIOS/`) |
|---|---|---|
| Binary | `/usr/bin/svxconnect-qt` | `/usr/bin/svxconnect-gui` |
| Package | `svxconnect-qt` | not currently packaged — `make install` only |
| Desktop file | `/usr/share/applications/SVXConnect.desktop` | `svxconnect-gui.desktop` |
| Desktop `Icon=` | `SVXConnect` | `svxconnect` |
| Icon path | `…/hicolor/<s>x<s>/apps/SVXConnect.png` | `…/hicolor/<s>x<s>/apps/svxconnect.png` (`Makefile:129-130`) |
| `StartupWMClass` | `svxconnect-qt` | `svxconnect-gui` |
| Portal app id | `SVXConnect` — **must equal the `.desktop` basename** | n/a |
| `QSettings` org/app | `SVXConnect` / `SVXConnect` → `~/.config/SVXConnect/SVXConnect.conf` | n/a |
| Run-lock owner kind | `gui-qt` | `gui` (`src/gui/main.c:211`) |

**The icon basename is a real collision and the draft missed it.** PIOS installs `svxconnect.png` into `hicolor` and its desktop file says `Icon=svxconnect`. Naming this package's icon after the **app id** — `SVXConnect.png` — avoids overwriting PIOS's file on a Pi where both are installed, and is what AppStream and the portal want anyway.

**`Conflicts` / `Replaces` / `Breaks`:**

```
Package: svxconnect-qt
Breaks:    svxconnect-gui (<< 0.1.0~)
Replaces:  svxconnect-gui (<< 0.1.0~)
```

declared **defensively**, against any future or third-party package using the old GUI name, so a rename never produces an unpack conflict. There is deliberately **no `Conflicts:` against PIOS**, because coexistence is now *verified behaviour* rather than a hope: with the CLI holding the run lock, the GUI refuses to start and says who has it —

```
00:39:00 [err ] the reflector connection is already held by the background service
                (svxconnect --headless) (process 62620)
```

— which is exactly the arbitration §1.4 and Q18 describe, observed live at M1. Two GUIs on one Pi do not fight over the FIFO, the lock and the status file; the second one declines and explains. Document that in the package description and in the CLI's refusal message rather than forbidding the combination.

**The `/usr/local/bin/svxconnect` shadow** is the other collision and it is not a dpkg problem, which is why dpkg will never mention it: `/usr/local/bin` precedes `/usr/bin` in the default `PATH`, so a hand-`make install`ed CLI shadows the packaged one **forever, silently**. Two mitigations, and neither of them deletes anything — never remove a file dpkg does not own:

- `debian/svxconnect.postinst` tests for `/usr/local/bin/svxconnect` and prints a warning naming it, with the `sudo make uninstall` line.
- The GUI does the same test at startup and logs at WARN, because the postinst message scrolls past and the log does not.

The `.desktop` file additionally carries `MimeType=x-scheme-handler/svxconnect;` for the URL scheme (M5), and `debian/svxconnect-qt.logrotate` ships the `copytruncate` snippet that §1.6 and Q14's size cap depend on.

### 6.3 Build and publish

**CI matrix** (`.github/workflows/deb.yml`): `{bookworm, trixie, noble, resolute} × {amd64, arm64, armhf}`. `ubuntu-24.04` and `ubuntu-24.04-arm` runners are both free for public repos, and arm64 matters disproportionately given this project's Pi audience; armhf comes from a `qemu-user-static` container (Q20). Build inside a container per suite, run `lintian --pedantic`, and fail the job on errors.

**Per-suite builds are mandatory, and the reason is not Qt.** Qt versions do diverge sharply — bookworm and noble ship **6.4.2**, trixie **6.8.2**, resolute **6.10.2**, which is why `svx_find_qt6(6.4 …)` sets the floor — but Qt's compatibility promise would let one build cover several suites. What does not is the **glibc floor** and the **`libasound2` → `libasound2t64` rename**. Build per suite and neither question arises.

Get the compatibility wording right, because the draft had it inverted and the inversion is the dangerous direction: **Qt guarantees *backward* compatibility within Qt 6** — an application built against 6.4 runs against a newer 6.8 runtime. A 6.8 build run against a 6.4 runtime is what fails, and it fails at load time with an unresolved symbol, not at build time.

**Repository** (`packaging/apt-repo/`): `reprepro`, one `conf/distributions` stanza per suite, `Components: main`, `SignWith: <keyid>`, `Acquire-By-Hash: yes`, and `Architectures: amd64 arm64 armhf source`. That `source` is only honest if the `.dsc` and `.debian.tar.xz` are actually included via `reprepro include` — do it, because the PPA and Debian-proper paths want the source upload anyway; otherwise drop `source` from the line.

Publish `dists/` + `pool/` to a `gh-pages` branch. GitHub Pages caps a site at **1 GB** with a **100 GB/month** soft bandwidth limit — single-digit-MB debs make that last years — but three limits the draft did not mention bite before the size does:

- **10 Pages builds per hour.** Publish **one `gh-pages` commit per release**, not one per matrix cell, or a 12-cell matrix burns the budget on its own.
- **A 100 MB per-file git limit**, which nothing here approaches.
- **`reprepro` keeps exactly one version of a package per suite.** That is a user-visible policy decision made by the tool: nobody can `apt install svxconnect-qt=0.1.0` once 0.2.0 has shipped, and there are no downgrades. Say so in the README rather than letting a user discover it mid-rollback.

Migrate to Cloudflare R2 via `aptly publish` if it outgrows any of that. GitHub's Pages terms also discourage using it as general file hosting, which is one more reason the AppImage and §5.5's Qt-source artefacts live on **Releases**, not on Pages.

**Signing.** A dedicated repo key, dearmored, published in **two** places under **one** filename: `/usr/share/keyrings/svxconnect-archive-keyring.pgp`, installed by the `svxconnect-archive-keyring` package, and the identical file at the root of the repository. Debian's third-party guidance is explicit and the draft got one detail wrong that breaks everything downstream of it: keys **MUST NOT** go in `/etc/apt/trusted.gpg.d` or via `apt-key add`; `Signed-By` **MUST** point to a file, not a fingerprint; the root-published export **SHOULD NOT** be ASCII-armored; and it **SHOULD** use *"the same filename that will be provided by the package"*. The draft installed `…-keyring.gpg` and published `…-keyring.pgp`. Use **`.pgp` in all three places** — repo root, package payload, and `Signed-By:` — or `Signed-By` points at a file that the keyring package will never own, and the bootstrap below silently stops self-updating.

**The bootstrap is a chicken-and-egg and must be documented as an ordered pair of commands.** `svxconnect-archive-keyring` lives *inside* the repository it authenticates. On a clean machine the keyring file does not exist yet, so the first `apt update` fails signature verification and the keyring package can never be fetched. The key must be placed by hand exactly once, at the path the package will later take over:

```sh
sudo wget -O /usr/share/keyrings/svxconnect-archive-keyring.pgp \
     https://apt.svxconnect.app/svxconnect-archive-keyring.pgp
sudo wget -O /etc/apt/sources.list.d/svxconnect.sources \
     https://apt.svxconnect.app/svxconnect.sources
sudo apt update && sudo apt install svxconnect-qt
```

The keyring package then takes over that exact path, so key rotations arrive via `apt upgrade`. This exact pair of commands belongs verbatim in the README and on the download page — not paraphrased, because the order and the filename are both load-bearing.

**The `.sources` file** (deb822; both Debian 13 and Ubuntu 24.04+ parse it natively):

```
Types: deb
URIs: https://apt.svxconnect.app/debian
Suites: trixie
Components: main
Architectures: amd64 arm64 armhf
Signed-By: /usr/share/keyrings/svxconnect-archive-keyring.pgp
```

Do **not** copy `/etc/apt/sources.list.d/signal-desktop.sources` uncritically — verified on this machine, it pins `Architectures: amd64` only (apt 404s on any box with a foreign arch enabled) and a fossilised `Suites: xenial`.

**Pinning — must key on origin, not a package glob.** v1 said *"restrict the repo to `svxconnect*`"*. The Debian wiki is explicit that a preferences file **MUST** pin with a user-controlled label. A package-glob-only pin does not stop the repo shadowing `libc6`; the priority-100 catch-all does.

```
# /etc/apt/preferences.d/svxconnect
Package: *
Pin: origin apt.svxconnect.app
Pin-Priority: 100

Package: svxconnect svxconnect-qt svxconnect-archive-keyring
Pin: origin apt.svxconnect.app
Pin-Priority: 500
```

**Two expiries, not one.** v1 scheduled a monthly re-sign for `Release`/`InRelease` — correct, and the classic self-hosted-repo outage. But if the **signing key itself** expires, every `apt update` fails with `EXPKEYSIG` no matter how fresh the `Release` file is, and the fix requires users to re-fetch the key manually — exactly the step the keyring package exists to avoid. `.github/workflows/keyexpiry.yml` therefore does both: re-sign monthly, **and fail the job when the key has under 12 months left**. Set a 5-year expiry, hold an offline revocation certificate, and document a rotation procedure that ships the new key via a keyring-package update *before* the old one lapses.

### 6.4 AppImage (secondary)

Build on `ubuntu-24.04` (glibc 2.39 floor) with `linuxdeploy` + `linuxdeploy-plugin-qt`, then a **current** `appimagetool` so you get the static runtime — the old runtime needs libfuse2, which Debian 13 does not install by default and Ubuntu 22.10+ dropped entirely.

**`packaging/appimage/compliance.sh` runs on every AppImage build and is not optional — see §5.5.** The AppImage bundles Qt, so it is conveyed under §4d0, and the job publishes the Qt source, the relink kit, and the in-AppDir licence texts. If that job is removed, the AppImage is removed with it.

**Explicitly exclude `libasound.so.2` and `libpulse.so.0` from the bundle.** Qt6 pulls libasound in transitively, and a bundled copy will be found by miniaudio's `dlopen` ahead of the host's, producing subtle breakage against a newer PipeWire.

**Run `tools/licence_guard.sh AppDir/usr/bin/svxconnect-qt AppDir`**, not the dev build — the bundled Qt is what ships, and the `find AppDir -name 'libQt6*'` arm catches a GPL-only module that arrives as a `dlopen`ed plugin and is therefore invisible to `ldd`.

Be honest in the docs that the AppImage does **not** install the CLI and does **not** integrate with the desktop (no menu entry, no autostart, and — critically — **no `.desktop` file, so `Registry.Register` cannot resolve the app id and the portal PTT backend is unavailable**). AppImage users fall back to evdev, X11 or the FIFO. Say so on the download page, next to the licence-source links.

---

## 7. Milestones

Ordered so something runs early. **M0 and M1 are done and were verified against a live reflector** — see the evidence in their rows, which records what was actually observed rather than what was intended.

### A milestone moved, and why

**M4.5 (tray icon + close-to-tray) was pulled out of M5 into v1**, after M4 was
built and tested. It is not scope creep; M4 does not work without it.

The global shortcut is registered by the RUNNING PROCESS. The portal session is
created at startup and closes when the process exits, at which point
kglobalaccel sets the component's `isActive` property to false and the key stops
doing anything. With `setQuitOnLastWindowClosed(true)` — the default, and what
v1 shipped until this point — closing the window silently disabled the headline
feature of the whole repository.

Observed exactly that way in testing: the shortcut showed as correctly bound in
both the log and `kglobalshortcutsrc`, and did nothing, because the application
had been closed. `isActive` is the one value that distinguishes "bound" from
"working", and it is worth checking first whenever a shortcut appears dead:

    gdbus call --session --dest org.kde.kglobalaccel \
      --object-path /component/SVXConnect \
      --method org.kde.kglobalaccel.Component.isActive

What shipped is the minimum that makes closing the window safe: state icon,
Show / Transmit / Connect / Quit menu, a tooltip carrying callsign, state and
talkgroup (a `QSystemTrayIcon` has no text label on Linux, unlike the macOS
menu bar item), and a one-time notification the first time the window is
closed. `setQuitOnLastWindowClosed(false)` is applied ONLY when a tray is
actually available — otherwise hiding the window would strand the process with
no interface and no way to quit it. The 260 px popup, single-instance
forwarding, the URL scheme and autostart remain in M5.

The **Scope** column implements [D-2](#decisions-already-made): **v1 is M0–M4.** Everything from M5 on is *Beyond v1* — it stays in this table with its detail intact because that detail is expensive to re-derive, not because it is scheduled.

| | Milestone | Scope | Independently verifiable by |
|---|---|---|---|
| **M0** | Repo skeleton, CMake (PkgConfig first, `svx_find_qt6` wrapper, `svxcore` target), CLI submodule + `CLI_DIR` override, `svxcore.h` shim + drift guard, empty `QMainWindow`, `SVX_WINDOW_ONLY` | **v1 · DONE** | **Observed.** All **24** core `.c` files compile and archive into `libsvxcore.a` under gcc 14.2.0; exactly two are excluded and both for a stated reason — `src/main.c` (the only `int main()`) and `src/ui/ui.c` (the only `<ncurses.h>`). **Exactly 5** CLI headers are not C++-parseable: `common/ring.h`, `audio/dev.h`, `audio/codec.h`, `audio/jitter.h`, and `src/app.h` transitively — all of them because of `_Atomic`, which is why `src/core/svxcore.h` exists. `tools/shim_check.c` is the drift guard and **was proven to catch an injected signature change** (`conflicting types for …`, at that file, on the normal build). The binary links **only** `libQt6Core`, `libQt6Gui`, `libQt6Widgets`, `libQt6DBus` — and links **neither `libasound` nor `libpulse`** directly, which is the fact `debian/control` is written around (§6.2). `tools/licence_guard.sh` passes. `SVX_WINDOW_ONLY=1` opens the window with no core (`SVX_WINDOW_ONLY set — the reflector core will not be started`) |
| **M0.5** | **`resources/strings/STRINGS.md`** — the full user-facing string inventory (§3.17), sourced from the macOS files, before any tab is written | v1 | Every string in the inventory cites a source or is marked NEW and reviewed; the CI `tr()`-literal check is wired and green |
| **M1** | `CoreLoop` (per-fd notifier registry, deferred re-entry, queued `coreChanged`), `LogBridge` (prefix synthesis, mutex-guarded `FILE*`, redaction, `-1` level guard), a single process-lifetime mutable `svx_config`, run lock that **identifies** its holder (`RunLock::Holder::isPios()`/`isSelfKind()`/`describe()`), double `setlocale` with the `%.7f` probe, deferred `app_start()`, a status line and a PTT button | **v1 · DONE** | **Observed live against `be.svx.link`**, on a machine already enrolled (`handshake_run` refuses without key+cert, `handshake.c:61-67`): SRV resolution (`SRV be.svx.link -> reflector.be.svx.link:5300`), mTLS, `authenticated as ON6URE-TPAD`, a live node roster (57 nodes in the GUI run, 62 in the CLI cross-check) with `NODE JOIN` events arriving on the notifier; the status file reading `owner=gui-qt … conn=connected tg=8`; the control FIFO answering `status`; and a clean teardown on `SIGTERM` — status file removed, lock released, `[idle] quit`. **The run-lock refusal was verified in both directions**: with the CLI holding it, the GUI logs `the reflector connection is already held by the background service (svxconnect --headless) (process N)` and does not hang. **Two deviations from the row above, and they are deliberate, not oversights.** (a) There is **no `ConfigStore` class**: the one mutable `svx_config` is a namespace-scope `g_cfg` in `src/main.cpp` (which satisfies §1.4's lifetime rule), and the only settings code in the tree is `src/ui/configfile.{h,cpp}`, which opens `svxconnect.conf` in the desktop's text editor. `ConfigStore` with `save()` is M3 work (§3.14), and §2's tree still shows where it lands. (b) The **raise** half of the run lock is not built — neither `kill(pid, SIGUSR1)` for a PIOS holder nor the `QLocalSocket` for our own kind. Per D-3 and §3.14 the raise is M5; what v1 owes is the *refusal* naming the holder, and that is what shipped. The window is also already further along than "a status label and a PTT button": `statusbar`, `sidebar`, `activitypanel`, `talkgroupbutton`, `levelmeter` and `timefmt` are all in the tree, i.e. M2 is under way. Still to demonstrate at M1: the `{3,3,5,10,20,30,60}` backoff on an unplugged network; a modal dialog open across a reconnect with **no `Multiple socket notifiers` warning**; `LANG=nl_BE.UTF-8` still emitting `51.0500000` |
| **M2** | Main window shell (incl. the `MapPlaceholder` splitter pane and a working Show-Map action), status bar, TG sidebar, Local + Recent panes, `LevelMeter` with **single-stage** ballistics, volume + mute, `PttButton` with hold+latch and the guard chain, `TimeFmt`'s three formatters | v1 | Full PIOS-equivalent parity against a live reflector: TG switching, lock (**and lock state surviving a restart**), mute, preemption banner, roger/busy/no-link beeps; `tests/test_timefmt` green; the meter falls promptly at end of speech |
| **M3** | Preferences (Connection / Audio / Talkgroups / General) + `ConfigStore::save()`; device pickers with the absent-device warning; test tone; **mic AGC / AGC target / mic gain / jitter_ms / default_tg / lock_on_start controls**; the TG-list-edit monitor re-push; the QTH reconnect prompt | v1 | `diff` before and after a no-op save is empty; change a device in the GUI and `svxconnect` picks it up next launch; **add a TG in Preferences and hear it without switching TGs**; set a QTH, accept the reconnect, see it on the portal; `tests/test_configstore` green |
| **M4** | **Global PTT.** `PttManager`, portal backend on a **dedicated `QDBusConnection`** with the correct Registry triple, evdev with autorepeat filtering **and `SYN_DROPPED` resync**, X11, FIFO; `ChordCapture` + `keysymmap` (libxkbcommon); learn-mode; the polkit udev helper; **Test PTT with measured hold duration**; the watchdog on all four loss paths | **v1 — the point of the repo** | Hold-to-talk works unfocused on Plasma-Wayland (portal) and on X11 (grab); a USB foot switch works after the generated rule; killing `xdg-desktop-portal` mid-transmission unkeys within one service tick; `SIGTERM` mid-transmission unkeys before exit; `tests/test_keysymmap` green |
| **M4.5** | **Tray icon + close-to-tray. PULLED INTO v1** — see the note below the table; M4 does not function without it. | **v1 — DONE** | Second launch raises the first window; `xdg-open 'svxconnect://ptt/toggle'` keys the running instance **without stealing focus**; with PIOS installed and holding the lock, the second launch reports it and does not hang; `dbus-monitor "interface='guru.rf.SVXConnect'"` shows PTT and connection edges |
| **M6** | Enhanced reflector: `QWebSocket` feed (snapshot / partial-merge `node_upsert` / talk_start / talk_stop / `/`-callsign synthesis / **`qth.long`** / **lenient numeric coercion**), 5 s probe, 15 s reconnect, **1 s domain-change debounce**, portal JSON fetcher, Reflector pane, `TalkerBridge` | Beyond v1 | Against a live enhanced reflector the Reflector pane fills; against a plain reflector it hides within 5 s and Recent takes over; **joining mid-over shows the station already talking**; a portable `ON6URE/P` plots at its real coordinates, not (0,0); `tests/test_feedschema` green |
| **M7** | Map: tile renderer + disk cache + **mandatory attribution overlay** + user-supplied tile template, node overlay (incl. **own-node synthesis**), grid clustering + spiderfy, stacked popup with **multi-line `callsignInfo`**, 5-level camera, recenter; the `MapPlaceholder` is swapped out with no shell change | Beyond v1 | Pins land correctly at every zoom; a busy reflector pans at 60 fps; `tests/test_clustering` reproduces the reference spiderfy layout; **with no tile key configured the map stays disabled and says why** (§5.6) |
| **M8** | Local history with N-day retention, Recent badge, QTH (manual + optional GeoClue + geocoding + picker sheet), Maidenhead | Beyond v1 | Restart and Recent is intact; a QTH set in the GUI appears on the portal map **after the prompted reconnect** |
| **M9** | Enrolment: first-run detection, wizard driving `svxconnect --enroll` by **absolute path** via `QProcess` reading **stderr** with `-v`, live output, cancel; **writes `certIssuedForCallsign`/`Email` on success**; first-run seeding of those keys from `pki_cert_info()`; cert badge with expiry; remove-all | Beyond v1 | A fresh machine with only a callsign and email reaches "connected" without a terminal; **changing the callsign afterwards raises the mismatch alert** — including for a user who enrolled with the CLI before installing the GUI |
| **M10** | Diagnostics overlay (**incl. `Cam/s`**), log pane with levels + rotation (`copytruncate` + SIGHUP + size cap) + redaction, device hot-swap and `PrepareForSleep` recovery, PTT/scripting docs tab, i18n scaffolding driven from `STRINGS.md` | Beyond v1 | `Ctrl+Shift+D` shows live numbers; the log rotates without going to an unlinked inode; no email local-part or full-precision coordinate appears in it; unplug a USB headset mid-QSO and audio recovers once, not in a loop; suspend/resume recovers |
| **M11** | Packaging: source package for **all three** binaries with a vendored `.orig` tarball, full `debian/control`, CI matrix `{bookworm,trixie,noble,resolute}×{amd64,arm64,armhf}`, reprepro on GitHub Pages (one commit per release), keyring package + **`.pgp` bootstrap flow**, origin pin, key-expiry job, AppImage **+ the §5.5 compliance job** | Beyond v1 | `apt install svxconnect-qt` on a clean trixie VM pulls the CLI, launches, connects — starting from the documented two-`wget` bootstrap; `lintian --pedantic` is clean; the AppImage runs on Fedora; the release carries `qt-source-<ver>` and a relink kit |
| **M12** | Polish: light/dark parity via palette roles (no hard-coded hex except PTT red, the meter gradient, star yellow, badge orange, the diagnostics bar), HiDPI + fractional scaling, keyboard navigation, AppStream metainfo with `<project_license>`, screenshots, README | Beyond v1 | Screenshots in both themes at 100 % and 150 % scaling; Discover/GNOME Software renders the metainfo; `appstreamcli validate` is clean |

**M0 residue — five things the milestone implies that the tree does not yet have.** They are small and they are named so nobody assumes otherwise:

1. The **C++ half of the drift guard** (`tools/shim_layout_check.cpp` + `tools/svxcore_layout.inc`, §1.3) is designed but not in the tree — `tools/` holds only `shim_check.c` and `licence_guard.sh`, so *prototypes* are cross-checked on every build and *struct layouts* are not.
2. **`cmake/SvxCliBinary.cmake` does not exist** (not merely "not included"), so `libsvxcore.a` builds but the `svxconnect` ncurses binary does not — a hard prerequisite for §6.2's `debian/rules`.
3. **`resources/`, `data/` and `licenses/` are all empty directories.** There is no `resources/icons.qrc` at all, and none of §5.4's "lands at M0" licence texts (`LGPL-3.0-only.txt`, `GPL-3.0-only.txt`, `Apache-2.0.txt`, `BSD-3-Clause.txt`, `MIT-0.txt`, `Unlicense.txt`, `Lucide-LICENSE.txt`) have landed. `LICENSE`, `THIRD-PARTY-NOTICES` and the README's licensing section **have**.
4. **Task M0-T1 has not landed.** `SVXConnect-CLI/LICENSE` and `SVXConnect-PIOS/LICENSE` still read `Copyright (c) 2026 Joeri Van Dooren`, and `SVXConnect-CLI/THIRD-PARTY-NOTICES` still says `Copyright (c) 2025 David Reid` — while this repo's `LICENSE` already says `Diëlectricum BV` and its own notices already say `2026`. D-1 wanted M0-T1 first; it is now owed, and it blocks §6.2's vendored `.orig` tarball.
5. `src/core/svxcore.h` includes **nine** of §1.3's ten clean headers — `reflector/enroll.h` is absent because nothing references `enroll_run` before M9 (Appendix B / B3).

Items 1–4 block M11; item 5 blocks nothing before M9. None blocks M1–M4.

**Known issue carried out of M1, and it is a CI hazard rather than a user-facing bug.** The run-lock refusal path opens a **modal `QMessageBox`**. On a real desktop that is correct and was verified as such. Under `QT_QPA_PLATFORM=offscreen` — which is how M11's CI will run the binary — a modal dialog **blocks forever**: there is no user, nothing dismisses it, and the job hangs until the runner's timeout rather than failing. Two acceptable fixes, and one of them must land **before** any CI job launches the GUI:

- a **`--no-gui`** (or `--check`) mode that performs the lock check, prints the same message to stderr, and exits with a distinct status, or
- a **non-modal path**: when `QGuiApplication::platformName()` is `offscreen`/`minimal`, or when `stderr` is not a TTY, log the refusal and `return 1` instead of calling `exec()` on a dialog.

The second is preferable because it also covers a user launching from a broken session. Either way it is an **M11 blocker**, listed here rather than in §1.4 because §1.4 describes the desktop behaviour and that behaviour is correct.

Upstream CLI patches (§3.15) land opportunistically alongside M1–M4, each as its own PR against `SVXConnect-CLI` with a `make test` run. Patch 14 (C++-clean headers) can land any time and deletes `src/core/svxcore.h`, `tools/shim_check.c`, `tools/shim_layout_check.cpp` and `tools/svxcore_layout.inc` when it does.

---

## 8. Open decisions

Each needs the repo owner's call. A recommended default is given for every one; implementers proceed on the default unless overruled.

**Three are already closed and are recorded at the top of this document, not here.** [D-1](#decisions-already-made) settles the copyright holder and the credits, which closes **Q16** below. [D-2](#decisions-already-made) fixes v1 at M0–M4, which is what the *Scope* column of §7 implements and what makes several defaults below read "beyond v1" rather than "later". [D-3](#decisions-already-made) keeps the run lock, the `gui-qt` owner kind and argv-before-lock in v1 even though the tray, the single-instance server and the URL handler are not.

**Q1 — Naming.** Binary `svxconnect-qt`, package `svxconnect-qt`, desktop id `SVXConnect`, run-lock owner kind `gui-qt`. Or reuse `svxconnect-gui` and collide with PIOS?
→ **Default: `svxconnect-qt` / `SVXConnect` / owner kind `gui-qt`.** PIOS already owns `svxconnect-gui` and the `"gui"` owner kind; the distinct kind is what makes §1.4's raise path correct.

**Q2 — QRZ enrichment.** No Linux analogue for `be.moreorless.qrz`, and `../QRZ/INTEGRATION.md` does not exist in any repo here, so the wire contract is unavailable. (a) drop; (b) direct QRZ.com XML client with credentials in QtKeychain; (c) HamQTH.
→ **Default: (a) drop for v1.** Ship the hooks (`QRZRecord`, `bestCityLabel`, `homeCall()` normalisation) behind a disabled `net/qrzclient`. **Never** reintroduce a plaintext password key.
*Two consequences, both now handled:* in non-WSS mode there are no coordinates for talkers, so the fallback map shows only your own node (M7's own-node synthesis, §7, makes that at least non-empty); and the Local row's second line collapses, so §3.4 makes the row single-line when no city is available.

**Q3 — Map stack.** (a) hand-rolled Web-Mercator tile renderer; (b) `QQuickWidget` + QtLocation OSM plugin; (c) no map in v1.
→ **Default: (a), delivered at M7**, with the `MapPlaceholder` shipping in M2 so the shell, splitter, persistence and menu action are real from the start rather than a menu item that toggles nothing.

**Q4 — Tile provider.** CARTO `dark_all`, Stadia, or self-hosted?
→ **Default (changed): ship no bundled key and the map disabled, with a configurable tile URL template + a QtKeychain-stored key.** Embedding one key in a public `.deb` and AppImage blows the quota for every user simultaneously and violates Stadia's terms outright (§5.6). Provider presets pre-fill the template, never the key. The reflector sysop pointing a community at their own tile server is the intended path.

**Q5 — Config ownership split.** Which keys live in `svxconnect.conf` and which in `QSettings`?
→ **Default:** everything the core parses → the conf (identity, TGs, **`default_tg`, `lock_on_start`**, audio incl. **`mic_agc`/`mic_agc_target_pct`/`mic_gain`/`jitter_ms`**, PTT timeout, FIFO, lock/status/log paths). GUI-only → QSettings: window geometry, `showToolbar`/`showSidebar`/`showMap`/`showDiagnostics`/`alwaysOnTop`, `mapPaneHeight`, `mapHomeRadiusKm`, `mapTileTemplate`, `repeaterPrefixes`, `localHistoryDays`, `udpAudioStatusEnabled`, `enhancedReflectorEnabled`, `tgInfoJSON`, `callsignInfoJSON`, `mapInfoAutoUpdate`, `mapInfoLastFetched`, `certIssuedForCallsign`, `certIssuedForEmail`, muted TGs, PTT binding, autostart. Tile API key → QtKeychain, neither file. Same key names as macOS where they exist.

**Q6 — Tray-keeps-app-alive.** `setQuitOnLastWindowClosed(false)` or `true`?
→ **Default: `false`, with a first-close notification and a General-tab preference.** A radio client that keeps receiving with the window closed is the point. See Q18 for the run-lock consequence.
*But v1 ships `true`,* because v1 has no tray (D-2): with `false` and no tray icon, closing the window would leave a running process with no way back to it. The `quitOnLastWindowClosed` key is defined in `QSettings` from M2 so that M5 flips a default rather than migrating anything (§3.6, §3.18).

**Q7 — Upstream CLI patches.** Land them in `SVXConnect-CLI` or carry local patches?
→ **Default: land them upstream.** Revised priority order, reflecting what the review surfaced:
 1. **`push_monitor` export** — without it a TG added in Preferences is inaudible; the workaround (re-select) is a live wire.
 2. **Poll `svx_dev_poll_event()` in `app_service`** with the debounce/backoff/TX-stop design of §3.15 — a USB unplug is a common desktop scenario and the GUI cannot fix it from outside.
 3. **`rc_send_node_info()`, split from `crypto_gen_tx_params()`** — otherwise QTH edits need a reconnect.
 4. **`jitter_end_of_stream()` on the disconnect edge** — one line, and the tail of an over currently vanishes.
 5. **`call_strip_ssid()` to handle `/`** — one line; note the draft had this inverted.
 6. **`log_open_file` write-through**, sequenced against `LogBridge`.
 7. Opus decode buffer to `SVX_FRAME*6`.
 8. Tail-trim direction (hold-back FIFO).
 9. `on_cert_message` callback for frame types 10/16/18/19.
 10. Node roster from `MsgServerInfo`.
 11. 250 ms post-switch audio gate.
 12. `auto_duck` implementation.
 13. Second `rc_callbacks` observer.
 14. **C++-clean public headers** (`extern "C"`, `SVX_ATOMIC`, tagged typedefs) — deletes the shim.
 15. `config_save`.
 16. (low) `reflector_hosts` failover list.
 17. (low) `app_output_device_name()` / `app_input_device_name()`, so the Audio tab can say which device is actually open.
 Nothing is a fork.

**Q8 — Connection-state vocabulary.** 13 macOS states, or the core's 4 + detail?
→ **Default: 4 + detail**, and the four are **Idle / Connecting / Connected / Reconnecting** — because `rc_state_name()` renders `RC_BACKOFF` as `"reconnecting"`, lowercase (`client.c:70-78`). v1's "Backoff" did not match what the core returns.

**Q9 — Roger-beep default.** macOS `false`; CLI `roger_beep = 1`.
→ **Default: follow the CLI (on).** One shared config file must have one answer. Same for `roger_beep_min_sec`: the CLI's 0…60 range, not macOS's 0…10.

**Q10 — Switchable-list priority.** macOS force-strips priority from switchable entries; `config_tg_priority()` honours it.
→ **Default: follow the core.** Document it in the Talkgroups tab help text so one config string behaves identically in both clients.

**Q11 — Geolocation.** Manual only, or GeoClue2 + Nominatim + `ipwho.is`?
→ **Default: manual primary; GeoClue behind `-DSVX_WITH_POSITIONING=ON` *and* an unchecked box; `ipwho.is` behind a second unchecked box, contingent on the terms check of §5.6 — if its free tier does not clearly permit redistributed desktop use, drop that fallback entirely.**

**Q12 — Icon set.** Bundle Lucide/Phosphor, or `QIcon::fromTheme`?
→ **Default: bundle Lucide in a QRC, with `fromTheme` as a fallback where a good standard name exists.** Ship Lucide's `LICENSE` **verbatim** (ISC + retained Feather MIT), not a generic ISC text.

**Q13 — Hard-depend on the packaged CLI?**
→ **Default: `Depends: svxconnect (= ${binary:Version})`** — lockstep, not `>=`, because the two share a config/FIFO/lock/status contract and the GUI links the CLI's core. The AppImage cannot honour this; document that AppImage users install the CLI separately or use the wizard's manual instructions.

**Q14 — Log redaction policy.**
→ **Default: redact by default, with a "verbose diagnostic log" checkbox that disables redaction for one session.** Mask email local-parts (`j***@example.com`), round coordinates to 4 decimals, never log key material. Redaction happens **inside the sink** (§1.6) so it covers the connect-worker's lines too. Size cap plus a `copytruncate` logrotate snippet in the `.deb`.

**Q15 — "Land where you left off" TG restore.**
→ **Default: follow the core.** `default_tg` is the documented shared contract. If wanted, add `restore_last_tg` as an explicit GUI preference, off by default.

**Q16 — Copyright holder. SETTLED — see [D-1](#decisions-already-made).** This was the one blocking question in the draft, and it is closed. The finding stands and is worth keeping: `SVXConnect-CLI/LICENSE` and `SVXConnect-PIOS/LICENSE` both read exactly `Copyright (c) 2026 Joeri Van Dooren`, while `SVXConnect-OSX` asserts `© 2026 Diëlectricum BV` in its About panel (`SVXConnectApp.swift:205`) and `Info.plist:36` and is signed under the Dielectricum Apple team. **The family claimed two different holders — an individual and a BV — and the draft asserted a uniformity that did not exist.**
→ **Resolved: the holder is `Diëlectricum BV`**, with the diaeresis (`Di` + `ë` U+00EB + `lectricum`), in `LICENSE`, `debian/copyright`'s `Files: *` and AppStream's `<project_license>`; **Joeri Van Dooren, ON6URE** is credited in the About dialog and in AppStream's `<developer_name>`. Licence MIT, DEP-5 name `Expat`.
**This is a task, not a note.** `SVXConnect-CLI/LICENSE`, `SVXConnect-CLI/THIRD-PARTY-NOTICES` and `SVXConnect-PIOS/LICENSE` must be updated to match, as **M0-T1**, *before* this repo's `LICENSE` is written — because §6.2 vendors the CLI tree into the `.orig` tarball and §5.4's `debian/copyright` stanza for `third_party/svxconnect-cli/*` would otherwise describe a holder the vendored tree itself contradicts. Do **not** append `, ON6URE` to any copyright line: it belongs in the credits, and adding it breaks byte-identity across the four repos, which was the point of making them agree.

**Q17 — Feed→talker bridge ownership. NEW.** Does the WS feed inject talkers into the core's TG manager (macOS behaviour, fixes SvxLink's lack of `MsgTalkerStart` backfill, but lets feed data drive preemption and `last_heard`), or is the merge display-only in Qt (safer, but the Local pane is decorative for anyone joining mid-over)?
→ **Default: inject, with the four guard rails of §3.14** (monitored TGs only, TCP wins on conflict, tagged for withdrawal on feed loss, no roger beep on injected stops). Preemption moves your *receive* talkgroup, never your transmitter, so the risk is surprise rather than harm — and macOS has run this way in production.

**Q18 — Run-lock lifetime while disconnected. NEW.** With Q6's `false`, a tray-resident GUI the user explicitly *disconnected* still owns the lock, so `svxconnect` in a terminal refuses to start.
→ **Default: hold the lock for the process lifetime, and say so loudly** — in the tray tooltip and in the CLI's refusal message ("the desktop client is running; quit it or use its window"). Releasing on disconnect and re-acquiring on connect is offered as a *preference*, off by default, because it makes the Connect button fail for a new reason (the CLI grabbed the lock meanwhile) and `svx_lock_release()` is idempotent and file-static, so the re-acquire path needs its own error UI. See Appendix A/R4.

**Q19 — CLI build system inside the Debian package. NEW.** Build `svxconnect` from the top-level CMakeLists (§2.1's `SvxCliBinary.cmake`), or drive the CLI's own hand-written `Makefile` from `override_dh_auto_*`?
→ **Default: CMake target**, contingent on a byte-for-byte behaviour comparison against `make` (flags, `-D` defines, ncurses variant — `ncursesw`, not `ncurses`). Fall back to the overrides if they differ.
Either way, **`debian/rules` is not three lines** — the draft's claim that it could be assumed one build system for a source package that has two. `dh $@ --buildsystem=cmake` configures at the top level and never touches the CLI's `Makefile`, and `cmake/SvxCore.cmake` deliberately excludes `src/main.c` and `src/ui/ui.c`, so it produces `libsvxcore.a` and **not** the `svxconnect` binary. §6.2 carries the real file.

**Q20 — armhf. NEW.** Build it via `qemu-user-static`, or drop it from `control`, `conf/distributions` and `.sources`?
→ **Default: build it.** 32-bit Pi OS is still common and the plan explicitly courts the Pi audience. Declaring an architecture and not shipping it produces `apt` "unable to locate" for exactly those users. If the emulated job proves unmaintainable, remove armhf from all three places in one commit.

---

## Appendix A — Rejected critiques

Five claims were checked and not adopted. Each is recorded with the evidence, because the plan is otherwise a record of accepting criticism and a future reader will otherwise re-raise them.

**R1 — "Opus decode buffer: use `SVX_FRAME*7`, or `*6 + SVX_FRAME`, for headroom for the concealment frames `jitter_push` may prepend in the same call."** (technical #48)

*Rejected on the facts; `SVX_FRAME*6` stands.* Verified at `jitter.c:59-72`: the concealment loop and the real decode do **not** accumulate into one buffer. Each concealed frame is decoded with a cap of `SVX_FRAME` and immediately handed to `queue_pcm()`, which writes it into the ring; only then is `pcm[]` reused for the packet decode with cap `SVX_FRAME * 4`.

```c
for (int i = 0; i < gap && i < 8; i++) {
    int n = codec_decode(j->codec, NULL, 0, pcm, SVX_FRAME);
    if (n > 0) { queue_pcm(j, pcm, n); j->n_concealed++; }   /* drained before reuse */
}
int n = codec_decode(j->codec, opus, (int)len, pcm, SVX_FRAME * 4);
```

`SVX_FRAME` is 320 (`dev.h:40`), so `*6` = 1920 samples = 120 ms at 16 kHz — which is **exactly** Opus's maximum frame duration. Extra headroom buys nothing and would mislead the next reader into thinking a larger packet is possible. The arithmetic in the critique is right; the rationale is wrong, and the rationale is what would survive into a code comment.

**R2 — "`lastHeard` is feed-first on macOS; the plan makes it local-only, so button ordering will differ in enhanced mode."** (completeness #16)

*Finding accepted as a documented divergence; the proposed change rejected.* `TalkGroupManager.swift:576-618` does prefer feed sessions and deliberately returns **nil** rather than falling back to local history. But `tgm_last_heard()` is the same clock the core's own linger, idle-drop and preemption logic runs on. Ordering the buttons by a *different* clock than the one that decides which talkgroup you get moved to produces a sidebar whose order contradicts its own behaviour — a worse bug than a cosmetic difference from macOS.

**Kept:** ordering uses `tgm_last_heard()` in both modes. **Added** (§3.3): when the feed is available and its session time for a TG is *newer* than the core's, the row's sub-label shows the feed time, so the user sees the better information without the ordering diverging from the state machine. This divergence from macOS is recorded in `STRINGS.md`'s notes and in the Talkgroups tab help.

**R3 — "Add `Q_ASSERT(np[i].events & POLLIN)` so a future core change that produces a POLLOUT-only fd fails loudly."** (technical #10)

*Intent accepted, mechanism rejected.* The underlying observation is correct and valuable: `rc_poll_fds()` (`client.c:462`) adds `POLLOUT` to the TLS fd *in addition to* `POLLIN`, never alone, which is why a Read notifier — which the kernel wakes on `POLLERR`/`POLLHUP` regardless of `events` — covers everything PIOS gets from its unconditional `G_IO_ERR | G_IO_HUP`.

But `Q_ASSERT` compiles out under `QT_NO_DEBUG`, which is precisely the build a user runs and precisely when a hang would matter. §1.4's `reconcile()` therefore creates the Read notifier **unconditionally** (so a POLLOUT-only fd still gets serviced rather than hanging) and logs at WARN naming the fd. Fail-safe beats fail-loud for something that runs in a release binary.

**R4 — "Release the run lock on explicit disconnect and re-acquire on connect."** (technical #23)

*Problem accepted, remedy rejected as the default.* The problem is real: with Q6's `setQuitOnLastWindowClosed(false)`, a tray-resident GUI that the user disconnected still blocks `svxconnect` in a terminal, and that is surprising.

The remedy is worse than the problem as a default. `svx_lock_release()` closes a file-static fd and does not unlink the file (deliberately — unlinking races a concurrent acquirer, per `lock.h`). So a release/re-acquire cycle introduces a new failure mode on the *Connect* button: between the disconnect and the reconnect, `svxconnect` may have taken the lock, and the user's Connect now fails for a reason they did not cause and cannot see. That needs its own dialog, its own error string, and a decision about whether to poll for the lock coming free.

**Kept:** the lock is held for the process lifetime, matching PIOS and the CLI. **Added** (Q18): the tray tooltip and the CLI's refusal message both name the holder, and a `release_lock_when_disconnected` preference exists, off by default, for users who genuinely alternate between the two clients.

**R5 — "Flathub does not accept `--device=input`", stated as current policy.** (adopted from the plan's own §6.1, challenged by legal-dist D1 — recorded here because the *challenge* is accepted and the *original assertion* is what gets rejected.)

*The draft's assertion is withdrawn.* The primary source is a Flathub Discourse thread whose objection was flatpak-version compatibility (`--device=input` requires flatpak ≥ 1.15.6; older hosts silently widened it to `--device=all`), a constraint that has largely evaporated, and no current authoritative Flathub policy statement either way was found. §6.1 now says *"as of the last public discussion, Flathub builders rejected `--device=input` on compatibility grounds; re-check before relying on this."* The Flatpak rejection rests entirely on the verified `pki_dir`-from-`$HOME` vs. XDG-redirected-state split (`config.c:107` against `:145`/`:160`), which is sufficient on its own and does not decay.

---

## Appendix B — Corrections found during verification, raised by no reviewer

Five things were found by reading the code and then by building it, and none of them came from a review. B1–B3 came out of verifying the draft against `SVXConnect-CLI`; B4 and B5 came out of actually building and running M0 and M1, which is the only way either could have surfaced.

**B1 — `call_strip_ssid()` was described backwards.** The draft's §3.5 and §3.15 both said the core *"handles `/` but not `-`"* and prescribed adding `-`. Verified at `util.c:128-137`:

```c
for (; src[i] && src[i] != '-' && i + 1 < cap; i++)
    dst[i] = (char)toupper((unsigned char)src[i]);
```

It stops at **`-`** and does **not** handle `/`. The upstream patch is to add `/`. This is not cosmetic: `/`-suffixed callsigns are exactly what the reflector's AI decoder emits (M7's map has to resolve them to a home call before it can plot a portable station), so today a station returning as `ON6URE/P` does not match your own callsign and the roger-beep self-suppression fails. Corrected in §3.5, §3.15 gap **5** and Q7 priority 5.

**B2 — `jitter_end_of_stream()` already exists.** The draft's gap table implied it needed implementing. It is at `jitter.c:136` and is already called from three sites (`app.c:125` talker-stop, `app.c:141` flush-notify, `app.c:797` test tone). The gap is only that the **connected→disconnected edge calls `jitter_flush()` instead** (`app.c:106`), discarding the tail. Downgraded from an implementation task to a one-line call-site change, which is why it rose to Q7 priority 4.

**B3 — `svxcore.h`'s ten "clean" headers must include `reflector/enroll.h`.** The draft listed it among the verified-clean headers but the shim's stated contents ("the ten clean headers") did not enumerate it consistently against the §2 tree, where `enrolwizard` needs the `enroll_run` prototype visible only to state the never-call-it rule. It is included in §1.3's list, and the never-call-it rule is a comment beside it. *As built at M0 the shim includes nine of the ten* — `reflector/enroll.h` is not there yet because nothing references `enroll_run` until M9's wizard. Add it with the wizard, together with the comment; it is listed in §7's M0 residue so it is not mistaken for an oversight.

**B4 — `svx_find_qt6()` cannot be a CMake `function()`.** The draft wrote the licence guard and the `find_package` call as two hand-maintained lists, and the review's fix (crit B4) was to fuse them into a `function(svx_find_qt6)` wrapper. Building M0 showed the fusion is right and the *keyword* is wrong. `find_package()` sets `Qt6_VERSION`, `Qt6_VERSION_MAJOR` and the `QT_KNOWN_POLICY_*` flags **in the scope it runs in**. Inside a `function()` those die at the closing paren, and the next line fails with

```
CMake Error at /usr/lib/x86_64-linux-gnu/cmake/Qt6Core/Qt6CoreMacros.cmake:
  Can not determine Qt version.
```

from `qt_standard_project_setup()` — an error that names neither the guard nor the wrapper. `cmake/LicenceGuard.cmake` therefore defines a **`macro`**, which is textually inlined and runs `find_package` in the caller's scope. One consequence follows and is noted in the file: `cmake_parse_arguments`'s `PARSE_ARGV` form is function-only, so the plain signature is mandatory there. Recorded in §5.3.

**B5 — the run-lock refusal is modal, and that makes it a headless-CI hazard.** M1's coexistence test passed exactly as §1.4 and Q18 describe: with `svxconnect --headless` holding the lock, the GUI refused to start and named the holder and its pid. It does so through a modal `QMessageBox`. Under `QT_QPA_PLATFORM=offscreen` — the way M11's CI will launch the binary — that dialog **blocks forever**, and the job hangs to its runner timeout instead of failing with a status. No reviewer raised it because no reviewer ran the binary twice. §7 records it as an M11 blocker with the two acceptable fixes (`--no-gui`, or a non-modal path keyed on `platformName()`/TTY). The desktop behaviour is correct and does not change.
