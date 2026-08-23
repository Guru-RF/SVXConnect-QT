/* SPDX-License-Identifier: MIT
 * SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
 */
#include "ptt/evdevbackend.h"
#include "core/svxcore.h"

#include <QSocketNotifier>
#include <QDir>
#include <QFileInfo>
#include <QFile>

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <linux/input.h>

namespace {

/* The Linux key codes worth naming in a settings page. Deliberately a short
 * table rather than all 700-odd: these are the keys someone actually binds to
 * push-to-talk, plus the ones a pedal or dongle reports. */
struct KeyName { int code; const char *name; };

const KeyName kKeys[] = {
    { KEY_CAPSLOCK,   "CAPSLOCK"   }, { KEY_ENTER,      "ENTER"      },
    { KEY_KPENTER,    "KPENTER"    }, { KEY_SPACE,      "SPACE"      },
    { KEY_LEFTCTRL,   "LEFTCTRL"   }, { KEY_RIGHTCTRL,  "RIGHTCTRL"  },
    { KEY_LEFTALT,    "LEFTALT"    }, { KEY_RIGHTALT,   "RIGHTALT"   },
    { KEY_LEFTSHIFT,  "LEFTSHIFT"  }, { KEY_RIGHTSHIFT, "RIGHTSHIFT" },
    { KEY_LEFTMETA,   "LEFTMETA"   }, { KEY_RIGHTMETA,  "RIGHTMETA"  },
    { KEY_SCROLLLOCK, "SCROLLLOCK" }, { KEY_NUMLOCK,    "NUMLOCK"    },
    { KEY_PAUSE,      "PAUSE"      }, { KEY_INSERT,     "INSERT"     },
    { KEY_F1,  "F1"  }, { KEY_F2,  "F2"  }, { KEY_F3,  "F3"  }, { KEY_F4,  "F4"  },
    { KEY_F5,  "F5"  }, { KEY_F6,  "F6"  }, { KEY_F7,  "F7"  }, { KEY_F8,  "F8"  },
    { KEY_F9,  "F9"  }, { KEY_F10, "F10" }, { KEY_F11, "F11" }, { KEY_F12, "F12" },
    { KEY_F13, "F13" }, { KEY_F14, "F14" }, { KEY_F15, "F15" }, { KEY_F16, "F16" },
    { BTN_LEFT,   "MOUSE-LEFT"   }, { BTN_RIGHT,  "MOUSE-RIGHT"  },
    { BTN_MIDDLE, "MOUSE-MIDDLE" }, { BTN_SIDE,   "MOUSE-SIDE"   },
    { BTN_EXTRA,  "MOUSE-EXTRA"  },
    { BTN_TRIGGER, "PEDAL-1" }, { BTN_THUMB, "PEDAL-2" }, { BTN_THUMB2, "PEDAL-3" },
    { BTN_JOYSTICK, "JOY-1" }, { BTN_TOP, "JOY-2" },
};

QString sysAttr(const QString &devNode, const char *attr)
{
    /* Walk up from /sys/class/input/eventN to the USB device that owns it. */
    QFileInfo fi(QStringLiteral("/sys/class/input/%1/device")
                     .arg(QFileInfo(devNode).fileName()));
    QDir d(fi.canonicalFilePath());
    for (int i = 0; i < 6 && d.exists(); ++i) {
        QFile f(d.filePath(QLatin1String(attr)));
        if (f.open(QIODevice::ReadOnly))
            return QString::fromLatin1(f.readAll()).trimmed();
        if (!d.cdUp())
            break;
    }
    return QString();
}

} // namespace

EvdevBackend::EvdevBackend(QObject *parent) : PttBackend(parent) {}
EvdevBackend::~EvdevBackend() { stop(); }

QString EvdevBackend::keyName(int code)
{
    for (const KeyName &k : kKeys)
        if (k.code == code)
            return QString::fromLatin1(k.name);
    return QStringLiteral("KEY_%1").arg(code);
}

int EvdevBackend::keyCode(const QString &name)
{
    const QString n = name.trimmed().toUpper();
    for (const KeyName &k : kKeys)
        if (n == QLatin1String(k.name))
            return k.code;
    if (n.startsWith(QLatin1String("KEY_")))
        return n.mid(4).toInt();
    return 0;
}

QVector<EvdevBackend::Candidate> EvdevBackend::enumerate()
{
    QVector<Candidate> out;

    QDir dir(QStringLiteral("/dev/input"));
    const QStringList nodes = dir.entryList({QStringLiteral("event*")}, QDir::System);

    /* Prefer a /dev/input/by-id/ symlink: event numbers are assigned in
     * enumeration order and change when a device is replugged or the machine
     * is rebooted with a different device attached. A binding stored as
     * "event5" silently starts pointing at something else. */
    QDir byId(QStringLiteral("/dev/input/by-id"));
    QHash<QString, QString> stable;
    for (const QString &link : byId.entryList(QDir::System | QDir::NoDotAndDotDot)) {
        const QString full = byId.filePath(link);
        stable.insert(QFileInfo(full).canonicalFilePath(), full);
    }

    for (const QString &node : nodes) {
        Candidate c;
        const QString path = dir.filePath(node);
        c.path     = stable.value(path, path);
        c.readable = (::access(qPrintable(path), R_OK) == 0);

        const int fd = ::open(qPrintable(path), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            char name[256] = {0};
            if (::ioctl(fd, EVIOCGNAME(sizeof name), name) >= 0)
                c.name = QString::fromUtf8(name);

            /* A device that reports KEY_A is a keyboard; that is the
             * distinction that decides whether reading it is a keylogger
             * grant or a single-purpose button. */
            unsigned long bits[(KEY_MAX / (8 * sizeof(unsigned long))) + 1] = {0};
            if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) >= 0) {
                const int idx = KEY_A / (8 * sizeof(unsigned long));
                const int off = KEY_A % (8 * sizeof(unsigned long));
                c.isKeyboard = (bits[idx] >> off) & 1;
            }
            ::close(fd);
        }

        if (c.name.isEmpty())
            c.name = node;

        c.vid = sysAttr(path, "idVendor");
        c.pid = sysAttr(path, "idProduct");
        out.append(c);
    }

    return out;
}

QString EvdevBackend::udevRuleFor(const Candidate &c)
{
    /* TAG+="uaccess" grants an ACL to the user logged in at the ACTIVE seat,
     * and revokes it on switch away. That is materially safer than the
     * `usermod -aG input` advice found all over the internet, which grants
     * every input device on the machine to that user forever. */
    if (!c.vid.isEmpty() && !c.pid.isEmpty()) {
        return QStringLiteral(
            "# %1\n"
            "# Install as /etc/udev/rules.d/70-svxconnect-ptt.rules, then:\n"
            "#   sudo udevadm control --reload-rules && sudo udevadm trigger\n"
            "SUBSYSTEM==\"input\", ATTRS{idVendor}==\"%2\", ATTRS{idProduct}==\"%3\", TAG+=\"uaccess\"\n")
            .arg(c.name, c.vid, c.pid);
    }

    /* No USB ids: a built-in keyboard on the i8042 controller, for instance. */
    return QStringLiteral(
        "# %1\n"
        "# This device has no USB vendor/product id, so the rule has to match\n"
        "# its name. Install as /etc/udev/rules.d/70-svxconnect-ptt.rules:\n"
        "#   sudo udevadm control --reload-rules && sudo udevadm trigger\n"
        "SUBSYSTEM==\"input\", ATTRS{name}==\"%1\", TAG+=\"uaccess\"\n")
        .arg(c.name);
}

PttAvailability EvdevBackend::probe() const
{
    PttAvailability a;
    a.hasRelease = true;   /* EV_KEY value 0 is a genuine release */

    const QVector<Candidate> devices = enumerate();
    int readable = 0;
    for (const Candidate &c : devices)
        if (c.readable)
            ++readable;

    if (devices.isEmpty()) {
        a.state  = PttAvailability::Unavailable;
        a.reason = tr("No input devices found under /dev/input.");
        return a;
    }

    if (readable == 0) {
        a.state  = PttAvailability::NeedsSetup;
        a.reason = tr("None of the %1 input devices are readable by you.")
                       .arg(devices.size());
        a.instructions = tr("Choose the device you want to use and install the udev "
                            "rule SVXConnect generates for it. Do not add yourself to "
                            "the 'input' group — that grants every input device on the "
                            "machine, permanently.");
        return a;
    }

    a.state  = PttAvailability::Available;
    a.reason = tr("%1 of %2 input devices are readable.").arg(readable).arg(devices.size());
    return a;
}

bool EvdevBackend::start(const PttBinding &binding)
{
    if (binding.kind != PttBinding::Device || !binding.isValid())
        return false;

    stop();
    m_binding = binding;

    m_fd = ::open(qPrintable(binding.devicePath), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        log_err("ptt: cannot open %s: %s",
                qPrintable(binding.devicePath), std::strerror(errno));
        return false;
    }

    /* EVIOCGRAB takes the device exclusively, so the key never reaches any
     * other application. Correct for a dedicated pedal; catastrophic for a
     * keyboard, where it makes the whole desktop appear frozen. The settings
     * page only offers it for non-keyboard devices. */
    if (binding.grab) {
        if (::ioctl(m_fd, EVIOCGRAB, 1) == 0)
            m_grabbed = true;
        else
            log_warn("ptt: could not grab %s exclusively: %s",
                     qPrintable(binding.devicePath), std::strerror(errno));
    }

    m_notify = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notify, &QSocketNotifier::activated, this, &EvdevBackend::onReadable);

    m_holdDown = m_keyDown = m_keyed = false;

    log_info("ptt: watching %s for %s", qPrintable(binding.devicePath),
             qPrintable(activeTrigger()));
    return true;
}

QString EvdevBackend::activeTrigger() const
{
    if (!m_binding.isValid())
        return QString();
    if (m_binding.holdCode)
        return QStringLiteral("%1 + %2").arg(keyName(m_binding.holdCode),
                                             keyName(m_binding.keyCode));
    return keyName(m_binding.keyCode);
}

void EvdevBackend::onReadable()
{
    struct input_event ev[32];

    for (;;) {
        const ssize_t n = ::read(m_fd, ev, sizeof ev);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR)
                return;
            /* ENODEV means unplugged mid-transmission. Report it: PttManager
             * unkeys on lost(), because a transmitter whose control has just
             * disappeared must not stay on the air. */
            log_warn("ptt: %s read failed: %s",
                     qPrintable(m_binding.devicePath), std::strerror(errno));
            closeDevice();
            emit lost(tr("The input device was disconnected."));
            return;
        }
        if (n == 0)
            return;

        const int count = static_cast<int>(n / sizeof(struct input_event));
        for (int i = 0; i < count; ++i) {
            /* SYN_DROPPED means the kernel's buffer overflowed and events
             * were discarded, so the key states we are tracking may now be
             * wrong. The only safe response is to assume nothing: drop back
             * to un-keyed rather than risk a stuck transmitter built on a
             * press whose release we never saw. */
            if (ev[i].type == EV_SYN && ev[i].code == SYN_DROPPED) {
                log_warn("ptt: input events were dropped; resynchronising");
                m_holdDown = m_keyDown = false;
                if (m_keyed) { m_keyed = false; emit released(); }
                continue;
            }

            if (ev[i].type != EV_KEY)
                continue;

            /* value 2 is autorepeat. Not filtering it is the classic
             * hold-to-talk bug: the key chatters press/press/press while held
             * and any edge-triggered logic falls apart. */
            if (ev[i].value == 2)
                continue;

            const bool down = (ev[i].value == 1);

            if (m_binding.holdCode && ev[i].code == m_binding.holdCode)
                m_holdDown = down;
            else if (ev[i].code == m_binding.keyCode)
                m_keyDown = down;
            else
                continue;

            const bool want = m_keyDown && (!m_binding.holdCode || m_holdDown);
            if (want == m_keyed)
                continue;

            m_keyed = want;
            if (want) emit pressed();
            else      emit released();
        }
    }
}

void EvdevBackend::closeDevice()
{
    if (m_notify) {
        m_notify->setEnabled(false);
        m_notify->deleteLater();
        m_notify = nullptr;
    }
    if (m_fd >= 0) {
        if (m_grabbed)
            ::ioctl(m_fd, EVIOCGRAB, 0);
        ::close(m_fd);
        m_fd = -1;
    }
    m_grabbed = false;
}

void EvdevBackend::stop()
{
    /* Never leave the radio keyed because the backend went away. */
    if (m_keyed) {
        m_keyed = false;
        emit released();
    }
    m_holdDown = m_keyDown = false;
    closeDevice();
}
