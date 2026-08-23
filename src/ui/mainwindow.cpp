/* SPDX-License-Identifier: MIT
 * SVXConnect-Debian — Copyright (c) 2026 Diëlectricum BV
 */
#include "ui/mainwindow.h"
#include "ui/statusbar.h"
#include "ui/sidebar.h"
#include "ui/activitypanel.h"
#include "ui/theme.h"
#include "ui/configfile.h"
#include "ui/preferencesdialog.h"
#include "ptt/pttmanager.h"
#include "ui/trayicon.h"
#include "core/logbridge.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTimer>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QKeyEvent>
#include <QScrollBar>
#include <QFrame>
#include <QFontDatabase>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QSettings>
#include <QCloseEvent>
#include <QSystemTrayIcon>
#include <QFileSystemWatcher>
#include <QFileInfo>

MainWindow::MainWindow(svx_app *app, QWidget *parent)
    : QMainWindow(parent), m_app(app)
{
    setWindowTitle(QStringLiteral("SVXConnect"));
    buildUi();
    buildMenus();

    /* Global push-to-talk. Constructed even without a core so the settings
     * page can probe backends in SVX_WINDOW_ONLY mode; the manager simply
     * never calls app_ptt() when m_app is null. */
    m_pttManager = new PttManager(m_app, this);
    connect(m_pttManager, &PttManager::backendLost, this, [this](const QString &id, const QString &why) {
        m_banner->setText(tr("Push-to-talk (%1) stopped working: %2").arg(id, why));
        m_banner->show();
    });
    applyPttBindings();

    /* The tray, and with it the ability to close the window without killing
     * the push-to-talk shortcut. */
    m_tray = new TrayIcon(m_app, this);
    connect(m_tray, &TrayIcon::showWindowRequested, this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });
    connect(m_tray, &TrayIcon::quitRequested, this, [this]() {
        m_reallyQuit = true;
        qApp->quit();
    });

    /* Only decouple the lifetime from the window if there is somewhere to
     * bring it back from. With no tray, hiding the window would strand the
     * process with no interface and no way to quit it. */
    qApp->setQuitOnLastWindowClosed(!m_tray->isAvailable());

    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    if (geometry().isEmpty() || !s.contains(QStringLiteral("window/geometry")))
        resize(760, 520);

    m_modelTick = new QTimer(this);
    m_modelTick->setInterval(100);
    connect(m_modelTick, &QTimer::timeout, this, &MainWindow::tickModel);
    m_modelTick->start();

    m_meterTick = new QTimer(this);
    m_meterTick->setInterval(33);
    connect(m_meterTick, &QTimer::timeout, this, &MainWindow::tickMeters);
    m_meterTick->start();

    tickModel();
}

MainWindow::~MainWindow()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    if (m_body)
        s.setValue(QStringLiteral("window/splitter"), m_body->saveState());
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    /* ---- status row ---- */
    m_status = new ConnectionBar(m_app, central);
    root->addWidget(m_status);

    auto *rule = new QFrame(central);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Plain);
    root->addWidget(rule);

    /* ---- banner ----
     * The core raises these for things the operator must see: a silent
     * microphone, a certificate about to expire, a refused transmit. It is
     * hidden when there is nothing to say rather than reserving empty space. */
    m_banner = new QLabel(central);
    m_banner->setWordWrap(true);
    m_banner->setContentsMargins(12, 6, 12, 6);
    m_banner->setStyleSheet(QStringLiteral("color:%1; background:%2;")
        .arg(Theme::busy().name(), Theme::wash(Theme::busy(), 30).name(QColor::HexArgb)));
    m_banner->hide();
    m_banner->setCursor(Qt::PointingHandCursor);
    m_banner->setToolTip(tr("Click to dismiss"));
    m_banner->installEventFilter(this);
    root->addWidget(m_banner);

    /* Shown when svxconnect.conf changes on disk while we are running.
     *
     * Nothing is reloaded in place, and that is deliberate. app_new() keeps
     * the svx_config POINTER and the talkgroup manager holds indices into its
     * lists, so rewriting those fields underneath a live connection would be a
     * use-after-free waiting for the next talker. A restart is cheap and
     * provably correct, so the bar offers exactly that.
     *
     * A real button, not a clickable bar: restarting drops the reflector
     * connection, and an action with that consequence should not be something
     * you can trigger by clicking a notification you were trying to dismiss. */
    m_reload = new QWidget(central);
    m_reload->setAutoFillBackground(true);
    {
        QPalette pal = m_reload->palette();
        pal.setColor(QPalette::Window, Theme::wash(Theme::connected(), 30));
        m_reload->setPalette(pal);
    }

    auto *reloadLay = new QHBoxLayout(m_reload);
    reloadLay->setContentsMargins(12, 6, 12, 6);
    reloadLay->setSpacing(8);

    auto *reloadText = new QLabel(
        tr("Configuration changed. Restart to apply it."), m_reload);
    reloadText->setWordWrap(true);
    reloadText->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::connected().name()));
    reloadLay->addWidget(reloadText, 1);

    auto *restartBtn = new QPushButton(tr("Restart now"), m_reload);
    restartBtn->setCursor(Qt::PointingHandCursor);
    restartBtn->setToolTip(tr("Quit and start again, applying the new configuration"));
    connect(restartBtn, &QPushButton::clicked, this, &MainWindow::onRestartRequested);
    reloadLay->addWidget(restartBtn);

    auto *laterBtn = new QPushButton(tr("Later"), m_reload);
    laterBtn->setFlat(true);
    laterBtn->setCursor(Qt::PointingHandCursor);
    connect(laterBtn, &QPushButton::clicked, m_reload, &QWidget::hide);
    reloadLay->addWidget(laterBtn);

    m_reload->hide();
    root->addWidget(m_reload);

    /* ---- body: sidebar | activity ---- */
    auto *body = new QWidget(central);
    auto *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);

    m_sidebar = new Sidebar(m_app, body);
    bodyLay->addWidget(m_sidebar);

    auto *vrule = new QFrame(body);
    vrule->setFrameShape(QFrame::VLine);
    vrule->setFrameShadow(QFrame::Plain);
    bodyLay->addWidget(vrule);

    m_activity = new ActivityPanel(m_app, body);
    connect(m_activity, &ActivityPanel::talkgroupChosen, this, [this](quint32 tg) {
        if (m_app) app_tg_select(m_app, tg);
    });
    bodyLay->addWidget(m_activity, 1);

    /* ---- PTT ----
     * Hold-to-talk on the mouse: pressed() keys, released() unkeys. A
     * deliberate divergence from the macOS app, where both the window button
     * and the menu-bar button call togglePTT(). A genuine hold is the thing a
     * pointer can offer and a terminal cannot, and it is the same gesture the
     * global hotkey will use in M4, so the two match.
     *
     * The core owns every refusal: no link, no talkgroup, or somebody else
     * already talking each produce a distinct beep pattern and no carrier. */
    m_ptt = new QPushButton(tr("PUSH TO TALK"), central);
    m_ptt->setMinimumHeight(54);
    m_ptt->setFocusPolicy(Qt::NoFocus);
    QFont pf = m_ptt->font();
    pf.setBold(true);
    pf.setPointSizeF(pf.pointSizeF() + 2);
    m_ptt->setFont(pf);
    m_ptt->setToolTip(tr("Hold to transmit. Space toggles, Escape stops."));
    connect(m_ptt, &QPushButton::pressed,  this, &MainWindow::onPttPressed);
    connect(m_ptt, &QPushButton::released, this, &MainWindow::onPttReleased);

    /* ---- log ---- */
    m_log = new QPlainTextEdit(central);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setFrameShape(QFrame::NoFrame);
    m_log->hide();

    m_body = new QSplitter(Qt::Vertical, central);
    m_body->addWidget(body);
    m_body->addWidget(m_log);
    m_body->setStretchFactor(0, 1);
    m_body->setStretchFactor(1, 0);
    m_body->setChildrenCollapsible(false);
    root->addWidget(m_body, 1);

    auto *pttWrap = new QWidget(central);
    auto *pttLay = new QHBoxLayout(pttWrap);
    pttLay->setContentsMargins(12, 8, 12, 12);
    pttLay->addWidget(m_ptt);
    root->addWidget(pttWrap);

    setCentralWidget(central);
    refreshPttButton();
}

void MainWindow::buildMenus()
{
    auto *file = menuBar()->addMenu(tr("&File"));

    QAction *prefs = file->addAction(tr("&Preferences…"));
    prefs->setShortcut(QKeySequence::Preferences);   /* Ctrl+, */
    prefs->setMenuRole(QAction::PreferencesRole);
    connect(prefs, &QAction::triggered, this, &MainWindow::onPreferences);

    QAction *editConf = file->addAction(tr("&Edit Configuration…"));
    editConf->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    editConf->setStatusTip(tr("Open svxconnect.conf in your text editor"));
    connect(editConf, &QAction::triggered, this, &MainWindow::onEditConfig);

    QAction *confDir = file->addAction(tr("Show Configuration &Folder"));
    connect(confDir, &QAction::triggered, this, [this]() {
        if (!m_configPath.isEmpty())
            ConfigFile::openContainingFolder(m_configPath, this);
    });

    file->addSeparator();

    QAction *certs = file->addAction(tr("Show &Certificates"));
    connect(certs, &QAction::triggered, this, [this]() {
        if (!m_app) return;
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QString::fromUtf8(app_config(m_app)->pki_dir)));
    });

    file->addSeparator();
    QAction *quit = file->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, [this]() {
        m_reallyQuit = true;
        qApp->quit();
    });

    auto *view = menuBar()->addMenu(tr("&View"));

    m_actShowSidebar = view->addAction(tr("Show &Sidebar"));
    m_actShowSidebar->setCheckable(true);
    m_actShowSidebar->setChecked(true);
    /* Ctrl+Shift+S, not macOS's Ctrl+Cmd+S — there is no sane Linux analogue
     * of the Control-Command pair. */
    m_actShowSidebar->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(m_actShowSidebar, &QAction::toggled, this, [this](bool on) {
        m_sidebar->setVisible(on);
        setMinimumWidth(on ? 560 : 320);
    });

    m_actShowLog = view->addAction(tr("Show &Log"));
    m_actShowLog->setCheckable(true);
    m_actShowLog->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    connect(m_actShowLog, &QAction::toggled, this, [this](bool on) {
        m_log->setVisible(on);
        if (on) refreshLog();
    });

    auto *conn = menuBar()->addMenu(tr("&Connection"));
    QAction *reconnect = conn->addAction(tr("&Reconnect"));
    reconnect->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(reconnect, &QAction::triggered, this, [this]() {
        if (m_app) app_reconnect(m_app);
    });

    QAction *lock = conn->addAction(tr("&Lock talkgroup"));
    lock->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    connect(lock, &QAction::triggered, this, [this]() {
        if (m_app) app_toggle_lock(m_app);
    });

    auto *help = menuBar()->addMenu(tr("&Help"));
    QAction *docs = help->addAction(tr("&Documentation"));
    connect(docs, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://svxconnect.app")));
    });

    QAction *about = help->addAction(tr("&About SVXConnect"));
    connect(about, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About SVXConnect"),
            tr("<h3>SVXConnect %1</h3>"
               "<p>A Qt desktop client for SvxLink reflectors.</p>"
               "<p>Copyright © 2026 Diëlectricum BV.<br>"
               "Written by Joeri Van Dooren, ON6URE.</p>"
               "<p>Released under the MIT licence.</p>"
               "<p>Built with the Qt toolkit %2, © The Qt Company Ltd and "
               "contributors, used under the GNU Lesser General Public License "
               "version 3. Qt is linked dynamically and unmodified; you may "
               "replace the Qt libraries with modified versions and relink.</p>")
                .arg(QString::fromLatin1(SVXCONNECT_VERSION),
                     QString::fromLatin1(qVersion())));
    });
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    if (!m_app) { QMainWindow::keyPressEvent(e); return; }

    /* Space is a TOGGLE, not a hold, and Escape is an unconditional stop.
     *
     * PIOS found the reason and it is worth restating: keyboard auto-repeat
     * under Wayland makes an in-window hold-to-talk unreliable — the release
     * you are waiting for may never arrive in the order you expect. A toggle
     * is deterministic. True hold-to-talk comes from the global hotkey in M4,
     * which gets its release from the compositor rather than from a focused
     * widget, and from the mouse on the PTT button.
     *
     * isAutoRepeat() is checked anyway, so holding Space does not chatter the
     * transmitter on and off. */
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        app_ptt(m_app, CTL_TOGGLE);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        app_ptt(m_app, CTL_OFF);
        e->accept();
        return;
    }
    QMainWindow::keyPressEvent(e);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        if (watched == m_banner) {
            /* Tell the core, not just the widget: app_banner() would hand the
             * same string straight back on the next tick otherwise. */
            if (m_app) app_dismiss_banner(m_app);
            m_banner->hide();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onCoreChanged()
{
    /* The observer fired: volume, mute, a device or the banner changed. All
     * cheap to re-read, and all covered by the next model tick anyway — this
     * just makes those four feel immediate. */
    refreshBanner();
}

void MainWindow::tickModel()
{
    const quint64 now = m_app ? now_ms() : 0;

    if (m_tray) m_tray->tickModel();
    m_status->tickModel(now);
    m_sidebar->tickModel(now);
    m_activity->tickModel(now);

    refreshBanner();
    refreshPttButton();

    if (m_log->isVisible())
        refreshLog();
}

void MainWindow::tickMeters()
{
    m_sidebar->tickMeters();
}

void MainWindow::refreshBanner()
{
    if (!m_app) return;

    const char *banner = app_banner(m_app);
    if (banner && *banner) {
        const QString text = QString::fromUtf8(banner);
        if (m_banner->text() != text)
            m_banner->setText(text);
        m_banner->show();
    } else if (m_banner->isVisible()) {
        m_banner->hide();
    }
}

void MainWindow::refreshPttButton()
{
    const bool tx = m_app && app_tx_active(m_app);
    if (m_txInit && tx == m_lastTxActive)
        return;
    m_txInit = true;
    m_lastTxActive = tx;

    /* Explicit colours rather than a palette role: several desktop themes
     * desaturate palette-derived accents on an unfocused window, which is
     * precisely when a transmitting operator most needs to see that they are
     * on the air. */
    m_ptt->setStyleSheet(tx
        ? QStringLiteral("QPushButton { background:%1; color:white; border:none; "
                         "border-radius:6px; }").arg(Theme::tx().name())
        : QString());
    m_ptt->setText(tx ? tr("TRANSMITTING") : tr("PUSH TO TALK"));
}

void MainWindow::refreshLog()
{
    const quint64 serial = LogBridge::serial();
    if (serial == m_lastLogSerial)
        return;
    m_lastLogSerial = serial;

    QScrollBar *bar = m_log->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;

    const QVector<LogLine> lines = LogBridge::snapshot(500);
    m_log->clear();
    for (const LogLine &l : lines)
        m_log->appendPlainText(l.text);

    if (atBottom)
        bar->setValue(bar->maximum());
}

void MainWindow::onPttPressed()
{
    if (m_app) app_ptt(m_app, CTL_ON);
}

void MainWindow::onPttReleased()
{
    if (m_app) app_ptt(m_app, CTL_OFF);
}


void MainWindow::setConfigPath(const QString &path)
{
    m_configPath = path;
    watchConfig();
}

void MainWindow::watchConfig()
{
    if (m_configPath.isEmpty())
        return;

    if (!m_configWatch) {
        m_configWatch = new QFileSystemWatcher(this);
        connect(m_configWatch, &QFileSystemWatcher::fileChanged,
                this, &MainWindow::onConfigFileChanged);
    }

    if (!m_configWatch->files().contains(m_configPath)
        && QFileInfo::exists(m_configPath))
        m_configWatch->addPath(m_configPath);
}

void MainWindow::onConfigFileChanged(const QString &path)
{
    Q_UNUSED(path);

    /* Most editors save by writing a temporary file and renaming it over the
     * target. That REMOVES the inode the watcher was holding, so the path is
     * silently dropped from the watch list and a second save would go
     * unnoticed. Re-adding it is not optional. */
    watchConfig();

    log_info("configuration file changed on disk");
    m_reload->show();
}

void MainWindow::onEditConfig()
{
    if (m_configPath.isEmpty()) {
        QMessageBox::warning(this, tr("No configuration file"),
            tr("SVXConnect does not know which configuration file to edit."));
        return;
    }

    if (!ConfigFile::exists(m_configPath)) {
        const QMessageBox::StandardButton answer = QMessageBox::question(this,
            tr("Create a configuration file?"),
            tr("There is no configuration file at:\n\n%1\n\n"
               "Create one now, filled in with this build's defaults?")
                .arg(m_configPath),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);

        if (answer != QMessageBox::Yes)
            return;

        QString error;
        if (!ConfigFile::createDefault(m_configPath,
                                       m_app ? app_config(m_app) : nullptr, &error)) {
            QMessageBox::critical(this, tr("Could not create the configuration"), error);
            return;
        }
        watchConfig();
    }

    ConfigFile::openInEditor(m_configPath, this);
}


void MainWindow::onRestartRequested()
{
    /* Never restart with the transmitter keyed. app_free() would unkey on the
     * way out, but the operator would hear their own audio stop mid-word with
     * no idea why, and the reflector would see a talker vanish without a
     * flush. Drop the carrier first, explicitly. */
    if (m_app && app_tx_active(m_app)) {
        app_ptt(m_app, CTL_OFF);
        log_info("unkeyed before restarting");
    }

    log_info("restarting to apply the new configuration");
    emit restartRequested();
}


void MainWindow::onPreferences()
{
    if (m_configPath.isEmpty()) {
        QMessageBox::warning(this, tr("No configuration file"),
            tr("SVXConnect does not know which configuration file to edit."));
        return;
    }

    /* Non-modal would be nicer, but the dialog writes the shared config file
     * and applies device changes through the core, and the core is
     * main-thread-only with no re-entrancy guarantees. Modal keeps that
     * simple and matches what the dialog actually does. */
    PreferencesDialog dlg(m_app, m_configPath, this);

    /* Show what the DESKTOP actually bound, not what we asked for — they can
     * differ, and the desktop's answer is the one that fires. */
    if (m_pttManager) {
        if (PttBackend *portal = m_pttManager->backend(QStringLiteral("portal")))
            dlg.setCurrentShortcut(portal->activeTrigger());
    }

    /* Keep it live: reassigning the key in system settings while this dialog
     * is open updates the label without reopening it. */
    QMetaObject::Connection trigConn;
    if (m_pttManager)
        trigConn = connect(m_pttManager, &PttManager::triggerChanged, &dlg,
                           [&dlg](const QString &id, const QString &human) {
                               if (id == QLatin1String("portal"))
                                   dlg.setCurrentShortcut(human);
                           });

    connect(&dlg, &PreferencesDialog::restartNeeded, this, [this]() {
        m_reload->show();
    });
    connect(&dlg, &PreferencesDialog::pttBindingChanged, this, [this]() {
        applyPttBindings();
    });
    connect(&dlg, &PreferencesDialog::talkgroupsChanged, this, [this]() {
        /* The buttons are rebuilt from the LIVE config, which the dialog
         * deliberately did not touch — so this only takes effect after the
         * restart. Calling it anyway is harmless and keeps the one code path. */
        m_sidebar->rebuildTalkgroups();
    });

    dlg.exec();

    if (trigConn)
        disconnect(trigConn);
}


void MainWindow::applyPttBindings()
{
    if (!m_pttManager)
        return;

    m_pttManager->setMode(PreferencesDialog::holdMode() ? PttManager::Mode::Hold
                                                 : PttManager::Mode::Toggle);
    m_pttManager->applyKeyboardBinding(PreferencesDialog::keyboardBinding());

    const PttBinding dev = PreferencesDialog::deviceBinding();
    if (dev.isValid())
        m_pttManager->applyDeviceBinding(dev);
}


void MainWindow::closeEvent(QCloseEvent *e)
{
    /* File → Quit and the tray's Quit set m_reallyQuit; everything else means
     * "close the window", which for this application means hide. */
    if (m_reallyQuit || !m_tray || !m_tray->isAvailable()) {
        QMainWindow::closeEvent(e);
        return;
    }

    e->ignore();
    hide();

    /* Say so once. A window that does not go away when closed is surprising,
     * and silently surviving is exactly the behaviour users report as "it
     * would not quit". */
    QSettings s;
    if (!s.value(QStringLiteral("window/toldAboutTray"), false).toBool()) {
        s.setValue(QStringLiteral("window/toldAboutTray"), true);
        m_tray->notifyStillRunning();
    }
}
