#include "MainWindow.h"

#include "IperfCoreBridge.h"
#include "IperfPages.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_bridge(new IperfCoreBridge(this))
    , m_navigation(new QListWidget(this))
    , m_stack(new QStackedWidget(this))
    , m_modeLabel(new QLabel(this))
    , m_targetLabel(new QLabel(this))
    , m_stateLabel(new QLabel(this))
    , m_summaryLabel(new QLabel(this))
    , m_quickPage(new QuickTestPage(this))
    , m_advancedPage(new AdvancedClientPage(this))
    , m_serverPage(new ServerPage(this))
    , m_historyPage(new HistoryPage(this))
    , m_settingsPage(new SettingsPage(this))
{
    setWindowTitle(QStringLiteral("IperfWin"));

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    auto *header = new QFrame(central);
    header->setObjectName(QStringLiteral("HeaderBar"));
    header->setStyleSheet(QStringLiteral(
        "#HeaderBar { background: linear-gradient(90deg, #17324d, #0f1f2e); border-radius: 14px; }"
        "#HeaderBar QLabel { color: white; }"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 14, 18, 14);

    auto *titleBlock = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("IperfWin"), header);
    title->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 700;"));
    auto *subtitle = new QLabel(QStringLiteral("Qt6 Widgets front-end for libiperf"), header);
    subtitle->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.72);"));
    titleBlock->addWidget(title);
    titleBlock->addWidget(subtitle);
    headerLayout->addLayout(titleBlock, 1);

    auto *stateBlock = new QHBoxLayout;
    stateBlock->addWidget(new QLabel(QStringLiteral("Mode"), header));
    stateBlock->addWidget(m_modeLabel);
    stateBlock->addSpacing(18);
    stateBlock->addWidget(new QLabel(QStringLiteral("Target"), header));
    stateBlock->addWidget(m_targetLabel);
    stateBlock->addSpacing(18);
    stateBlock->addWidget(new QLabel(QStringLiteral("State"), header));
    stateBlock->addWidget(m_stateLabel);
    stateBlock->addSpacing(18);
    stateBlock->addWidget(new QLabel(QStringLiteral("Summary"), header));
    stateBlock->addWidget(m_summaryLabel);
    headerLayout->addLayout(stateBlock, 2);

    rootLayout->addWidget(header);

    auto *splitter = new QSplitter(central);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);

    m_navigation->addItems({
        QStringLiteral("Quick Test"),
        QStringLiteral("Advanced Client"),
        QStringLiteral("Server"),
        QStringLiteral("History"),
        QStringLiteral("Settings"),
    });
    m_navigation->setFixedWidth(180);
    m_navigation->setCurrentRow(0);

    splitter->addWidget(m_navigation);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(1, 1);

    m_stack->addWidget(m_quickPage);
    m_stack->addWidget(m_advancedPage);
    m_stack->addWidget(m_serverPage);
    m_stack->addWidget(m_historyPage);
    m_stack->addWidget(m_settingsPage);

    rootLayout->addWidget(splitter, 1);
    setCentralWidget(central);

    statusBar()->showMessage(QStringLiteral("Ready"));
    resize(1440, 900);

    m_quickPage->bindBridge(m_bridge);
    m_advancedPage->bindBridge(m_bridge);
    m_serverPage->bindBridge(m_bridge);
    m_historyPage->bindBridge(m_bridge);

    connect(m_navigation, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_stack->count()) {
            m_stack->setCurrentIndex(row);
        }
    });

    connect(m_bridge, &IperfCoreBridge::configurationChanged, this, &MainWindow::updateHeaderFromConfig);
    connect(m_bridge, &IperfCoreBridge::stateChanged, this, &MainWindow::updateHeaderFromState);
    connect(m_bridge, &IperfCoreBridge::eventReceived, this, &MainWindow::updateHeaderFromEvent);
    connect(m_bridge, &IperfCoreBridge::sessionCompleted, this, &MainWindow::updateHeaderFromSession);
    connect(m_bridge, &IperfCoreBridge::runningChanged, this, [this](bool running) {
        statusBar()->showMessage(running ? QStringLiteral("Running") : QStringLiteral("Ready"));
    });

    updateHeaderFromConfig(m_bridge->configuration());
    updateHeaderFromState(QStringLiteral("Idle"));
}

void
MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_bridge != nullptr && m_bridge->isRunning()) {
        m_bridge->stop();
    }
    QMainWindow::closeEvent(event);
}

void
MainWindow::updateHeaderFromConfig(const IperfGuiConfig &config)
{
    m_modeLabel->setText(iperfModeName(config.mode));
    m_targetLabel->setText(config.mode == IperfGuiConfig::Mode::Server
        ? QStringLiteral(":%1").arg(config.port)
        : QStringLiteral("%1:%2").arg(iperfTargetName(config)).arg(config.port));
    m_summaryLabel->setText(QStringLiteral("%1 / %2")
        .arg(iperfProtocolName(config.protocol),
             config.family == IperfGuiConfig::AddressFamily::Any ? QStringLiteral("Any") : iperfFamilyName(config.family)));
}

void
MainWindow::updateHeaderFromState(const QString &state)
{
    m_stateLabel->setText(state.isEmpty() ? QStringLiteral("Idle") : state);
}

void
MainWindow::updateHeaderFromEvent(const IperfGuiEvent &event)
{
    if (!event.message.isEmpty()) {
        m_summaryLabel->setText(event.message);
    }
}

void
MainWindow::updateHeaderFromSession(const IperfSessionRecord &record)
{
    m_summaryLabel->setText(record.statusText.isEmpty() ? QStringLiteral("Completed") : record.statusText);
    updateHeaderFromConfig(record.config);
}
