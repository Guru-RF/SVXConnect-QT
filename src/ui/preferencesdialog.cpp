/* SPDX-License-Identifier: MIT
 * SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
 */
#include "ui/preferencesdialog.h"
#include "core/devlist.h"
#include "ptt/portalbackend.h"
#ifdef SVX_HAVE_EVDEV
#  include "ptt/evdevbackend.h"
#endif
#include <QSettings>
#include <QClipboard>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QProcess>

#include <QTabWidget>
#include <QScrollArea>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QRegularExpressionValidator>
#include <QDoubleValidator>
#include <QGuiApplication>
#include <QSignalBlocker>
#include <QApplication>

namespace {

/* A caption under a control. These carry the explanations that would otherwise
 * only exist in example.conf, which a GUI user never opens. */
QLabel *hint(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    QFont f = l->font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
    l->setFont(f);
    QColor fg = l->palette().color(QPalette::WindowText);
    fg.setAlphaF(0.6f);
    l->setStyleSheet(QStringLiteral("color:%1;").arg(fg.name(QColor::HexArgb)));
    return l;
}

QWidget *scrolled(QWidget *inner)
{
    auto *area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(inner);
    return area;
}

} // namespace

PreferencesDialog::PreferencesDialog(svx_app *app, const QString &configPath, QWidget *parent)
    : QDialog(parent), m_app(app), m_store(app ? app_config(app) : nullptr, configPath)
{
    setWindowTitle(tr("SVXConnect Preferences"));
    resize(620, 540);

    auto *root = new QVBoxLayout(this);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(scrolled(buildConnectionTab()), tr("Connection"));
    tabs->addTab(scrolled(buildAudioTab()),      tr("Audio"));
    tabs->addTab(scrolled(buildTalkgroupsTab()), tr("Talkgroups"));
    tabs->addTab(scrolled(buildPttTab()),        tr("Push-to-talk"));
    tabs->addTab(scrolled(buildGeneralTab()),    tr("General"));
    root->addWidget(tabs, 1);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::onAccept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &PreferencesDialog::onApply);
    root->addWidget(m_buttons);

    load();
}

QWidget *PreferencesDialog::buildConnectionTab()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_callsign = new QLineEdit(page);
    /* Uppercase, and only the characters a callsign can contain. Note '/' is
     * deliberately NOT allowed: a '/'-suffixed callsign only ever arrives FROM
     * the reflector's decoder, it is never something you enrol as. */
    m_callsign->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[A-Za-z0-9-]{0,31}")), m_callsign));
    connect(m_callsign, &QLineEdit::textEdited, this, [this](const QString &t) {
        const int pos = m_callsign->cursorPosition();
        const QSignalBlocker b(m_callsign);
        m_callsign->setText(t.toUpper());
        m_callsign->setCursorPosition(pos);
    });
    form->addRow(tr("Callsign"), m_callsign);

    m_email = new QLineEdit(page);
    form->addRow(tr("Email"), m_email);
    form->addRow(QString(), hint(
        tr("Becomes the certificate request's contact address, so the reflector "
           "sysop can reach you. Required to enrol."), page));

    m_reflector = new QLineEdit(page);
    /* Strip whitespace as it is typed: a hostname pasted out of an email
     * routinely arrives with a trailing space, and the SRV lookup then fails
     * in a way that looks like the reflector is down. */
    connect(m_reflector, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (!t.contains(QLatin1Char(' ')) && !t.contains(QLatin1Char('\t')))
            return;
        const int pos = m_reflector->cursorPosition();
        const QSignalBlocker b(m_reflector);
        m_reflector->setText(t.simplified().remove(QLatin1Char(' ')));
        m_reflector->setCursorPosition(std::max(0, pos - 1));
    });
    form->addRow(tr("Reflector"), m_reflector);

    m_port = new QSpinBox(page);
    m_port->setRange(1, 65535);
    form->addRow(tr("Port"), m_port);
    form->addRow(QString(), hint(
        tr("The host is looked up by SRV record first, so the port is usually "
           "discovered automatically and this is only the fallback."), page));

    auto *qth = new QGroupBox(tr("Station position"), page);
    auto *qthForm = new QFormLayout(qth);

    m_location = new QLineEdit(qth);
    qthForm->addRow(tr("Location"), m_location);

    /* QLocale::c() is not optional. Under a comma locale a QDoubleValidator
     * built from the system locale accepts "51,05", which is then written to
     * the config verbatim and read back by the core as 51. */
    auto *latVal = new QDoubleValidator(-90.0, 90.0, 7, this);
    latVal->setNotation(QDoubleValidator::StandardNotation);
    latVal->setLocale(QLocale::c());
    m_latitude = new QLineEdit(qth);
    m_latitude->setValidator(latVal);
    qthForm->addRow(tr("Latitude"), m_latitude);

    auto *lonVal = new QDoubleValidator(-180.0, 180.0, 7, this);
    lonVal->setNotation(QDoubleValidator::StandardNotation);
    lonVal->setLocale(QLocale::c());
    m_longitude = new QLineEdit(qth);
    m_longitude->setValidator(lonVal);
    qthForm->addRow(tr("Longitude"), m_longitude);

    m_grid = new QLabel(qth);
    qthForm->addRow(tr("Grid square"), m_grid);

    auto recomputeGrid = [this]() {
        bool okLat = false, okLon = false;
        const double la = QLocale::c().toDouble(m_latitude->text(), &okLat);
        const double lo = QLocale::c().toDouble(m_longitude->text(), &okLon);
        if (!okLat || !okLon || (la == 0.0 && lo == 0.0)) {
            m_grid->setText(tr("—"));
            return;
        }
        char g[16] = {0};
        maidenhead(g, sizeof g, la, lo);
        m_grid->setText(QString::fromUtf8(g));
    };
    connect(m_latitude,  &QLineEdit::textChanged, this, recomputeGrid);
    connect(m_longitude, &QLineEdit::textChanged, this, recomputeGrid);

    qthForm->addRow(QString(), hint(
        tr("Leave both at 0 to publish no position at all. The reflector portal "
           "then shows no marker, rather than plotting you off the coast of Ghana."),
        qth));

    form->addRow(qth);
    return page;
}

QWidget *PreferencesDialog::buildAudioTab()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_inputDev = new QComboBox(page);
    m_outputDev = new QComboBox(page);
    form->addRow(tr("Microphone"), m_inputDev);
    form->addRow(tr("Speaker"), m_outputDev);

    m_devWarning = new QLabel(page);
    m_devWarning->setWordWrap(true);
    m_devWarning->setStyleSheet(QStringLiteral("color:#d29922;"));
    m_devWarning->hide();
    form->addRow(QString(), m_devWarning);

    auto *toneRow = new QHBoxLayout;
    auto *tone = new QPushButton(tr("Play test tone"), page);
    connect(tone, &QPushButton::clicked, this, &PreferencesDialog::onTestTone);
    toneRow->addWidget(tone);
    toneRow->addStretch(1);
    form->addRow(QString(), toneRow);
    form->addRow(QString(), hint(
        tr("The test tone forces the volume to at least 50% and opens the playback "
           "gate, so it plays even when the jitter buffer is empty."), page));

    m_volume = new QSpinBox(page);
    m_volume->setRange(0, 100);
    m_volume->setSuffix(tr(" %"));
    form->addRow(tr("Output volume"), m_volume);

    auto *mic = new QGroupBox(tr("Microphone processing"), page);
    auto *micForm = new QFormLayout(mic);

    m_micGain = new QSpinBox(mic);
    m_micGain->setRange(-20, 40);
    m_micGain->setSuffix(tr(" dB"));
    micForm->addRow(tr("Input gain"), m_micGain);

    m_micAgc = new QCheckBox(tr("Automatic gain control"), mic);
    micForm->addRow(QString(), m_micAgc);

    m_micAgcTgt = new QSpinBox(mic);
    m_micAgcTgt->setRange(5, 95);
    m_micAgcTgt->setSuffix(tr(" %"));
    micForm->addRow(tr("AGC target level"), m_micAgcTgt);
    connect(m_micAgc, &QCheckBox::toggled, m_micAgcTgt, &QWidget::setEnabled);

    micForm->addRow(QString(), hint(
        tr("Gain is applied before AGC. If your audio is quiet at the far end, "
           "raise the gain rather than the AGC target."), mic));
    form->addRow(mic);

    auto *net = new QGroupBox(tr("Network audio"), page);
    auto *netForm = new QFormLayout(net);

    m_jitter = new QSpinBox(net);
    m_jitter->setRange(40, 300);
    m_jitter->setSuffix(tr(" ms"));
    netForm->addRow(tr("Jitter buffer"), m_jitter);
    netForm->addRow(QString(), hint(
        tr("How much audio to hold back before playing, to absorb network jitter. "
           "Raise it if speech breaks up on a poor link; lower it to reduce delay."),
        net));

    m_tailTrim = new QSpinBox(net);
    m_tailTrim->setRange(0, 1000);
    m_tailTrim->setSingleStep(50);
    m_tailTrim->setSuffix(tr(" ms"));
    netForm->addRow(tr("Tail trim"), m_tailTrim);
    form->addRow(net);

    auto *beep = new QGroupBox(tr("Roger beep"), page);
    auto *beepForm = new QFormLayout(beep);
    m_rogerBeep = new QCheckBox(tr("Beep when an over ends"), beep);
    beepForm->addRow(QString(), m_rogerBeep);
    m_rogerMin = new QSpinBox(beep);
    m_rogerMin->setRange(0, 60);
    m_rogerMin->setSuffix(tr(" s"));
    beepForm->addRow(tr("Minimum over length"), m_rogerMin);
    connect(m_rogerBeep, &QCheckBox::toggled, m_rogerMin, &QWidget::setEnabled);
    beepForm->addRow(QString(), hint(
        tr("Overs shorter than this do not produce a beep. Your own transmissions "
           "never do."), beep));
    form->addRow(beep);

    return page;
}

QWidget *PreferencesDialog::buildTalkgroupsTab()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_switchable = new QLineEdit(page);
    m_switchable->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9,+ ]*")), m_switchable));
    form->addRow(tr("Switchable"), m_switchable);
    form->addRow(QString(), hint(
        tr("The talkgroups the sidebar cycles through, in this order."), page));

    m_monitored = new QLineEdit(page);
    m_monitored->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9,+ ]*")), m_monitored));
    form->addRow(tr("Monitored"), m_monitored);

    m_tgPreview = new QLabel(page);
    m_tgPreview->setWordWrap(true);
    form->addRow(QString(), m_tgPreview);

    form->addRow(QString(), hint(
        tr("Everything you want to hear. A trailing '+' raises priority: "
           "8 is normal, 8+ is higher, 8++ is highest. A talker on a "
           "higher-priority talkgroup moves you to it automatically."), page));

    auto updatePreview = [this]() {
        svx_tg_entry v[SVX_MAX_TG];
        const int n = tglist_parse(qPrintable(m_monitored->text()), v, SVX_MAX_TG);
        if (n < 0) {
            m_tgPreview->setText(tr("Not a valid list."));
            m_tgPreview->setStyleSheet(QStringLiteral("color:#d13b3b;"));
            return;
        }
        QStringList parts;
        for (int i = 0; i < n; ++i)
            parts << (v[i].priority > 0
                          ? tr("TG %1 (priority %2)").arg(v[i].id).arg(v[i].priority)
                          : tr("TG %1").arg(v[i].id));
        m_tgPreview->setStyleSheet(QString());
        m_tgPreview->setText(parts.join(QStringLiteral(" · ")));
    };
    connect(m_monitored, &QLineEdit::textChanged, this, updatePreview);

    m_defaultTg = new QSpinBox(page);
    m_defaultTg->setRange(0, 999999);
    m_defaultTg->setSpecialValueText(tr("monitor only"));
    form->addRow(tr("Talkgroup at start"), m_defaultTg);

    m_lockOnStart = new QCheckBox(tr("Start locked"), page);
    form->addRow(QString(), m_lockOnStart);

    m_linger = new QSpinBox(page);
    m_linger->setRange(10, 300);
    m_linger->setSuffix(tr(" s"));
    form->addRow(tr("Linger"), m_linger);
    form->addRow(QString(), hint(
        tr("How long a talkgroup stays protected after an over, so a higher-priority "
           "talkgroup cannot pull you out of the gap between two overs of the QSO "
           "you are actually having."), page));

    /* The macOS app hard-codes 60 seconds as a private constant and offers no
     * control at all. The core exposes it, and 0 disables the behaviour. */
    m_idle = new QSpinBox(page);
    m_idle->setRange(0, 3600);
    m_idle->setSuffix(tr(" s"));
    m_idle->setSpecialValueText(tr("never"));
    form->addRow(tr("Drop to monitor after"), m_idle);
    form->addRow(QString(), hint(
        tr("Silence everywhere for this long releases your talkgroup and returns "
           "you to monitoring."), page));

    m_tgOrder = new QComboBox(page);
    m_tgOrder->addItem(tr("By number"), QStringLiteral("numeric"));
    m_tgOrder->addItem(tr("By last heard"), QStringLiteral("lastheard"));
    form->addRow(tr("Sidebar order"), m_tgOrder);

    return page;
}

QWidget *PreferencesDialog::buildPttTab()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_pttMode = new QComboBox(page);
    m_pttMode->addItem(tr("Hold to talk"), QStringLiteral("hold"));
    m_pttMode->addItem(tr("Toggle"),       QStringLiteral("toggle"));
    form->addRow(tr("Mode"), m_pttMode);

    /* ---- keyboard, via the desktop portal ---- */
    auto *kb = new QGroupBox(tr("Keyboard shortcut"), page);
    auto *kbForm = new QFormLayout(kb);

    m_pttPortalStatus = new QLabel(kb);
    m_pttPortalStatus->setWordWrap(true);
    kbForm->addRow(tr("Status"), m_pttPortalStatus);

    /* Read-only, with a button that hands off to the desktop.
     *
     * A text field here was the wrong model. The desktop owns this binding,
     * not us: preferred_trigger is only a hint, the user can reassign the key
     * in system settings at any time, and whatever they choose there is what
     * actually fires. A field you can type into implies SVXConnect decides,
     * then silently disagrees with reality the moment the desktop says
     * otherwise. So: show what is really bound, and send the user to the one
     * place that can change it. */
    m_pttShortcut = new QLabel(kb);
    QFont scFont = m_pttShortcut->font();
    scFont.setBold(true);
    m_pttShortcut->setFont(scFont);
    m_pttShortcut->setTextInteractionFlags(Qt::TextSelectableByMouse);
    kbForm->addRow(tr("Shortcut"), m_pttShortcut);

    auto *btnRow = new QHBoxLayout;
    auto *openSettings = new QPushButton(tr("Configure in System Settings…"), kb);
    connect(openSettings, &QPushButton::clicked, this, [this]() {
        if (!openShortcutSettings())
            QMessageBox::information(this, tr("Configure the shortcut"),
                tr("SVXConnect could not open your desktop's shortcut settings.\n\n"
                   "Open them yourself and look for \"SVXConnect\":\n"
                   "    System Settings → Keyboard → Shortcuts"));
    });
    btnRow->addWidget(openSettings);
    btnRow->addStretch(1);
    kbForm->addRow(QString(), btnRow);

    kbForm->addRow(QString(), hint(
        tr("The shortcut is registered with your desktop, which owns it: it survives "
           "restarts, and changing it there takes effect here immediately.\n\n"
           "SVXConnect must be running for it to work — the shortcut is tied to the "
           "running program, so closing the window stops it."), kb));

    auto *capsNote = new QLabel(kb);
    capsNote->setWordWrap(true);
    capsNote->setStyleSheet(QStringLiteral("color:#d29922;"));
    capsNote->setText(tr(
        "Using CapsLock as the push-to-talk modifier\n"
        "\n"
        "CapsLock is a lock state, not a modifier, so it cannot be bound as one "
        "directly — asking the desktop for CapsLock+Enter binds plain Enter, which "
        "would transmit every time you press Enter in any application. SVXConnect "
        "refuses such a binding.\n"
        "\n"
        "Turn CapsLock into a real modifier instead, once, and it works here and "
        "everywhere else on your desktop:\n"
        "\n"
        "    System Settings → Keyboard → Key Bindings (top right)\n"
        "        → Caps Lock behavior → Make Caps Lock an additional Hyper\n"
        "\n"
        "CapsLock+Enter is then exactly Meta+Return. Hyper and Super share the same "
        "X11 modifier (Mod4), so either choice matches; Hyper is tidier because "
        "almost nothing else uses it, so CapsLock chords will not collide with "
        "existing Meta shortcuts."));
    kbForm->addRow(QString(), capsNote);

    form->addRow(kb);

    /* The input-device (evdev) backend is deliberately NOT offered here.
     *
     * It is the only way to use CapsLock in a chord — at that layer CapsLock
     * is just KEY_CAPSLOCK going down and up — but it costs read access to
     * every key the device produces, which for a keyboard is every keystroke
     * you type. That is the wrong trade to make inside a radio client, and it
     * is the wrong LAYER to solve it at: remapping CapsLock into a real
     * modifier belongs to the desktop, where it benefits every application at
     * once. See docs/PTT.md.
     *
     * The backend itself still builds and PttManager will drive it, so a
     * future release can expose it without re-plumbing anything. */

    /* ---- the fallback that always works ---- */
    auto *fifo = new QGroupBox(tr("Scripting and compositor bindings"), page);
    auto *fifoForm = new QVBoxLayout(fifo);
    auto *fifoDoc = new QPlainTextEdit(fifo);
    fifoDoc->setReadOnly(true);
    fifoDoc->setMaximumHeight(150);
    fifoDoc->setPlainText(tr(
        "The control FIFO works with no setup at all and always reports both edges.\n"
        "\n"
        "  echo \"ptt on\"  > %1\n"
        "  echo \"ptt off\" > %1\n"
        "\n"
        "sway or i3 — --no-repeat is required, or autorepeat re-fires the press:\n"
        "  bindsym --no-repeat           F12 exec sh -c 'echo \"ptt on\"  > %1'\n"
        "  bindsym --no-repeat --release F12 exec sh -c 'echo \"ptt off\" > %1'\n"
        "\n"
        "Other commands: tg <n>|next|prev, lock on|off|toggle, mute <n>,\n"
        "volume 0-100, status, quit").arg(m_store.value(QStringLiteral("ctl_fifo"))));
    fifoForm->addWidget(fifoDoc);
    form->addRow(fifo);

    m_pttNote = new QLabel(page);
    m_pttNote->setWordWrap(true);
    form->addRow(QString(), m_pttNote);

    return page;
}

QWidget *PreferencesDialog::buildGeneralTab()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_txTimeout = new QSpinBox(page);
    m_txTimeout->setRange(0, 3600);
    m_txTimeout->setSuffix(tr(" s"));
    m_txTimeout->setSpecialValueText(tr("no limit"));
    form->addRow(tr("Transmit timeout"), m_txTimeout);
    form->addRow(QString(), hint(
        tr("Hard un-key after this long, so a stuck push-to-talk cannot leave you "
           "transmitting. Setting it to \"no limit\" is strongly discouraged."), page));

    m_logLevel = new QComboBox(page);
    /* The core's enum is "err|warn|info|debug" — note the last is spelled
     * debug, not dbg. config_set() rejects anything else. */
    m_logLevel->addItem(tr("Errors only"), QStringLiteral("err"));
    m_logLevel->addItem(tr("Warnings"),    QStringLiteral("warn"));
    m_logLevel->addItem(tr("Normal"),      QStringLiteral("info"));
    m_logLevel->addItem(tr("Debug"),       QStringLiteral("debug"));
    form->addRow(tr("Log detail"), m_logLevel);

    m_ctlFifo = new QLineEdit(page);
    form->addRow(tr("Control FIFO"), m_ctlFifo);
    form->addRow(QString(), hint(
        tr("A named pipe for scripts, a foot switch, or a compositor key binding:\n"
           "    echo \"ptt on\"  > %1\n"
           "    echo \"ptt off\" > %1\n"
           "Clearing this disables it.").arg(QStringLiteral("<path>")), page));

    return page;
}

/* ---- PTT settings live in QSettings ----
 * The core has no configuration keys for a global hotkey — it could not use
 * one — so these are GUI-only and deliberately not written to the shared
 * svxconnect.conf. */
namespace {
constexpr char kPttMode[]   = "ptt/mode";
constexpr char kPttTrigger[]= "ptt/trigger";
constexpr char kPttDevice[] = "ptt/devicePath";
constexpr char kPttHold[]   = "ptt/holdCode";
constexpr char kPttKey[]    = "ptt/keyCode";
constexpr char kPttGrab[]   = "ptt/grab";
}

bool PreferencesDialog::holdMode()
{
    return QSettings().value(QLatin1String(kPttMode), QStringLiteral("hold")).toString()
           != QLatin1String("toggle");
}

PttBinding PreferencesDialog::keyboardBinding()
{
    QSettings s;
    PttBinding b;
    b.kind    = PttBinding::Keyboard;
    /* Default LOGO+Return — Meta+Enter.
     *
     * Chosen because it composes with the xkb option `caps:super` ("Make Caps
     * Lock an additional Super", in KDE's Keyboard → Advanced settings). With
     * that set, CapsLock+Enter IS Meta+Return, which gives a CapsLock chord
     * for push-to-talk with no /dev/input access, no udev rule and no
     * keylogger-grade permission — and leaves CapsLock working as a modifier
     * for everything else on the desktop too.
     *
     * LOGO is the freedesktop Shortcuts spelling; the portal renders it back
     * as "Meta+Return". SUPER is NOT accepted — tested, it binds nothing. */
    b.trigger = s.value(QLatin1String(kPttTrigger), QStringLiteral("LOGO+Return")).toString();
    return b;
}

PttBinding PreferencesDialog::deviceBinding()
{
    QSettings s;
    PttBinding b;
    b.kind       = PttBinding::Device;
    b.devicePath = s.value(QLatin1String(kPttDevice)).toString();
    b.holdCode   = s.value(QLatin1String(kPttHold), 0).toInt();
    b.keyCode    = s.value(QLatin1String(kPttKey),  0).toInt();
    b.grab       = s.value(QLatin1String(kPttGrab), false).toBool();
    return b;
}

void PreferencesDialog::load()
{
    m_callsign->setText(m_store.value(QStringLiteral("callsign")));
    m_email->setText(m_store.value(QStringLiteral("email")));
    m_reflector->setText(m_store.value(QStringLiteral("reflector")));
    m_port->setValue(m_store.valueInt(QStringLiteral("port")));
    m_location->setText(m_store.value(QStringLiteral("location")));
    m_latitude->setText(m_store.value(QStringLiteral("latitude")));
    m_longitude->setText(m_store.value(QStringLiteral("longitude")));

    m_volume->setValue(m_store.valueInt(QStringLiteral("output_volume_pct")));
    m_micGain->setValue(m_store.valueInt(QStringLiteral("mic_gain")));
    m_micAgc->setChecked(m_store.valueBool(QStringLiteral("mic_agc")));
    m_micAgcTgt->setValue(m_store.valueInt(QStringLiteral("mic_agc_target_pct")));
    m_micAgcTgt->setEnabled(m_micAgc->isChecked());
    m_jitter->setValue(m_store.valueInt(QStringLiteral("jitter_ms")));
    m_tailTrim->setValue(m_store.valueInt(QStringLiteral("tail_trim_ms")));
    m_rogerBeep->setChecked(m_store.valueBool(QStringLiteral("roger_beep")));
    m_rogerMin->setValue(m_store.valueInt(QStringLiteral("roger_beep_min_sec")));
    m_rogerMin->setEnabled(m_rogerBeep->isChecked());

    m_switchable->setText(m_store.value(QStringLiteral("switchable")));
    m_monitored->setText(m_store.value(QStringLiteral("monitored")));
    m_defaultTg->setValue(m_store.valueInt(QStringLiteral("default_tg")));
    m_lockOnStart->setChecked(m_store.valueBool(QStringLiteral("lock_on_start")));
    m_linger->setValue(m_store.valueInt(QStringLiteral("linger_seconds")));
    m_idle->setValue(m_store.valueInt(QStringLiteral("idle_seconds")));
    m_tgOrder->setCurrentIndex(
        m_tgOrder->findData(m_store.value(QStringLiteral("tg_order"))));

    m_txTimeout->setValue(m_store.valueInt(QStringLiteral("tx_timeout_sec")));
    m_logLevel->setCurrentIndex(
        m_logLevel->findData(m_store.value(QStringLiteral("log_level"))));
    m_ctlFifo->setText(m_store.value(QStringLiteral("ctl_fifo")));

    QSettings qs;
    m_pttMode->setCurrentIndex(m_pttMode->findData(
        qs.value(QLatin1String(kPttMode), QStringLiteral("hold")).toString()));

    refreshPttStatus();
    refreshDeviceLists();
}

void PreferencesDialog::refreshPttStatus()
{
    auto render = [](QLabel *l, const PttAvailability &a) {
        QString colour;
        switch (a.state) {
        case PttAvailability::Available:   colour = QStringLiteral("#2ea043"); break;
        case PttAvailability::NeedsSetup:  colour = QStringLiteral("#d29922"); break;
        case PttAvailability::Unavailable: colour = QStringLiteral("#d13b3b"); break;
        }
        QString text = a.reason;
        if (!a.instructions.isEmpty())
            text += QLatin1String("\n") + a.instructions;
        l->setStyleSheet(QStringLiteral("color:%1;").arg(colour));
        l->setText(text);
    };

    PortalBackend portal;
    render(m_pttPortalStatus, portal.probe());

    /* The evdev backend is built but not exposed — see buildPttTab(). Its
     * status is therefore not shown either; there is nothing the user can act
     * on here. */
}

void PreferencesDialog::setCurrentShortcut(const QString &human)
{
    if (!m_pttShortcut)
        return;

    if (human.isEmpty()) {
        m_pttShortcut->setText(tr("not assigned"));
        m_pttShortcut->setStyleSheet(QStringLiteral("color:#d29922;"));
    } else {
        m_pttShortcut->setText(human);
        m_pttShortcut->setStyleSheet(QString());
    }
}

bool PreferencesDialog::openShortcutSettings()
{
    /* The desktop's shortcut editor, best effort and in order of specificity.
     * Passing the app id to KDE's kcm_keys focuses our component directly,
     * which is the difference between "here are your shortcuts" and "here is
     * the one you were looking for". */
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();

    QList<QPair<QString, QStringList>> attempts;

    if (desktop.contains(QLatin1String("kde"))) {
        attempts << qMakePair(QStringLiteral("systemsettings"),
                              QStringList{QStringLiteral("kcm_keys"),
                                          QStringLiteral("--args"),
                                          QStringLiteral("SVXConnect")})
                 << qMakePair(QStringLiteral("kcmshell6"),
                              QStringList{QStringLiteral("kcm_keys")})
                 << qMakePair(QStringLiteral("kcmshell5"),
                              QStringList{QStringLiteral("kcm_keys")});
    } else if (desktop.contains(QLatin1String("gnome"))) {
        attempts << qMakePair(QStringLiteral("gnome-control-center"),
                              QStringList{QStringLiteral("keyboard")});
    }

    /* Whatever the desktop, these are worth a try before giving up. */
    attempts << qMakePair(QStringLiteral("systemsettings"),
                          QStringList{QStringLiteral("kcm_keys")})
             << qMakePair(QStringLiteral("gnome-control-center"),
                          QStringList{QStringLiteral("keyboard")});

    for (const auto &a : std::as_const(attempts)) {
        if (QStandardPaths::findExecutable(a.first).isEmpty())
            continue;
        if (QProcess::startDetached(a.first, a.second)) {
            log_info("ptt: opened %s for shortcut configuration", qPrintable(a.first));
            return true;
        }
    }

    log_warn("ptt: no desktop shortcut editor could be launched");
    return false;
}

void PreferencesDialog::savePtt()
{
    /* Only the mode is ours to store. The shortcut itself belongs to the
     * desktop — it is registered through the portal and edited in system
     * settings — so there is nothing to write back for it here. */
    QSettings qs;
    const QString mode = m_pttMode->currentData().toString();

    const bool changed = qs.value(QLatin1String(kPttMode)).toString() != mode;
    qs.setValue(QLatin1String(kPttMode), mode);

    if (changed)
        emit pttBindingChanged();
}

void PreferencesDialog::onApply()
{
    savePtt();
    commit();
}

void PreferencesDialog::onAccept()
{
    savePtt();
    if (commit())
        accept();
}

void PreferencesDialog::refreshDeviceLists()
{
    if (!m_app)
        return;

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);

    auto fill = [this](QComboBox *box, bool capture, const QString &key) {
        const QString want = m_store.value(key);
        const QSignalBlocker b(box);
        box->clear();
        box->addItem(tr("System default"), QString());
        for (const DevList::Device &d : DevList::list(capture))
            box->addItem(d.name, d.id);

        int idx = box->findData(want);
        if (idx < 0 && !want.isEmpty()) {
            /* The saved device is not present. Show it anyway, marked, so the
             * user can see WHICH device went missing instead of finding the
             * picker silently reset to the default — and so the stale id is
             * not quietly rewritten into the config the CLI also reads. */
            box->addItem(tr("%1 (not connected)").arg(want), want);
            idx = box->count() - 1;
        }
        box->setCurrentIndex(idx < 0 ? 0 : idx);
        return want;
    };

    const QString wantIn  = fill(m_inputDev,  true,  QStringLiteral("input_device"));
    const QString wantOut = fill(m_outputDev, false, QStringLiteral("output_device"));

    QStringList missing;
    if (!wantIn.isEmpty()  && !DevList::resolve(true,  wantIn).matched)  missing << wantIn;
    if (!wantOut.isEmpty() && !DevList::resolve(false, wantOut).matched) missing << wantOut;

    QGuiApplication::restoreOverrideCursor();

    if (missing.isEmpty()) {
        m_devWarning->hide();
    } else {
        m_devWarning->setText(tr("Not currently connected: %1. "
                                 "The system default is being used instead.")
                                  .arg(missing.join(QStringLiteral(", "))));
        m_devWarning->show();
    }
}

bool PreferencesDialog::commit()
{
    m_store.set(QStringLiteral("callsign"),   m_callsign->text().trimmed());
    m_store.set(QStringLiteral("email"),      m_email->text().trimmed());
    m_store.set(QStringLiteral("reflector"),  m_reflector->text().trimmed());
    m_store.setInt(QStringLiteral("port"),    m_port->value());
    m_store.set(QStringLiteral("location"),   m_location->text().trimmed());

    /* C-locale, 7 decimals, matching what the core emits. Anything else and
     * the coordinate the reflector receives is not a number it can read. */
    bool okLat = false, okLon = false;
    const double la = QLocale::c().toDouble(m_latitude->text(), &okLat);
    const double lo = QLocale::c().toDouble(m_longitude->text(), &okLon);
    m_store.set(QStringLiteral("latitude"),  QString::number(okLat ? la : 0.0, 'f', 7));
    m_store.set(QStringLiteral("longitude"), QString::number(okLon ? lo : 0.0, 'f', 7));

    m_store.set(QStringLiteral("input_device"),  m_inputDev->currentData().toString());
    m_store.set(QStringLiteral("output_device"), m_outputDev->currentData().toString());
    m_store.setInt(QStringLiteral("output_volume_pct"), m_volume->value());
    m_store.setInt(QStringLiteral("mic_gain"),           m_micGain->value());
    m_store.setBool(QStringLiteral("mic_agc"),           m_micAgc->isChecked());
    m_store.setInt(QStringLiteral("mic_agc_target_pct"), m_micAgcTgt->value());
    m_store.setInt(QStringLiteral("jitter_ms"),          m_jitter->value());
    m_store.setInt(QStringLiteral("tail_trim_ms"),       m_tailTrim->value());
    m_store.setBool(QStringLiteral("roger_beep"),        m_rogerBeep->isChecked());
    m_store.setInt(QStringLiteral("roger_beep_min_sec"), m_rogerMin->value());

    m_store.set(QStringLiteral("switchable"), m_switchable->text().trimmed());
    m_store.set(QStringLiteral("monitored"),  m_monitored->text().trimmed());
    m_store.setInt(QStringLiteral("default_tg"),     m_defaultTg->value());
    m_store.setBool(QStringLiteral("lock_on_start"), m_lockOnStart->isChecked());
    m_store.setInt(QStringLiteral("linger_seconds"), m_linger->value());
    m_store.setInt(QStringLiteral("idle_seconds"),   m_idle->value());
    m_store.set(QStringLiteral("tg_order"), m_tgOrder->currentData().toString());

    m_store.setInt(QStringLiteral("tx_timeout_sec"), m_txTimeout->value());
    m_store.set(QStringLiteral("log_level"), m_logLevel->currentData().toString());
    m_store.set(QStringLiteral("ctl_fifo"),  m_ctlFifo->text().trimmed());

    if (!m_store.isDirty())
        return true;

    const QStringList changed = m_store.changedKeys();

    QString error;
    if (!m_store.save(&error)) {
        QMessageBox::critical(this, tr("Could not save the configuration"), error);
        return false;
    }

    applyLiveChanges(changed);

    if (changed.contains(QStringLiteral("switchable"))
        || changed.contains(QStringLiteral("monitored")))
        emit talkgroupsChanged();

    /* Everything except the three live-applicable keys needs a restart. */
    QStringList needsRestart = changed;
    needsRestart.removeAll(QStringLiteral("input_device"));
    needsRestart.removeAll(QStringLiteral("output_device"));
    needsRestart.removeAll(QStringLiteral("output_volume_pct"));
    if (!needsRestart.isEmpty())
        emit restartNeeded();

    return true;
}

void PreferencesDialog::applyLiveChanges(const QStringList &changed)
{
    if (!m_app)
        return;

    if (changed.contains(QStringLiteral("output_volume_pct")))
        app_set_volume(m_app, m_volume->value());

    /* Switching a device tears down the whole miniaudio context and takes one
     * to two seconds, on this thread, because the core is not thread-safe.
     * A wait cursor is the honest minimum. */
    const bool inChanged  = changed.contains(QStringLiteral("input_device"));
    const bool outChanged = changed.contains(QStringLiteral("output_device"));

    if (inChanged || outChanged) {
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        m_inputDev->setEnabled(false);
        m_outputDev->setEnabled(false);
        QApplication::processEvents();

        if (inChanged)
            app_set_input_device(m_app, qPrintable(m_inputDev->currentData().toString()));
        if (outChanged)
            app_set_output_device(m_app, qPrintable(m_outputDev->currentData().toString()));

        m_inputDev->setEnabled(true);
        m_outputDev->setEnabled(true);
        QGuiApplication::restoreOverrideCursor();
    }
}

void PreferencesDialog::onTestTone()
{
    /* app_test_tone() forces the volume to at least 50% and opens the playback
     * gate, so it is audible even with an empty jitter buffer. */
    if (m_app)
        app_test_tone(m_app);
}
