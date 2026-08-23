/* SPDX-License-Identifier: MIT
 * SVXConnect-Qt — Copyright (c) 2026 Diëlectricum BV
 */
#include "ui/activitypanel.h"
#include "ui/theme.h"
#include "ui/timefmt.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFontDatabase>
#include <QMouseEvent>
#include <algorithm>
#include <functional>

namespace {

/* A section header: 11 pt semibold, uppercase, with 0.5 px of extra letter
 * spacing — the macOS treatment, which is what makes two stacked lists read as
 * two lists rather than one long one. */
QLabel *sectionHeader(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text.toUpper(), parent);
    QFont f = l->font();
    f.setPointSizeF(std::max(8.0, f.pointSizeF() - 0.5));
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    l->setFont(f);
    QColor fg = l->palette().color(QPalette::WindowText);
    fg.setAlphaF(0.55f);
    l->setStyleSheet(QStringLiteral("color:%1;").arg(fg.name(QColor::HexArgb)));
    return l;
}

QLabel *emptyState(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setAlignment(Qt::AlignCenter);
    QColor fg = l->palette().color(QPalette::WindowText);
    fg.setAlphaF(0.45f);
    l->setStyleSheet(QStringLiteral("color:%1;").arg(fg.name(QColor::HexArgb)));
    l->setContentsMargins(0, 10, 0, 10);
    return l;
}

/* A row that reports clicks without a QListView and a delegate.
 *
 * A plain std::function rather than a Q_OBJECT signal, so the class can live
 * here in the .cpp with no moc pass and no header. AUTOMOC only scans headers
 * and files that #include a moc output, so a Q_OBJECT here would compile and
 * then fail to link on the vtable. */
class ClickableRow : public QWidget {
public:
    ClickableRow(quint32 tg, QWidget *parent) : QWidget(parent), m_tg(tg)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
    }

    quint32 tg() const { return m_tg; }

    std::function<void()> onClicked;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton
            && rect().contains(e->position().toPoint())
            && onClicked)
            onClicked();
        QWidget::mouseReleaseEvent(e);
    }

    void enterEvent(QEnterEvent *) override
    {
        setBackgroundRole(QPalette::AlternateBase);
        setAutoFillBackground(true);
        update();
    }

    void leaveEvent(QEvent *) override
    {
        setAutoFillBackground(false);
        update();
    }

private:
    quint32 m_tg;
};

} // namespace

ActivityPanel::ActivityPanel(svx_app *app, QWidget *parent) : QWidget(parent), m_app(app)
{
    buildUi();
}

void ActivityPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(6);

    /* ---- LOCAL ---- */
    auto *localHead = new QHBoxLayout;
    localHead->setSpacing(6);
    m_localHeader = sectionHeader(tr("Active"), this);
    localHead->addWidget(m_localHeader);

    m_localBadge = new QLabel(this);
    QFont bf = m_localBadge->font();
    bf.setPointSizeF(std::max(7.0, bf.pointSizeF() - 1.0));
    m_localBadge->setFont(bf);
    m_localBadge->setStyleSheet(QStringLiteral(
        "color:%1; background:%2; border-radius:7px; padding:1px 6px;")
        .arg(Theme::connected().name(),
             Theme::wash(Theme::connected(), 40).name(QColor::HexArgb)));
    m_localBadge->hide();
    localHead->addWidget(m_localBadge);
    localHead->addStretch(1);
    root->addLayout(localHead);

    m_localLayout = new QVBoxLayout;
    m_localLayout->setContentsMargins(0, 0, 0, 0);
    m_localLayout->setSpacing(1);
    root->addLayout(m_localLayout);

    m_localEmpty = emptyState(tr("Not connected"), this);
    root->addWidget(m_localEmpty);

    root->addSpacing(8);

    /* ---- RECENT ---- */
    m_recentHeader = sectionHeader(tr("Recent"), this);
    root->addWidget(m_recentHeader);

    m_recentLayout = new QVBoxLayout;
    m_recentLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLayout->setSpacing(1);
    root->addLayout(m_recentLayout);

    m_recentEmpty = emptyState(tr("Nothing heard yet"), this);
    root->addWidget(m_recentEmpty);

    root->addStretch(1);
}

ActivityPanel::Row ActivityPanel::makeRow(const QString &callsign, quint32 tg,
                                          bool live, quint64 stamp)
{
    auto *w = new ClickableRow(tg, this);
    w->onClicked = [this, tg]() { if (tg) emit talkgroupChosen(tg); };

    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(8, 5, 8, 5);
    lay->setSpacing(8);

    /* Live rows get a filled dot, finished rows an outline, so the two states
     * are distinguishable without reading the time column. */
    auto *dot = new QLabel(w);
    dot->setFixedSize(8, 8);
    dot->setStyleSheet(QStringLiteral("border-radius:4px; background:%1;")
        .arg(live ? Theme::connected().name()
                  : Theme::wash(Theme::down(), 140).name(QColor::HexArgb)));
    lay->addWidget(dot);

    auto *call = new QLabel(callsign, w);
    QFont cf = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    cf.setBold(live);
    call->setFont(cf);
    lay->addWidget(call);

    auto *tgLabel = new QLabel(tr("TG %1").arg(tg), w);
    QFont tf = tgLabel->font();
    tf.setPointSizeF(std::max(7.0, tf.pointSizeF() - 1.0));
    tgLabel->setFont(tf);
    tgLabel->setStyleSheet(QStringLiteral(
        "color:%1; background:%2; border-radius:6px; padding:1px 5px;")
        .arg(palette().color(QPalette::WindowText).name(),
             Theme::wash(palette().color(QPalette::WindowText), 22).name(QColor::HexArgb)));
    lay->addWidget(tgLabel);

    lay->addStretch(1);

    auto *time = new QLabel(w);
    time->setFont(tf);
    time->setMinimumWidth(52);
    time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QColor tfg = palette().color(QPalette::WindowText);
    tfg.setAlphaF(0.6f);
    time->setStyleSheet(QStringLiteral("color:%1;").arg(tfg.name(QColor::HexArgb)));
    lay->addWidget(time);

    return Row{w, time, stamp, live};
}

void ActivityPanel::rebuildLocal(quint64 nowMs)
{
    const tg_manager *tgm = app_tgm(m_app);

    QString sig;
    for (int i = 0; i < tgm->n_active; ++i)
        sig += QStringLiteral("%1@%2,").arg(QString::fromUtf8(tgm->active[i].full))
                                        .arg(tgm->active[i].tg);

    if (sig != m_localSig) {
        m_localSig = sig;
        for (const Row &r : std::as_const(m_localRows))
            r.widget->deleteLater();
        m_localRows.clear();

        for (int i = 0; i < tgm->n_active; ++i) {
            const tgm_talker &t = tgm->active[i];
            /* .full, not .call: ON6URE-TPAD and ON6URE-PI are different
             * stations, and a list that renders both as "ON6URE" cannot tell
             * you which one is on the air. */
            Row r = makeRow(QString::fromUtf8(t.full), t.tg, true, t.start_ms);
            m_localLayout->addWidget(r.widget);
            m_localRows.append(r);
        }

        const int n = m_localRows.size();
        m_localBadge->setText(QString::number(n));
        m_localBadge->setVisible(n > 0);

        const bool connected = rc_get_state(app_rc(m_app)) == RC_CONNECTED;
        m_localEmpty->setText(connected ? tr("No active traffic") : tr("Not connected"));
        m_localEmpty->setVisible(n == 0);
    }

    /* Live rows count up, so the time label is refreshed every tick even when
     * the row set did not change. */
    for (const Row &r : std::as_const(m_localRows))
        r.time->setText(TimeFmt::elapsed(r.stamp, nowMs));
}

void ActivityPanel::rebuildRecent(quint64 nowMs)
{
    const tg_manager *tgm = app_tgm(m_app);

    QString sig;
    for (int i = 0; i < tgm->n_recent; ++i)
        sig += QStringLiteral("%1@%2@%3,").arg(QString::fromUtf8(tgm->recent[i].full))
                                           .arg(tgm->recent[i].tg)
                                           .arg(tgm->recent[i].stop_ms);

    if (sig != m_recentSig) {
        m_recentSig = sig;
        for (const Row &r : std::as_const(m_recentRows))
            r.widget->deleteLater();
        m_recentRows.clear();

        for (int i = 0; i < tgm->n_recent; ++i) {
            const tgm_recent &t = tgm->recent[i];
            Row r = makeRow(QString::fromUtf8(t.full), t.tg, false, t.stop_ms);
            m_recentLayout->addWidget(r.widget);
            m_recentRows.append(r);
        }
        m_recentEmpty->setVisible(m_recentRows.isEmpty());
    }

    for (const Row &r : std::as_const(m_recentRows))
        r.time->setText(TimeFmt::ago(r.stamp, nowMs));
}

void ActivityPanel::tickModel(quint64 nowMs)
{
    if (!m_app) return;
    rebuildLocal(nowMs);
    rebuildRecent(nowMs);
}
