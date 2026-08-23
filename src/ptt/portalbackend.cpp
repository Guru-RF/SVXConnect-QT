/* SPDX-License-Identifier: MIT
 * SVXConnect-Qt — Copyright (c) 2026 Diëlectricum BV
 */
#include "ptt/portalbackend.h"
#include "core/svxcore.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QFile>
#include <QUuid>
#include <QRegularExpression>

/* The (s, a{sv}) struct the portal's a(sa{sv}) is made of. */
QDBusArgument &operator<<(QDBusArgument &arg, const Shortcut &s)
{
    arg.beginStructure();
    arg << s.first << s.second;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, Shortcut &s)
{
    arg.beginStructure();
    arg >> s.first >> s.second;
    arg.endStructure();
    return arg;
}

namespace {

const char *kService  = "org.freedesktop.portal.Desktop";
const char *kPath     = "/org/freedesktop/portal/desktop";
const char *kIface    = "org.freedesktop.portal.GlobalShortcuts";
const char *kRequest  = "org.freedesktop.portal.Request";
const char *kRegistry = "org.freedesktop.host.portal.Registry";

/* Must equal the basename of an installed .desktop file, or Register fails
 * with "Could not register app ID: App info not found".
 *
 * Plain "SVXConnect" rather than a reverse-DNS id, and that is a deliberate
 * trade. KDE's portal registers the global-shortcuts component under the app
 * id VERBATIM — it does not resolve it to the .desktop file's Name — so a
 * reverse-DNS id shows up in System Settings → Shortcuts as the literal
 * string "guru.rf.SVXConnect", which is what a user sees when they go looking
 * for the push-to-talk key. Verified: with the id "guru.rf.SVXConnect" the
 * component's friendlyName property stayed "guru.rf.SVXConnect" across a
 * ksycoca rebuild, a component cleanUp and a daemon restart.
 *
 * The cost is the freedesktop convention of reverse-DNS desktop ids, which
 * exists to avoid collisions and is required by Flathub. Flatpak is already
 * rejected for this project (docs/PLAN.md §6.1), and nothing else in Debian
 * ships an "SVXConnect.desktop" — SVXConnect-PIOS installs
 * "svxconnect-gui.desktop". If a Flatpak is ever wanted, this is the one
 * string that has to change back. */
const char *kAppId = "SVXConnect";

const char *kShortcutId = "ptt";

/* The connection name. A DEDICATED connection, not QDBusConnection::sessionBus().
 *
 * Registry.Register must be the first portal call made on its connection, and
 * Qt itself talks to org.freedesktop.portal.Settings during QGuiApplication
 * construction for theme and colour-scheme detection — long before any of this
 * runs. Sharing the session bus would mean Register arrives second and the
 * portal has already decided who we are. */
const char *kConnName = "svxconnect-ptt-portal";

/* The object path the portal will send Request::Response to, which must be
 * subscribed to BEFORE the call is issued or the reply races the subscription.
 * Built from our unique bus name with the leading ':' stripped and '.' -> '_'. */
QString requestPath(const QDBusConnection &conn, const QString &token)
{
    QString sender = conn.baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

QString freshToken(const char *prefix)
{
    return QLatin1String(prefix)
         + QUuid::createUuid().toString(QUuid::Id128).left(8);
}

} // namespace

PortalBackend::PortalBackend(QObject *parent) : PttBackend(parent)
{
    /* Once per process; qDBusRegisterMetaType is idempotent. Doing it in the
     * constructor rather than at static-init time keeps it after
     * QCoreApplication exists, which the D-Bus type system wants. */
    static bool registered = false;
    if (!registered) {
        registered = true;
        qDBusRegisterMetaType<Shortcut>();
        qDBusRegisterMetaType<ShortcutList>();
    }
}

PortalBackend::~PortalBackend()
{
    stop();
    if (m_conn)
        QDBusConnection::disconnectFromBus(QLatin1String(kConnName));
}

PttAvailability PortalBackend::probe() const
{
    PttAvailability a;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        a.state  = PttAvailability::Unavailable;
        a.reason = tr("No session bus.");
        return a;
    }

    QDBusInterface props(QLatin1String(kService), QLatin1String(kPath),
                         QStringLiteral("org.freedesktop.DBus.Properties"), bus);
    const QDBusReply<QVariant> v = props.call(QStringLiteral("Get"),
                                              QLatin1String(kIface),
                                              QStringLiteral("version"));
    if (!v.isValid()) {
        a.state  = PttAvailability::Unavailable;
        a.reason = tr("Your desktop does not provide the global-shortcuts portal. "
                      "wlroots compositors such as sway, labwc and Hyprland have no "
                      "backend for it.");
        a.instructions = tr("Bind a key in your compositor to the control FIFO instead — "
                            "see the scripting section below.");
        return a;
    }

    /* Register needs an installed .desktop whose basename is our app id.
     * Without it the portal refuses and the failure is opaque, so check for
     * the file directly and say what is missing. */
    if (!hasDesktopFile()) {
        a.state  = PttAvailability::NeedsSetup;
        a.reason = tr("SVXConnect is not installed as a desktop application, so the "
                      "portal cannot identify it.");
        a.instructions = tr("Install the package, or copy %1.desktop into "
                            "~/.local/share/applications/.").arg(QLatin1String(kAppId));
        a.hasRelease = true;
        return a;
    }

    a.state      = PttAvailability::Available;
    a.hasRelease = true;
    a.reason     = tr("Ready.");
    return a;
}

bool PortalBackend::hasDesktopFile()
{
    const QString name = QStringLiteral("%1.desktop").arg(QLatin1String(kAppId));

    /* An explicit QStringList rather than a braced initialiser: with
     * QT_USE_QSTRINGBUILDER the concatenated entry is a QStringBuilder
     * expression, not a QString, so the list has no common type to deduce. */
    QStringList dirs;
    dirs << QStringLiteral("/usr/share/applications/")
         << QStringLiteral("/usr/local/share/applications/")
         << QDir::homePath() + QStringLiteral("/.local/share/applications/");

    for (const QString &dir : std::as_const(dirs))
        if (QFile::exists(dir + name))
            return true;

    return false;
}

bool PortalBackend::start(const PttBinding &binding)
{
    if (!binding.isValid())
        return false;

    stop();

    m_requested = binding.trigger;

    /* A dedicated connection — see kConnName. */
    QDBusConnection bus =
        QDBusConnection::connectToBus(QDBusConnection::SessionBus, QLatin1String(kConnName));
    if (!bus.isConnected()) {
        log_err("ptt: could not open a dedicated session bus connection");
        return false;
    }
    m_conn = true;

    /* Registry.Register FIRST, at most once, and never under Flatpak — inside
     * a sandbox the portal already knows the app id from the .flatpak-info
     * file and Register is both unnecessary and an error. */
    if (!m_registered && !QFile::exists(QStringLiteral("/.flatpak-info"))) {
        /* Once per connection. A second attempt fails with "Connection already
         * associated with an application ID", which is correct and harmless
         * but reads like a real fault in the log. */
        m_registered = true;
        QDBusInterface reg(QLatin1String(kService), QLatin1String(kPath),
                           QLatin1String(kRegistry), bus);
        const QDBusMessage r = reg.call(QStringLiteral("Register"),
                                        QLatin1String(kAppId), QVariantMap{});
        if (r.type() == QDBusMessage::ErrorMessage)
            log_warn("ptt: Registry.Register failed: %s",
                     qPrintable(r.errorMessage()));
    }

    /* Subscribe BEFORE calling, or the reply can arrive first. */
    const QString sessionToken = freshToken("svxs");
    const QString sessionReq   = requestPath(bus, sessionToken);

    bus.connect(QString(), sessionReq, QLatin1String(kRequest),
                QStringLiteral("Response"),
                this, SLOT(onCreateSessionResponse(uint, QVariantMap)));

    QDBusInterface gs(QLatin1String(kService), QLatin1String(kPath),
                      QLatin1String(kIface), bus);

    QVariantMap opts;
    opts.insert(QStringLiteral("handle_token"), sessionToken);
    opts.insert(QStringLiteral("session_handle_token"), freshToken("svxsess"));

    const QDBusMessage reply = gs.call(QStringLiteral("CreateSession"), opts);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        log_err("ptt: CreateSession failed: %s", qPrintable(reply.errorMessage()));
        return false;
    }

    return true;
}

void PortalBackend::onCreateSessionResponse(uint code, const QVariantMap &results)
{
    if (code != 0) {
        log_warn("ptt: the portal refused the shortcuts session (code %u)", code);
        emit lost(tr("The desktop refused the global-shortcut session."));
        return;
    }

    m_session = results.value(QStringLiteral("session_handle")).toString();
    if (m_session.isEmpty()) {
        /* Some portal versions return the handle as an object path variant. */
        const QDBusObjectPath p =
            qvariant_cast<QDBusObjectPath>(results.value(QStringLiteral("session_handle")));
        m_session = p.path();
    }
    if (m_session.isEmpty()) {
        emit lost(tr("The desktop returned no shortcuts session."));
        return;
    }

    log_info("ptt: portal session %s", qPrintable(m_session));

    /* Before either branch below. Both need it, and only this point is common
     * to both. */
    subscribeSignals();

    /* ASK BEFORE BINDING.
     *
     * BindShortcuts is a request to (re)configure, and KDE's portal answers it
     * by opening its shortcut editor — EVERY time, not just the first. An
     * application that binds unconditionally at startup therefore throws a
     * settings window in the user's face on every launch.
     *
     * ListShortcuts is the read-only counterpart: it returns what is already
     * registered for this app id with no interaction at all. The shortcut
     * persists across runs (kglobalshortcutsrc on KDE), so on every launch
     * after the first there is nothing to bind and nothing to show. */
    listShortcuts();
}

QString PortalBackend::triggerFrom(const QVariantMap &results)
{
    const QDBusArgument arg = results.value(QStringLiteral("shortcuts")).value<QDBusArgument>();
    ShortcutList list;
    arg >> list;
    for (const auto &s : std::as_const(list))
        if (s.first == QLatin1String(kShortcutId))
            return s.second.value(QStringLiteral("trigger_description")).toString();
    return QString();
}

void PortalBackend::subscribeSignals()
{
    if (m_subscribed)
        return;

    QDBusConnection bus = QDBusConnection(QLatin1String(kConnName));

    /* Activated / Deactivated are what make this a push-to-talk backend rather
     * than a plain shortcut backend: the release is the safety-critical edge.
     * Both are matched on session handle AND shortcut id inside the slots. */
    const bool okAct = bus.connect(QString(), QString(), QLatin1String(kIface),
                QStringLiteral("Activated"),
                this, SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    const bool okDeact = bus.connect(QString(), QString(), QLatin1String(kIface),
                QStringLiteral("Deactivated"),
                this, SLOT(onDeactivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    bus.connect(QString(), QString(), QLatin1String(kIface),
                QStringLiteral("ShortcutsChanged"),
                this, SLOT(onShortcutsChanged(QDBusObjectPath, ShortcutList)));

    if (!okAct || !okDeact) {
        /* A mismatched slot signature makes QDBusConnection::connect() return
         * false and then simply never deliver anything, which is invisible
         * without this line. */
        log_err("ptt: could not subscribe to the portal's key signals "
                "(Activated=%d Deactivated=%d) — the shortcut will do nothing",
                okAct, okDeact);
        return;
    }

    m_subscribed = true;
    log_dbg("ptt: subscribed to portal key signals");
}

void PortalBackend::listShortcuts()
{
    QDBusConnection bus = QDBusConnection(QLatin1String(kConnName));

    const QString token = freshToken("svxl");
    bus.connect(QString(), requestPath(bus, token), QLatin1String(kRequest),
                QStringLiteral("Response"),
                this, SLOT(onListResponse(uint, QVariantMap)));

    QVariantMap opts;
    opts.insert(QStringLiteral("handle_token"), token);

    QDBusMessage call = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
        QStringLiteral("ListShortcuts"));
    call << QVariant::fromValue(QDBusObjectPath(m_session)) << opts;

    const QDBusMessage reply = bus.call(call);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        log_warn("ptt: ListShortcuts failed (%s) — falling back to binding",
                 qPrintable(reply.errorMessage()));
        bindShortcut();
    }
}

void PortalBackend::onListResponse(uint code, const QVariantMap &results)
{
    const QString existing = (code == 0) ? triggerFrom(results) : QString();

    if (!existing.isEmpty()) {
        /* Already registered and assigned. Adopt it silently — no dialog, no
         * BindShortcuts call. Note this is the DESKTOP's binding, which the
         * user may have changed in system settings; that is the one to honour
         * and the one to display. */
        m_active = existing;
        log_info("ptt: portal shortcut already bound to %s", qPrintable(m_active));
        emit triggerChanged(m_active);
        return;
    }

    /* Nothing registered yet, or registered with no key. Bind once — this is
     * the launch where the desktop may legitimately ask the user. */
    log_info("ptt: no portal shortcut registered yet, requesting one");
    bindShortcut();
}

void PortalBackend::bindShortcut()
{
    QDBusConnection bus = QDBusConnection(QLatin1String(kConnName));

    const QString bindToken = freshToken("svxb");
    bus.connect(QString(), requestPath(bus, bindToken), QLatin1String(kRequest),
                QStringLiteral("Response"),
                this, SLOT(onBindResponse(uint, QVariantMap)));

    QVariantMap desc;
    desc.insert(QStringLiteral("description"), tr("Push to talk"));
    desc.insert(QStringLiteral("preferred_trigger"), m_requested);

    ShortcutList shortcuts;
    shortcuts.append(qMakePair(QString::fromLatin1(kShortcutId), desc));

    QVariantMap opts;
    opts.insert(QStringLiteral("handle_token"), bindToken);

    QDBusMessage call = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
        QStringLiteral("BindShortcuts"));
    call << QVariant::fromValue(QDBusObjectPath(m_session))
         << QVariant::fromValue(shortcuts)
         << QString()          /* parent_window */
         << opts;

    const QDBusMessage reply = bus.call(call);
    if (reply.type() == QDBusMessage::ErrorMessage)
        log_err("ptt: BindShortcuts failed: %s", qPrintable(reply.errorMessage()));
}

void PortalBackend::onBindResponse(uint code, const QVariantMap &results)
{
    if (code != 0) {
        emit lost(tr("The desktop refused the push-to-talk shortcut."));
        return;
    }

    const QString bound = triggerFrom(results);

    if (!acceptTrigger(bound))
        return;

    m_active = bound;
    log_info("ptt: portal bound %s", qPrintable(m_active));
    emit triggerChanged(m_active);
}

bool PortalBackend::acceptTrigger(const QString &bound)
{
    /* THE SAFETY CHECK.
     *
     * preferred_trigger is a hint and the portal may bind something else. The
     * dangerous case is not "something else" in general, it is specifically
     * losing the modifiers: asking for CAPS+Return on Plasma 6.3.6 yields a
     * bare "Return", and a bare Return as global push-to-talk means the
     * transmitter keys every time the operator presses Enter in any
     * application. Refuse it rather than hand back a radio with a hair
     * trigger. */
    if (bound.isEmpty()) {
        /* The portal accepted the registration but assigned no key.
         *
         * On KDE this is usually not a rejection of the combination itself.
         * kglobalaccel remembers a shortcut per (app id, shortcut id) FOREVER,
         * including one that was registered badly once — clearing the entry
         * out of kglobalshortcutsrc does not evict the daemon's in-memory
         * copy, so every later attempt comes back unassigned. Observed
         * directly: after a first registration that lost its modifiers, this
         * app id returned an empty trigger on every subsequent run until
         * plasma-kglobalaccel was restarted, after which it bound first time.
         *
         * So the useful advice is "assign it yourself", not "pick another
         * key" — the latter sends the user round the same loop. */
        log_err("ptt: the desktop registered the shortcut but assigned no key "
                "(requested '%s')", qPrintable(m_requested));
        emit lost(tr("Your desktop registered SVXConnect's push-to-talk shortcut but "
                     "did not assign a key to it.\n\n"
                     "Open your desktop's keyboard shortcut settings, find "
                     "SVXConnect, and assign one there. On KDE Plasma that is "
                     "System Settings → Keyboard → Shortcuts."));
        return false;
    }

    const bool wantedModifier = m_requested.contains(QLatin1Char('+'));
    const bool gotModifier    = bound.contains(QLatin1Char('+'));

    if (wantedModifier && !gotModifier) {
        log_err("ptt: refusing '%s' — asked for '%s' but the desktop dropped the "
                "modifiers, which would key the transmitter on a bare key press",
                qPrintable(bound), qPrintable(m_requested));
        emit lost(tr("Your desktop reduced %1 to a single key (%2). SVXConnect will "
                     "not bind that: it would transmit every time you press %2 in "
                     "any application. Choose a different key combination.")
                      .arg(m_requested, bound));
        return false;
    }

    return true;
}

void PortalBackend::onActivated(const QDBusObjectPath &session, const QString &shortcutId,
                                qulonglong timestamp, const QVariantMap &)
{
    Q_UNUSED(timestamp);   /* its epoch is explicitly undefined by the spec */
    if (session.path() != m_session || shortcutId != QLatin1String(kShortcutId))
        return;

    /* Logged, not silent.
     *
     * Without this line a key that arrives and is then refused by the core
     * (no talkgroup, no link, someone else talking) is indistinguishable from
     * a key that never arrived at all — and those have completely different
     * causes. One is a radio state problem, the other is a desktop shortcut
     * problem. Saying "key down" here makes the next question obvious. */
    log_info("ptt: key down (%s)", qPrintable(m_active));
    emit pressed();
}

void PortalBackend::onDeactivated(const QDBusObjectPath &session, const QString &shortcutId,
                                  qulonglong timestamp, const QVariantMap &)
{
    Q_UNUSED(timestamp);
    if (session.path() != m_session || shortcutId != QLatin1String(kShortcutId))
        return;

    log_info("ptt: key up");
    emit released();
}

void PortalBackend::onShortcutsChanged(const QDBusObjectPath &session, const ShortcutList &list)
{
    if (session.path() != m_session)
        return;
    for (const auto &s : list) {
        if (s.first != QLatin1String(kShortcutId))
            continue;
        const QString bound = s.second.value(QStringLiteral("trigger_description")).toString();
        if (bound.isEmpty() || bound == m_active)
            continue;
        m_active = bound;
        log_info("ptt: the desktop reassigned push-to-talk to %s", qPrintable(m_active));
        emit triggerChanged(m_active);
    }
}

void PortalBackend::stop()
{
    if (m_session.isEmpty())
        return;

    QDBusConnection bus = QDBusConnection(QLatin1String(kConnName));
    if (bus.isConnected()) {
        QDBusMessage close = QDBusMessage::createMethodCall(
            QLatin1String(kService), m_session,
            QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
        bus.call(close, QDBus::NoBlock);
    }

    m_session.clear();
    m_active.clear();

    /* m_subscribed is deliberately NOT cleared.
     *
     * The D-Bus match rules belong to the connection, not the session, and the
     * connection outlives both. Calling QDBusConnection::connect() a second
     * time for the same rule returns FALSE — it is already there — which the
     * old code read as failure, logged as "the shortcut will do nothing", and
     * then made true by never marking itself subscribed. Rebinding after a
     * settings change silently killed push-to-talk.
     *
     * Leaving the rules in place is safe: onActivated/onDeactivated match on
     * the CURRENT session handle, so events for a closed session are ignored. */
}
