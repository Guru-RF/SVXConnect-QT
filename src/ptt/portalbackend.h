/* SPDX-License-Identifier: MIT
 * SVXConnect-Qt — Copyright (c) 2026 Diëlectricum BV
 *
 * Global PTT via org.freedesktop.portal.GlobalShortcuts.
 *
 * The default backend: it works on Wayland and on X11, needs no elevated
 * permission and no group membership, survives the compositor reassigning the
 * key, and — the part that matters — emits Deactivated as well as Activated,
 * so it can drive a real hold-to-talk rather than a toggle.
 *
 * Verified on Debian 13 / Plasma 6.3.6 / Wayland with xdg-desktop-portal
 * 1.20.3 and xdg-desktop-portal-kde 6.3.5: interface version 1, both signals
 * present, no permission dialog, binding persisted to kglobalshortcutsrc.
 *
 * Three ordering rules, each of which fails opaquely when broken:
 *
 *   1. Registry.Register must be the FIRST portal call on its connection, so
 *      this uses its own QDBusConnection — Qt already talks to the Settings
 *      portal during QGuiApplication construction.
 *   2. The Request::Response subscription must exist BEFORE the call that
 *      generates it, or the reply races the subscription.
 *   3. BindShortcuts may be called once per session. Rebinding means closing
 *      the session and making a new one.
 */
#ifndef SVXCONNECT_QT_PORTALBACKEND_H
#define SVXCONNECT_QT_PORTALBACKEND_H

#include "ptt/pttbackend.h"

#include <QDBusObjectPath>
#include <QVariantMap>
#include <QList>
#include <QPair>
#include <QDir>
#include <QMetaType>

/* The portal's a(sa{sv}) — an array of (shortcut id, properties) structs.
 *
 * QtDBus cannot marshal this on its own: it has no idea that the pair is a
 * D-Bus STRUCT rather than two separate arguments, so BindShortcuts fails at
 * marshalling time with "Unregistered type". The stream operators below supply
 * that, and qDBusRegisterMetaType() must be called once before the first call
 * that uses them. */
using Shortcut     = QPair<QString, QVariantMap>;
using ShortcutList = QList<Shortcut>;

Q_DECLARE_METATYPE(Shortcut)
Q_DECLARE_METATYPE(ShortcutList)

class QDBusArgument;
QDBusArgument &operator<<(QDBusArgument &arg, const Shortcut &s);
const QDBusArgument &operator>>(const QDBusArgument &arg, Shortcut &s);

class PortalBackend : public PttBackend {
    Q_OBJECT

public:
    explicit PortalBackend(QObject *parent = nullptr);
    ~PortalBackend() override;

    QString id() const override          { return QStringLiteral("portal"); }
    QString displayName() const override { return tr("Desktop portal"); }

    PttAvailability probe() const override;
    bool start(const PttBinding &binding) override;
    void stop() override;

    QString activeTrigger() const override { return m_active; }

    /* Is an appropriately-named .desktop installed? Registry.Register needs
     * one and fails opaquely without it. Public so the settings page can
     * explain the situation before anything is attempted. */
    static bool hasDesktopFile();

private slots:
    void onCreateSessionResponse(uint code, const QVariantMap &results);
    void onListResponse(uint code, const QVariantMap &results);
    void onBindResponse(uint code, const QVariantMap &results);
    void onActivated(const QDBusObjectPath &session, const QString &shortcutId,
                     qulonglong timestamp, const QVariantMap &options);
    void onDeactivated(const QDBusObjectPath &session, const QString &shortcutId,
                       qulonglong timestamp, const QVariantMap &options);
    void onShortcutsChanged(const QDBusObjectPath &session, const ShortcutList &shortcuts);

private:
    /* Subscribe to Activated / Deactivated / ShortcutsChanged.
     *
     * MUST run on every start, on BOTH paths — adopting an existing binding
     * and creating a new one. Having this inside bindShortcut() was a real
     * regression: once the "already bound" path started returning early to
     * avoid KDE's configuration dialog, no subscription was ever made and the
     * shortcut fired into nothing. The binding looked perfect in the logs and
     * in kglobalshortcutsrc, and the key did nothing. */
    void subscribeSignals();

    void listShortcuts();
    void bindShortcut();

    /* Pull our shortcut's trigger out of a portal a(sa{sv}) reply. Empty when
     * the shortcut is absent or registered with no key assigned. */
    static QString triggerFrom(const QVariantMap &results);

    /* Reject a binding that lost its modifiers. See the long comment in
     * pttbackend.h: CAPS+Return comes back as bare "Return" on Plasma, and
     * binding that would transmit on every Enter keypress anywhere. */
    bool acceptTrigger(const QString &bound);

    bool    m_conn = false;
    bool    m_registered = false;
    bool    m_subscribed = false;
    QString m_session;
    QString m_requested;
    QString m_active;
};

#endif
