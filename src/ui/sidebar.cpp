/* SPDX-License-Identifier: MIT
 * SVXConnect-Qt — Copyright (c) 2026 Diëlectricum BV
 */
#include "ui/sidebar.h"
#include "ui/talkgroupbutton.h"
#include "ui/levelmeter.h"
#include "ui/theme.h"
#include "ui/timefmt.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QSlider>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QFontDatabase>
#include <algorithm>

namespace {
constexpr int kWidth = 200;
} // namespace

Sidebar::Sidebar(svx_app *app, QWidget *parent) : QWidget(parent), m_app(app)
{
    setFixedWidth(kWidth);
    buildUi();
    rebuildTalkgroups();
}

void Sidebar::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    /* ---- which talkgroup, and the lock ---- */
    auto *tgRow = new QHBoxLayout;
    tgRow->setSpacing(6);

    m_activeTg = new QLabel(this);
    QFont big = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    big.setPointSizeF(big.pointSizeF() + 5);
    big.setBold(true);
    m_activeTg->setFont(big);
    tgRow->addWidget(m_activeTg);

    tgRow->addStretch(1);

    m_lock = new QToolButton(this);
    m_lock->setCheckable(true);
    m_lock->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(m_lock, &QToolButton::clicked, this, [this]() {
        if (m_app) app_toggle_lock(m_app);
    });
    tgRow->addWidget(m_lock);
    root->addLayout(tgRow);

    applyLockVisuals(false);

    /* Free-form label a sysop can attach to a talkgroup. Populated from the
     * portal's talkgroups.json once the Map/portal fetcher exists (beyond v1);
     * the widget ships now so the layout does not shift when it arrives. */
    m_tgInfo = new QLabel(this);
    m_tgInfo->setWordWrap(true);
    QFont small = font();
    small.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.0));
    m_tgInfo->setFont(small);
    m_tgInfo->hide();
    root->addWidget(m_tgInfo);

    /* The preemption notice. The core gives this for free and does it better
     * than the macOS app: tgm_preempt_banner() returns the talkgroup you were
     * moved AWAY from and expires itself after five seconds, where the Swift
     * version kept a non-expiring `preemptedBy`. */
    m_preempt = new QLabel(this);
    m_preempt->setWordWrap(true);
    m_preempt->setFont(small);
    m_preempt->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::busy().name()));
    m_preempt->hide();
    root->addWidget(m_preempt);

    auto *hint = new QLabel(tr("Talkgroups (right-click to mute)"), this);
    hint->setFont(small);
    QColor hintFg = palette().color(QPalette::WindowText);
    hintFg.setAlphaF(0.6f);
    hint->setStyleSheet(QStringLiteral("color:%1;").arg(hintFg.name(QColor::HexArgb)));
    root->addWidget(hint);

    /* ---- the talkgroup list ---- */
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_tgHost = new QWidget(scroll);
    m_tgLayout = new QVBoxLayout(m_tgHost);
    m_tgLayout->setContentsMargins(0, 0, 0, 0);
    m_tgLayout->setSpacing(2);
    m_tgLayout->addStretch(1);
    scroll->setWidget(m_tgHost);
    root->addWidget(scroll, 1);

    /* ---- meters ---- */
    auto *meterGrid = new QVBoxLayout;
    meterGrid->setSpacing(4);

    auto meterRow = [this, &small](const QString &caption, LevelMeter **out) {
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        auto *cap = new QLabel(caption, this);
        cap->setFont(small);
        cap->setFixedWidth(26);
        row->addWidget(cap);
        *out = new LevelMeter(this);
        row->addWidget(*out, 1);
        return row;
    };
    meterGrid->addLayout(meterRow(tr("MIC"), &m_micMeter));
    meterGrid->addLayout(meterRow(tr("SPK"), &m_spkMeter));
    root->addLayout(meterGrid);

    /* ---- volume ---- */
    auto *volRow = new QHBoxLayout;
    volRow->setSpacing(6);

    m_mute = new QToolButton(this);
    m_mute->setCheckable(true);
    m_mute->setText(tr("🔇"));
    m_mute->setToolTip(tr("Mute output"));
    connect(m_mute, &QToolButton::clicked, this, [this]() {
        if (m_app) app_toggle_output_mute(m_app);
    });
    volRow->addWidget(m_mute);

    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setRange(0, 100);
    m_volume->setValue(m_app ? app_volume(m_app) : 80);
    m_volume->setToolTip(tr("Output volume"));
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        if (m_app) app_set_volume(m_app, v);
    });
    volRow->addWidget(m_volume, 1);
    root->addLayout(volRow);
}

void Sidebar::applyLockVisuals(bool locked)
{
    /* Text that names the STATE, not the action.
     *
     * "Lock" on a button that is already locked reads as "click to lock",
     * which is exactly backwards and is what made this look broken: the core
     * was locking every time, the label just never said so. */
    m_lock->setText(locked ? tr("Locked") : tr("Lock"));

    /* An explicit amber background, because a checked QToolButton is too
     * subtle to carry a state that changes whether the radio moves under you.
     * Amber rather than the theme's blue Highlight: blue reads as "selected",
     * which is what every other checked control on the desktop means, whereas
     * this is a hold that changes the radio's behaviour. It is the same amber
     * the status dot uses for "not in the normal state". */
    m_lock->setStyleSheet(locked
        ? QStringLiteral("QToolButton { background:%1; color:white; font-weight:bold;"
                         " border:none; border-radius:4px; padding:3px 8px; }")
              .arg(Theme::busy().name())
        : QStringLiteral("QToolButton { padding:3px 8px; }"));

    m_lock->setToolTip(locked
        ? tr("Locked to this talkgroup. Nothing will move you automatically — "
             "click to unlock.")
        : tr("Click to lock. While locked, a talker on a higher-priority "
             "talkgroup will not pull you away."));
}

void Sidebar::rebuildTalkgroups()
{
    if (!m_app) return;
    const svx_config *cfg = app_config(m_app);

    /* Build a signature of the switchable set. Cheap to compute, and it means
     * this function is safe to call from anywhere without a "did it change?"
     * check at every call site. */
    QString sig;
    for (int i = 0; i < cfg->n_switchable; ++i)
        sig += QStringLiteral("%1:%2,")
                   .arg(cfg->switchable[i].id).arg(cfg->switchable[i].priority);
    if (sig == m_tgSignature)
        return;
    m_tgSignature = sig;

    for (auto *b : std::as_const(m_buttons))
        b->deleteLater();
    m_buttons.clear();

    for (int i = 0; i < cfg->n_switchable; ++i) {
        const uint32_t tg = cfg->switchable[i].id;

        /* Priority comes from config_tg_priority(), not from the switchable
         * entry's own suffix: the monitored list wins where both name a
         * talkgroup, and that is the priority the state machine actually acts
         * on. Showing the other one would put stars on a row that does not
         * behave like a priority row. */
        auto *b = new TalkgroupButton(tg, config_tg_priority(cfg, tg), m_tgHost);
        connect(b, &TalkgroupButton::clicked, this, [this, tg]() {
            if (m_app) app_tg_select(m_app, tg);
        });
        connect(b, &TalkgroupButton::muteRequested, this, [this](quint32 t) {
            if (m_app) app_toggle_mute(m_app, t);
        });
        m_tgLayout->insertWidget(m_tgLayout->count() - 1, b);
        m_buttons.append(b);
    }
}

void Sidebar::tickModel(quint64 nowMs)
{
    if (!m_app) return;

    tg_manager *tgm = app_tgm(m_app);
    const rc_state st = rc_get_state(app_rc(m_app));
    const uint32_t sel = tgm_selected(tgm);

    /* Four states for the headline, matching what the core can actually tell
     * us: no link at all, linked but monitor-only, or a talkgroup. */
    if (st != RC_CONNECTED)
        m_activeTg->setText(QStringLiteral("—"));
    else if (sel == 0)
        m_activeTg->setText(tr("Monitor"));
    else
        m_activeTg->setText(tr("TG %1").arg(sel));

    const bool locked = tgm_locked(tgm) != 0;
    if (!m_lockInit || locked != m_lockState) {
        m_lockInit  = true;
        m_lockState = locked;
        const QSignalBlocker blocker(m_lock);
        m_lock->setChecked(locked);
        applyLockVisuals(locked);
    }

    const uint32_t from = tgm_preempt_banner(tgm, nowMs);
    if (from) {
        m_preempt->setText(tr("Moved from TG %1").arg(from));
        m_preempt->show();
    } else {
        m_preempt->hide();
    }

    for (auto *b : std::as_const(m_buttons)) {
        const uint32_t tg = b->talkgroup();
        b->setChecked(tg == sel);
        b->setMuted(tgm_is_muted(tgm, tg) != 0);

        const tgm_talker *t = tgm_talker_on(tgm, tg);
        b->setTalker(t ? QString::fromUtf8(t->full) : QString());
        b->setLastHeard(tgm_last_heard(tgm, tg));
        b->refreshAge(nowMs);
    }

    if (!m_volume->isSliderDown() && m_volume->value() != app_volume(m_app)) {
        const QSignalBlocker blocker(m_volume);
        m_volume->setValue(app_volume(m_app));
    }
    {
        const QSignalBlocker blocker(m_mute);
        m_mute->setChecked(app_output_muted(m_app) != 0);
    }
}

void Sidebar::tickMeters()
{
    if (!m_app) return;

    /* app_mic_level() holds its last peak after capture stops, so gate it on
     * transmit; the speaker meter must read zero while muted rather than
     * showing what would have played. */
    const float mic = app_tx_active(m_app)    ? app_mic_level(m_app) : 0.0f;
    const float spk = app_output_muted(m_app) ? 0.0f : app_spk_level(m_app);

    auto ballistic = [](float in, float &vu) {
        vu = (in > vu) ? in : vu * 0.75f;
        if (vu < 0.001f) vu = 0.0f;
        return vu;
    };

    m_micMeter->setLevel(ballistic(mic, m_micVu));
    m_spkMeter->setLevel(ballistic(spk, m_spkVu));
}
