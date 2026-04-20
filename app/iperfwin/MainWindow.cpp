#include "MainWindow.h"

#include "IperfCoreBridge.h"
#include "IperfPages.h"

#include <QAction>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSysInfo>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_bridge(new IperfCoreBridge(this))
    , m_navigation(new QListWidget(this))
    , m_stack(new QStackedWidget(this))
    , m_stateLabel(new QLabel(QStringLiteral("Idle"), this))
    , m_testPage(new TestPage(this))
    , m_historyPage(new HistoryPage(this))
    , m_settingsPage(new SettingsPage(this))
{
    setWindowTitle(QStringLiteral("IperfWin"));

    // ── Central widget layout ─────────────────────────────────────────────
    auto *central   = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Slim header bar ────────────────────────────────────────────────────
    auto *header = new QWidget(central);
    header->setObjectName(QStringLiteral("HeaderBar"));
    header->setStyleSheet(
        QStringLiteral("#HeaderBar{"
                       "  background:#17324d;"
                       "  border-bottom:1px solid #0f1f2e;"
                       "}"
                       "#HeaderBar QLabel{ color:white; }"));
    header->setFixedHeight(48);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(16, 0, 16, 0);

    auto *titleLabel = new QLabel(QStringLiteral("IperfWin"), header);
    titleLabel->setStyleSheet(QStringLiteral("font-size:17px; font-weight:700;"));
    auto *subLabel = new QLabel(QStringLiteral("Network throughput test tool"), header);
    subLabel->setStyleSheet(QStringLiteral("color:rgba(255,255,255,0.65); font-size:11px;"));

    auto *titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(0);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(subLabel);
    hl->addLayout(titleBlock);
    hl->addStretch();
    hl->addWidget(new QLabel(QStringLiteral("State:"), header));
    hl->addSpacing(4);
    m_stateLabel->setStyleSheet(
        QStringLiteral("color:white; font-weight:600; min-width:80px;"));
    hl->addWidget(m_stateLabel);
    rootLayout->addWidget(header);

    // ── Main content: nav + stack ─────────────────────────────────────────
    auto *content   = new QWidget(central);
    auto *contentHl = new QHBoxLayout(content);
    contentHl->setContentsMargins(0, 0, 0, 0);
    contentHl->setSpacing(0);

    // Navigation sidebar
    m_navigation->setFixedWidth(150);
    m_navigation->setStyleSheet(
        QStringLiteral("QListWidget{"
                       "  background:#f0f0f0; border:none; border-right:1px solid #ddd;"
                       "  padding:8px 0;"
                       "}"
                       "QListWidget::item{"
                       "  padding:10px 16px; font-size:13px;"
                       "}"
                       "QListWidget::item:selected{"
                       "  background:#0066cc; color:white;"
                       "}"));
    m_navigation->addItem(QStringLiteral("Test"));
    m_navigation->addItem(QStringLiteral("History"));
    m_navigation->addItem(QStringLiteral("Settings"));
    m_navigation->setCurrentRow(0);

    contentHl->addWidget(m_navigation);

    // Page stack
    m_stack->addWidget(m_testPage);     // 0
    m_stack->addWidget(m_historyPage);  // 1
    m_stack->addWidget(m_settingsPage); // 2
    contentHl->addWidget(m_stack, 1);

    rootLayout->addWidget(content, 1);
    setCentralWidget(central);

    // ── Menu bar ──────────────────────────────────────────────────────────
    auto *helpMenu  = menuBar()->addMenu(QStringLiteral("&Help"));
    auto *aboutAct  = helpMenu->addAction(QStringLiteral("About IperfWin"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::showAboutDialog);

    // ── Status bar ────────────────────────────────────────────────────────
    statusBar()->showMessage(QStringLiteral("Ready"));

    // ── Bind pages to bridge ──────────────────────────────────────────────
    m_testPage->bindBridge(m_bridge);
    m_historyPage->bindBridge(m_bridge);
    m_settingsPage->bindBridge(m_bridge);
    m_settingsPage->loadSettings();

    // Forward sessions from TestPage to HistoryPage
    connect(m_testPage, &TestPage::sessionRecorded,
            m_historyPage, &HistoryPage::appendSession);

    // Navigation
    connect(m_navigation, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_stack->count()) {
            m_stack->setCurrentIndex(row);
        }
    });

    // Single-session lock: disable History and Settings nav while test is running
    connect(m_bridge, &IperfCoreBridge::runningChanged,
            this, &MainWindow::onRunningChanged);

    // State label
    connect(m_bridge, &IperfCoreBridge::stateChanged, this, [this](const QString &state) {
        m_stateLabel->setText(state.isEmpty() ? QStringLiteral("Idle") : state);
    });
    connect(m_bridge, &IperfCoreBridge::runningChanged, this, [this](bool running) {
        statusBar()->showMessage(running ? QStringLiteral("Running") : QStringLiteral("Ready"));
    });

    // Expert mode toggle
    connect(m_settingsPage, &SettingsPage::expertModeChanged,
            m_testPage, &TestPage::setExpertMode);

    // Load expert mode state immediately
    {
        QSettings s;
        s.beginGroup(QStringLiteral("preferences"));
        const bool expert = s.value(QStringLiteral("expertMode"), false).toBool();
        s.endGroup();
        m_testPage->setExpertMode(expert);
    }

    // Restore address fields
    {
        QSettings s;
        m_testPage->loadSettings(s);
    }

    loadWindowSettings();
    resize(1280, 820);
}

// ---------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_bridge && m_bridge->isRunning()) {
        m_bridge->stop();
    }
    if (m_settingsPage) {
        m_settingsPage->saveSettings();
    }
    if (m_testPage) {
        QSettings s;
        m_testPage->saveSettings(s);
    }
    saveWindowSettings();
    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------------------
void MainWindow::onRunningChanged(bool running)
{
    // Lock History and Settings nav items during a test
    for (int i = 1; i < m_navigation->count(); ++i) {
        auto *item = m_navigation->item(i);
        if (item) {
            if (running) {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            } else {
                item->setFlags(item->flags() | Qt::ItemIsEnabled);
            }
        }
    }
    if (!running && m_navigation->currentRow() < 0) {
        m_navigation->setCurrentRow(0);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::showAboutDialog()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("About IperfWin"));
    box.setTextFormat(Qt::RichText);
    box.setText(
        QStringLiteral("<b>IperfWin v1.0</b><br>"
                       "Network throughput test tool powered by libiperf.<br><br>"
                       "Runtime: Qt %1<br>"
                       "Platform: %2 (%3)<br><br>"
                       "Select <b>Test</b> to start a measurement.<br>"
                       "Use <b>Settings → Show Expert Controls</b> for advanced options.")
        .arg(QString::fromLatin1(QT_VERSION_STR),
             QSysInfo::prettyProductName(),
             QSysInfo::currentCpuArchitecture()));
    box.exec();
}

// ---------------------------------------------------------------------------
void MainWindow::loadWindowSettings()
{
    QSettings s;
    const QByteArray geometry = s.value(QStringLiteral("window/geometry")).toByteArray();
    const int page = s.value(QStringLiteral("window/currentPage"), 0).toInt();

    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        resize(1280, 820);
    }

    if (m_navigation && m_stack->count() > 0) {
        m_navigation->setCurrentRow(qBound(0, page, m_stack->count() - 1));
    }
}

void MainWindow::saveWindowSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"),
               saveGeometry());
    s.setValue(QStringLiteral("window/currentPage"),
               m_navigation ? m_navigation->currentRow() : 0);
    s.sync();
}
