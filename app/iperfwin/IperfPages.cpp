#include "IperfPages.h"

#include "IperfCoreBridge.h"
#include "IperfTestOrchestrator.h"

#include <algorithm>
#include <QButtonGroup>
#include <QNetworkInterface>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QComboBox>
#include <QStandardItemModel>
#include <QSysInfo>
#include <QTabBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

// ============================================================================
// Local helpers
// ============================================================================
namespace {

static QPushButton *makeToggleBtn(const QString &text, QButtonGroup *group, QWidget *parent)
{
    auto *btn = new QPushButton(text, parent);
    btn->setCheckable(true);
    btn->setFixedHeight(28);
    btn->setStyleSheet(
        QStringLiteral("QPushButton{"
                       "border:1px solid #bbb;border-radius:4px;"
                       "padding:2px 10px;background:#f5f5f5;}"
                       "QPushButton:checked{"
                       "background:#0066cc;color:white;border-color:#004fa3;}"));
    if (group) { group->addButton(btn); }
    return btn;
}

static QComboBox *makeTrafficTypeCombo(QWidget *parent)
{
    auto *cb = new QComboBox(parent);
    cb->addItem(QStringLiteral("TCP"),   QVariant::fromValue(TrafficType::Tcp));
    cb->addItem(QStringLiteral("UDP"),   QVariant::fromValue(TrafficType::Udp));
    const QStringList future = {
        QStringLiteral("ICMP"), QStringLiteral("HTTP"), QStringLiteral("HTTPS"),
        QStringLiteral("DNS"),  QStringLiteral("FTP"),
    };
    for (const QString &n : future) { cb->addItem(n); }
    auto *model = qobject_cast<QStandardItemModel *>(cb->model());
    if (model) {
        for (int i = 2; i < cb->count(); ++i) {
            auto *item = model->item(i);
            if (item) {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setToolTip(QStringLiteral("Coming in a future version"));
            }
        }
    }
    return cb;
}

static QComboBox *makePacketSizeCombo(QWidget *parent)
{
    auto *cb = new QComboBox(parent);
    cb->addItem(QStringLiteral("64 B"),           QVariant::fromValue(PacketSize::B64));
    cb->addItem(QStringLiteral("128 B"),          QVariant::fromValue(PacketSize::B128));
    cb->addItem(QStringLiteral("256 B"),          QVariant::fromValue(PacketSize::B256));
    cb->addItem(QStringLiteral("512 B"),          QVariant::fromValue(PacketSize::B512));
    cb->addItem(QStringLiteral("1024 B"),         QVariant::fromValue(PacketSize::B1024));
    cb->addItem(QStringLiteral("1518 B"),          QVariant::fromValue(PacketSize::B1518));
    cb->addItem(QStringLiteral("9000 B (Jumbo)"), QVariant::fromValue(PacketSize::Jumbo));
    cb->setCurrentIndex(5); // default 1518B
    return cb;
}

static QLabel *makeBigMetricLabel(const QString &name, QWidget *parent)
{
    auto *lbl = new QLabel(
        QStringLiteral("<b>%1</b><br><span style='font-size:18px;'>—</span>").arg(name),
        parent);
    lbl->setTextFormat(Qt::RichText);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setMinimumHeight(64);
    lbl->setFrameShape(QFrame::StyledPanel);
    return lbl;
}

static void setMetricLabel(QLabel *lbl, const QString &name, const QString &value)
{
    lbl->setText(
        QStringLiteral("<b>%1</b><br><span style='font-size:18px;'>%2</span>")
        .arg(name, value));
}

static bool writeTextFile(const QString &path, const QString &content, QString *errOut = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errOut) { *errOut = f.errorString(); }
        return false;
    }
    QTextStream ts(&f);
    ts << content;
    return true;
}

} // namespace

// ============================================================================
// TestPage
// ============================================================================
TestPage::TestPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    // ── Role toggle ──────────────────────────────────────────────────────
    {
        auto *bar = new QHBoxLayout;
        bar->setSpacing(6);
        auto *grp = new QButtonGroup(this);
        grp->setExclusive(true);
        m_clientBtn = makeToggleBtn(QStringLiteral("Client"), grp, this);
        m_serverBtn = makeToggleBtn(QStringLiteral("Server"), grp, this);
        m_clientBtn->setChecked(true);
        bar->addWidget(new QLabel(QStringLiteral("Role:"), this));
        bar->addWidget(m_clientBtn);
        bar->addWidget(m_serverBtn);
        bar->addStretch();
        root->addLayout(bar);
        connect(grp, qOverload<QAbstractButton*>(&QButtonGroup::buttonClicked),
                this, [this](QAbstractButton *) { onRoleChanged(); });
    }

    // ── Role stacked area (client / server) ───────────────────────────────
    m_roleStack = new QStackedWidget(this);
    m_roleStack->addWidget(buildClientArea());   // 0
    m_roleStack->addWidget(buildServerArea());   // 1
    root->addWidget(m_roleStack);

    // ── Expert panel (hidden by default) ─────────────────────────────────
    m_expertPanel = new QFrame(this);
    m_expertPanel->setFrameShape(QFrame::StyledPanel);
    m_expertPanel->setVisible(false);
    {
        auto *fl = new QFormLayout(m_expertPanel);
        m_customPortSpin = new QSpinBox(m_expertPanel);
        m_customPortSpin->setRange(0, 65535);
        m_customPortSpin->setSpecialValueText(QStringLiteral("Auto"));
        fl->addRow(QStringLiteral("Custom Port:"), m_customPortSpin);

        m_bindAddrEdit = new QLineEdit(m_expertPanel);
        m_bindAddrEdit->setPlaceholderText(QStringLiteral("empty = system default"));
        fl->addRow(QStringLiteral("Bind Address:"), m_bindAddrEdit);
    }
    root->addWidget(m_expertPanel);

    // ── Action bar ────────────────────────────────────────────────────────
    {
        auto *bar = new QHBoxLayout;
        bar->setSpacing(8);
        m_startBtn  = new QPushButton(QStringLiteral("▶  Start Test"), this);
        m_stopBtn   = new QPushButton(QStringLiteral("■  Stop"),       this);
        m_exportBtn = new QPushButton(QStringLiteral("Export ↓"),      this);
        m_startBtn->setFixedHeight(32);
        m_stopBtn->setFixedHeight(32);
        m_exportBtn->setFixedHeight(32);
        m_stopBtn->setEnabled(false);
        m_exportBtn->setEnabled(false);
        m_statusLabel = new QLabel(QStringLiteral("Idle"), this);
        m_statusLabel->setStyleSheet(QStringLiteral("color:#555;"));
        bar->addWidget(m_startBtn);
        bar->addWidget(m_stopBtn);
        bar->addWidget(m_exportBtn);
        bar->addSpacing(12);
        bar->addWidget(m_statusLabel, 1);
        root->addLayout(bar);
        connect(m_startBtn,  &QPushButton::clicked, this, &TestPage::onStartClicked);
        connect(m_stopBtn,   &QPushButton::clicked, this, &TestPage::onStopClicked);
        connect(m_exportBtn, &QPushButton::clicked, this, &TestPage::onExportClicked);
    }

    // ── Results area ──────────────────────────────────────────────────────
    root->addWidget(buildResultsArea(), 1);
}

// ---------------------------------------------------------------------------
QWidget *TestPage::buildClientArea()
{
    auto *w  = new QWidget(this);
    auto *vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(8);

    // Traffic Mode toggle (Single / Mixed)
    {
        auto *bar = new QHBoxLayout;
        bar->setSpacing(6);
        auto *grp = new QButtonGroup(this);
        grp->setExclusive(true);
        m_singleModeBtn = makeToggleBtn(QStringLiteral("Single"), grp, w);
        m_mixedModeBtn  = makeToggleBtn(QStringLiteral("Mixed"),  grp, w);
        m_singleModeBtn->setChecked(true);;
        bar->addWidget(new QLabel(QStringLiteral("Traffic Mode:"), w));
        bar->addWidget(m_singleModeBtn);
        bar->addWidget(m_mixedModeBtn);
        bar->addStretch();
        vl->addLayout(bar);
        connect(grp, qOverload<QAbstractButton*>(&QButtonGroup::buttonClicked),
                this, [this](QAbstractButton *) { onTrafficModeChanged(); });
    }

    // Server address
    {
        auto *bar = new QHBoxLayout;
        bar->setSpacing(6);
        m_serverAddress = new QLineEdit(w);
        m_serverAddress->setPlaceholderText(QStringLiteral("Server IP or hostname"));
        bar->addWidget(new QLabel(QStringLiteral("Server Address:"), w));
        bar->addWidget(m_serverAddress, 1);
        vl->addLayout(bar);
    }

    // Traffic-mode stacked (0=single, 1=mixed)
    m_trafficModeStack = new QStackedWidget(w);

    // ── [0] Single ──────────────────────────────────────────────────────
    {
        auto *sw = new QWidget(m_trafficModeStack);
        auto *hl = new QHBoxLayout(sw);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(20);
        m_trafficType = makeTrafficTypeCombo(sw);
        m_packetSize  = makePacketSizeCombo(sw);
        auto *tl = new QHBoxLayout;
        tl->setSpacing(6);
        tl->addWidget(new QLabel(QStringLiteral("Traffic Type:"), sw));
        tl->addWidget(m_trafficType);
        auto *pl = new QHBoxLayout;
        pl->setSpacing(6);
        auto *psLabel = new QLabel(QStringLiteral("Packet Size:"), sw);
        psLabel->setToolTip(
            QStringLiteral("UDP: controls datagram size (close to on-wire packet size).\n"
                           "TCP: controls application write block size — actual wire frames\n"
                           "are shaped by MSS, TSO/GSO and NIC offload."));
        pl->addWidget(psLabel);
        pl->addWidget(m_packetSize);
        hl->addLayout(tl);
        hl->addLayout(pl);
        hl->addStretch();
        m_trafficModeStack->addWidget(sw);
    }

    // ── [1] Mixed ───────────────────────────────────────────────────────
    {
        auto *mw  = new QWidget(m_trafficModeStack);
        auto *mvl = new QVBoxLayout(mw);
        mvl->setContentsMargins(0, 0, 0, 0);
        mvl->setSpacing(4);

        // Column headers
        auto *hdr = new QHBoxLayout;
        auto makeH = [&](const QString &t, int s) {
            auto *l = new QLabel(QStringLiteral("<b>%1</b>").arg(t), mw);
            l->setTextFormat(Qt::RichText);
            hdr->addWidget(l, s);
        };
        makeH(QStringLiteral("Traffic Type"), 2);
        makeH(QStringLiteral("Packet Size"),  2);
        makeH(QStringLiteral("Ratio"),        1);
        hdr->addSpacing(28);
        mvl->addLayout(hdr);

        // Scroll area
        auto *scroll = new QScrollArea(mw);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setMaximumHeight(150);
        m_mixContainer = new QWidget(scroll);
        m_mixLayout    = new QVBoxLayout(m_mixContainer);
        m_mixLayout->setContentsMargins(0, 0, 0, 0);
        m_mixLayout->setSpacing(2);
        m_mixLayout->addStretch();
        scroll->setWidget(m_mixContainer);
        mvl->addWidget(scroll);

        // Footer
        auto *footer = new QHBoxLayout;
        auto *addBtn = new QPushButton(QStringLiteral("+ Add Row"), mw);
        addBtn->setFixedHeight(26);
        m_mixTotalLabel = new QLabel(QStringLiteral("Total: 0%"), mw);
        footer->addWidget(addBtn);
        footer->addStretch();
        footer->addWidget(m_mixTotalLabel);
        mvl->addLayout(footer);

        m_trafficModeStack->addWidget(mw);

        // Default 4 rows
        addMixRow(TrafficType::Tcp, PacketSize::B1518, 40);
        addMixRow(TrafficType::Udp, PacketSize::B1518, 30);
        addMixRow(TrafficType::Tcp, PacketSize::B64,   20);
        addMixRow(TrafficType::Udp, PacketSize::B64,   10);

        connect(addBtn, &QPushButton::clicked, this, &TestPage::onAddMixRow);
    }

    vl->addWidget(m_trafficModeStack);

    // Duration buttons
    {
        auto *bar = new QHBoxLayout;
        bar->setSpacing(4);
        bar->addWidget(new QLabel(QStringLiteral("Duration:"), w));
        m_durationGroup = new QButtonGroup(this);
        m_durationGroup->setExclusive(true);

        struct DurEntry { const char *lbl; DurationPreset preset; };
        const DurEntry entries[] = {
            { "5 min",  DurationPreset::Min5       },
            { "30 min", DurationPreset::Min30      },
            { "1 h",    DurationPreset::H1         },
            { "6 h",    DurationPreset::H6         },
            { "24 h",   DurationPreset::H24        },
            { "\xe2\x88\x9e", DurationPreset::Continuous }, // ∞
        };
        for (const auto &e : entries) {
            auto *btn = makeToggleBtn(QString::fromUtf8(e.lbl), m_durationGroup, w);
            btn->setProperty("durationPreset", QVariant::fromValue(e.preset));
            // Default: 1 h — long enough for stability testing without committing to 24h
            if (e.preset == DurationPreset::H1) { btn->setChecked(true); }
            bar->addWidget(btn);
        }
        bar->addStretch();
        vl->addLayout(bar);
    }

    // Direction buttons
    {
        auto *bar = new QHBoxLayout;
        bar->setSpacing(4);
        bar->addWidget(new QLabel(QStringLiteral("Direction:"), w));
        m_directionGroup = new QButtonGroup(this);
        m_directionGroup->setExclusive(true);

        struct DirEntry { const char *lbl; Direction dir; };
        const DirEntry entries[] = {
            { "Uplink",        Direction::Uplink        },
            { "Downlink",      Direction::Downlink      },
            { "Bidirectional", Direction::Bidirectional },
        };
        for (const auto &e : entries) {
            auto *btn = makeToggleBtn(QString::fromUtf8(e.lbl), m_directionGroup, w);
            btn->setProperty("direction", QVariant::fromValue(e.dir));
            // Default: Bidirectional — measures full-duplex path capacity
            if (e.dir == Direction::Bidirectional) { btn->setChecked(true); }
            bar->addWidget(btn);
        }
        bar->addStretch();
        vl->addLayout(bar);
    }

    return w;
}

// ---------------------------------------------------------------------------
QWidget *TestPage::buildServerArea()
{
    auto *w  = new QWidget(this);
    auto *vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(8);

    // Network interface selector
    {
        auto *bar = new QHBoxLayout;
        bar->setSpacing(6);
        m_nicSelector = new QComboBox(w);
        m_nicSelector->setMinimumWidth(320);
        populateNicSelector();
        bar->addWidget(new QLabel(QStringLiteral("Network Interface:"), w));
        bar->addWidget(m_nicSelector, 1);
        vl->addLayout(bar);
    }

    // Help text
    {
        auto *hint = new QLabel(
            QStringLiteral("Start the server, then point the client to the IP shown above.\n"
                           "The server listens on the selected interface and accepts any traffic type."),
            w);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#666; font-size:11px;"));
        vl->addWidget(hint);
    }

    vl->addStretch();
    return w;
}

// ---------------------------------------------------------------------------
void TestPage::populateNicSelector()
{
    if (!m_nicSelector) { return; }
    m_nicSelector->clear();
    // "All interfaces" sentinel — listenAddress will be empty → 0.0.0.0
    m_nicSelector->addItem(QStringLiteral("All interfaces  (0.0.0.0)"), QString());

    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp)) { continue; }
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) { continue; }
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            // Skip link-local IPv6 (fe80::…) — rarely useful for testing
            if (addr.isLinkLocal()) { continue; }
            const QString display = QStringLiteral("%1   %2")
                .arg(iface.humanReadableName(), addr.toString());
            m_nicSelector->addItem(display, addr.toString());
        }
    }
}

// ---------------------------------------------------------------------------
QWidget *TestPage::buildResultsArea()
{
    auto *w  = new QWidget(this);
    auto *vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    m_resultTabBar = new QTabBar(w);
    m_resultTabBar->addTab(QStringLiteral("Overview"));
    m_resultTabBar->addTab(QStringLiteral("Details"));
    m_resultTabBar->addTab(QStringLiteral("Raw Output"));
    vl->addWidget(m_resultTabBar);

    auto *line = new QFrame(w);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    vl->addWidget(line);

    m_resultsStack = new QStackedWidget(w);
    m_resultsStack->addWidget(buildOverviewTab());
    m_resultsStack->addWidget(buildDetailsTab());
    m_resultsStack->addWidget(buildRawTab());
    vl->addWidget(m_resultsStack, 1);

    connect(m_resultTabBar, &QTabBar::currentChanged,
            this, &TestPage::onResultTabChanged);
    return w;
}

// ---------------------------------------------------------------------------
QWidget *TestPage::buildOverviewTab()
{
    auto *w    = new QWidget(this);
    auto *grid = new QGridLayout(w);
    grid->setSpacing(16);
    grid->setContentsMargins(12, 12, 12, 12);

    m_ovPeak   = makeBigMetricLabel(QStringLiteral("Peak Throughput"),   w);
    m_ovStable = makeBigMetricLabel(QStringLiteral("Stable Throughput"), w);
    m_ovLoss   = makeBigMetricLabel(QStringLiteral("Loss"),              w);
    m_ovJitter = makeBigMetricLabel(QStringLiteral("Jitter / Retrans"),  w);

    grid->addWidget(m_ovPeak,   0, 0);
    grid->addWidget(m_ovStable, 0, 1);
    grid->addWidget(m_ovLoss,   0, 2);
    grid->addWidget(m_ovJitter, 0, 3);
    for (int c = 0; c < 4; ++c) { grid->setColumnStretch(c, 1); }
    return w;
}

// ---------------------------------------------------------------------------
QWidget *TestPage::buildDetailsTab()
{
    m_intervalTable = new QTableWidget(0, 5, this);
    m_intervalTable->setHorizontalHeaderLabels({
        QStringLiteral("Time (s)"),
        QStringLiteral("Throughput"),
        QStringLiteral("Retrans / Lost"),
        QStringLiteral("Jitter"),
        QStringLiteral("Dir"),
    });
    m_intervalTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_intervalTable->verticalHeader()->setVisible(false);
    m_intervalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_intervalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    return m_intervalTable;
}

// ---------------------------------------------------------------------------
QWidget *TestPage::buildRawTab()
{
    m_rawOutput = new QPlainTextEdit(this);
    m_rawOutput->setReadOnly(true);
    m_rawOutput->setFont(QFont(QStringLiteral("Courier New"), 9));
    m_rawOutput->setMaximumBlockCount(5000);
    return m_rawOutput;
}

// ---------------------------------------------------------------------------
void TestPage::bindBridge(IperfCoreBridge *bridge)
{
    m_bridge = bridge;
    if (!bridge) { return; }
    connect(bridge, &IperfCoreBridge::runningChanged,
            this, &TestPage::onBridgeRunningChanged);
    connect(bridge, &IperfCoreBridge::eventReceived,
            this, &TestPage::onEventReceived);
    connect(bridge, &IperfCoreBridge::sessionCompleted,
            this, &TestPage::onSessionCompleted);
}

// ---------------------------------------------------------------------------
void TestPage::loadSettings(QSettings &s)
{
    if (m_serverAddress) {
        m_serverAddress->setText(s.value(QStringLiteral("test/serverAddress")).toString());
    }
    // NIC selection: try to restore by stored IP address
    if (m_nicSelector) {
        const QString savedIp = s.value(QStringLiteral("test/nicAddress")).toString();
        if (!savedIp.isEmpty()) {
            const int idx = m_nicSelector->findData(savedIp);
            if (idx >= 0) { m_nicSelector->setCurrentIndex(idx); }
        }
    }
}

void TestPage::saveSettings(QSettings &s) const
{
    if (m_serverAddress) {
        s.setValue(QStringLiteral("test/serverAddress"), m_serverAddress->text());
    }
    if (m_nicSelector) {
        s.setValue(QStringLiteral("test/nicAddress"), m_nicSelector->currentData().toString());
    }
}

// ---------------------------------------------------------------------------
void TestPage::setExpertMode(bool expert)
{
    m_expertMode = expert;
    m_expertPanel->setVisible(expert);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
void TestPage::onRoleChanged()
{
    const bool isClient = m_clientBtn->isChecked();
    m_roleStack->setCurrentIndex(isClient ? 0 : 1);
    m_startBtn->setText(isClient
        ? QStringLiteral("▶  Start Test")
        : QStringLiteral("▶  Start Server"));
}

void TestPage::onTrafficModeChanged()
{
    m_trafficModeStack->setCurrentIndex(m_mixedModeBtn->isChecked() ? 1 : 0);
}

// ---------------------------------------------------------------------------
void TestPage::onStartClicked()
{
    if (!m_bridge) { return; }
    if (m_bridge->isRunning() || m_orchestrator != nullptr) { return; }

    const bool isClient = m_clientBtn->isChecked();

    if (isClient && m_serverAddress->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Missing Input"),
                             QStringLiteral("Please enter a Server Address."));
        return;
    }

    // Mixed mode v1 notice
    if (isClient && m_mixedModeBtn->isChecked()) {
        int total = 0;
        for (const auto &row : m_mixRows) { total += row.ratioSpin->value(); }
        if (total != 100) {
            QMessageBox::warning(this, QStringLiteral("Mixed Mode"),
                QStringLiteral("Traffic ratios must sum to 100%% (current: %1%%).").arg(total));
            return;
        }
        QMessageBox::information(this, QStringLiteral("Mixed Mode — v1 Notice"),
            QStringLiteral("Full parallel multi-stream mixed traffic is planned for v2.\n"
                           "This test will run the dominant traffic type."));
    }

    m_baseConfig = buildConfig();
    m_bridge->setConfiguration(m_baseConfig);

    setControlsEnabled(false);
    m_stopBtn->setEnabled(true);
    m_exportBtn->setEnabled(false);
    m_rawOutput->clear();
    m_intervalTable->setRowCount(0);
    m_runningPeakBps = 0.0;

    // Reset overview
    setMetricLabel(m_ovPeak,   QStringLiteral("Peak Throughput"),   QStringLiteral("—"));
    setMetricLabel(m_ovStable, QStringLiteral("Stable Throughput"), QStringLiteral("—"));
    setMetricLabel(m_ovLoss,   QStringLiteral("Loss"),              QStringLiteral("—"));
    setMetricLabel(m_ovJitter, QStringLiteral("Jitter / Retrans"),  QStringLiteral("—"));

    if (isClient) {
        m_phase = Phase::Probing;
        setStatus(QStringLiteral("Probing optimal load…"));

        m_orchestrator = new IperfTestOrchestrator(m_bridge, this);
        connect(m_orchestrator, &IperfTestOrchestrator::stepStarted,
                this, &TestPage::onOrchestratorStepStarted);
        connect(m_orchestrator, &IperfTestOrchestrator::stepCompleted,
                this, &TestPage::onOrchestratorStepCompleted);
        connect(m_orchestrator, &IperfTestOrchestrator::foundMaxThroughput,
                this, &TestPage::onOrchestratorFoundMax);
        connect(m_orchestrator, &IperfTestOrchestrator::orchestrationFinished,
                this, &TestPage::onOrchestratorFinished);

        m_orchestrator->startClimb(m_baseConfig);
    } else {
        m_phase = Phase::Sustaining;
        m_serverPersist = true;   // keep restarting after each client session
        setStatus(QStringLiteral("Server listening\u2026"));
        m_bridge->start();
    }
}

// ---------------------------------------------------------------------------
void TestPage::onStopClicked()
{
    m_serverPersist = false;   // prevent server auto-restart on this stop
    if (m_orchestrator != nullptr && m_orchestrator->isRunning()) {
        m_orchestrator->abort();
    } else if (m_bridge != nullptr && m_bridge->isRunning()) {
        m_bridge->stop();
    }
    setStatus(QStringLiteral("Stopping\u2026"));
}

// ---------------------------------------------------------------------------
void TestPage::onExportClicked()
{
    if (!m_hasSession) { return; }
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export CSV"),
        QStringLiteral("%1/iperf_%2.csv")
            .arg(QDir::homePath(),
                 m_lastSession.startedAt.toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        QStringLiteral("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) { return; }
    QString err;
    if (!writeTextFile(path, buildCsvContent(), &err)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), err);
    }
}

// ---------------------------------------------------------------------------
void TestPage::onAddMixRow()
{
    addMixRow();
}

void TestPage::updateMixTotal()
{
    int total = 0;
    for (const auto &row : m_mixRows) { total += row.ratioSpin->value(); }
    const bool ok = (total == 100);
    m_mixTotalLabel->setText(
        QStringLiteral("<span style='color:%1;'>Total: %2%%</span>")
        .arg(ok ? QStringLiteral("green") : QStringLiteral("red"))
        .arg(total));
}

// ---------------------------------------------------------------------------
void TestPage::onBridgeRunningChanged(bool running)
{
    if (running) {
        // Bridge started a new probe step or the sustain phase — keep UI locked.
        if (m_phase == Phase::Probing) {
            // Each probe step is a fresh 5-second run; clear the table so
            // timestamps don't repeat (0–5 s, then 0–5 s again, …).
            m_intervalTable->setRowCount(0);
        }
        if (m_phase == Phase::Probing || m_phase == Phase::Sustaining) {
            setControlsEnabled(false);
            m_stopBtn->setEnabled(true);
        }
    } else {
        // Bridge stopped.
        if (m_phase == Phase::Sustaining) {
            // Check whether the stop was user-initiated (explicit Stop click sets
            // the bridge status to "Stopped"/"Stopping") vs natural completion.
            const QString bStatus = m_bridge ? m_bridge->statusText() : QString();
            const bool explicitStop = bStatus.startsWith(QStringLiteral("Stop"));

            if (m_serverPersist && m_serverBtn && m_serverBtn->isChecked() && !explicitStop) {
                // Server mode: session finished naturally — schedule an auto-
                // restart so the server keeps accepting clients without a manual
                // Start click.
                //
                // IMPORTANT: this slot is invoked synchronously (direct
                // connection, same thread) from inside
                // IperfCoreBridge::finishSessionOnGuiThread(), which still holds
                // m_mutex at this point.  Calling bridge->start() here directly
                // would try to re-lock the same non-recursive mutex → deadlock.
                // QTimer::singleShot(0) defers to the next event-loop iteration,
                // after finishSessionOnGuiThread() has returned and released the
                // lock.
                setStatus(QStringLiteral("Waiting for client\u2026"));
                QTimer::singleShot(0, this, [this]() {
                    if (!m_serverPersist || !m_bridge || m_bridge->isRunning()) {
                        return; // stop was requested or bridge already restarted
                    }
                    m_intervalTable->setRowCount(0);
                    m_rawOutput->clear();
                    m_runningPeakBps = 0.0;
                    setMetricLabel(m_ovPeak,   QStringLiteral("Peak Throughput"),   QStringLiteral("—"));
                    setMetricLabel(m_ovStable, QStringLiteral("Stable Throughput"), QStringLiteral("—"));
                    setMetricLabel(m_ovLoss,   QStringLiteral("Loss"),              QStringLiteral("—"));
                    setMetricLabel(m_ovJitter, QStringLiteral("Jitter / Retrans"),  QStringLiteral("—"));
                    m_bridge->start();
                });
                return;
            }

            // Sustain finished (or explicitly stopped) — back to Idle.
            m_serverPersist = false;
            m_phase = Phase::Idle;
            setControlsEnabled(true);
            m_stopBtn->setEnabled(false);
            m_exportBtn->setEnabled(m_hasSession);
        }
        // Phase::Probing: the orchestrator will call orchestrationFinished
        // when done; leave controls locked until that arrives.
    }
}

// ---------------------------------------------------------------------------
void TestPage::onEventReceived(const IperfGuiEvent &event)
{
    if (event.kind == IperfEventKind::Interval) {
        addIntervalRow(event);
        applyOverviewFromEvent(event);
    }
    if (!event.rawJson.isEmpty()) {
        m_rawOutput->appendPlainText(event.rawJson);
    } else if (!event.message.isEmpty()) {
        m_rawOutput->appendPlainText(event.message);
    }
}

// ---------------------------------------------------------------------------
void TestPage::onSessionCompleted(const IperfSessionRecord &record)
{
    m_lastSession = record;
    m_hasSession  = true;
    applyOverviewFromSession(record);
    emit sessionRecorded(record);

    if (m_phase == Phase::Sustaining) {
        m_exportBtn->setEnabled(true);
        setStatus(QStringLiteral("Completed — Peak: %1  Stable: %2")
            .arg(iperfHumanBitsPerSecond(record.peakBps),
                 iperfHumanBitsPerSecond(record.stableBps)));
    }
    // Phase::Probing: each probe step also emits sessionCompleted, but the
    // orchestrator owns the state machine — do not touch controls here.
}

// ---------------------------------------------------------------------------
void TestPage::onOrchestratorStepStarted(int step, const QString &description)
{
    Q_UNUSED(step)
    setStatus(QStringLiteral("Probing… %1").arg(description));
}

void TestPage::onOrchestratorStepCompleted(int step, double stableBps, double lossPercent)
{
    setStatus(QStringLiteral("Probe step %1 → %2  loss %3%")
        .arg(step + 1)
        .arg(iperfHumanBitsPerSecond(stableBps))
        .arg(lossPercent, 0, 'f', 2));
}

void TestPage::onOrchestratorFoundMax(double stableBps, double peakBps,
                                       int optimalParallel, double maxUdpBps)
{
    Q_UNUSED(peakBps)
    m_optimalParallel = optimalParallel;
    m_optimalUdpBps   = maxUdpBps;
    setStatus(QStringLiteral("Optimal: %1  (parallel=%2) — starting sustained test…")
        .arg(iperfHumanBitsPerSecond(stableBps))
        .arg(optimalParallel));
}

void TestPage::onOrchestratorFinished(bool aborted)
{
    if (m_orchestrator) {
        m_orchestrator->deleteLater();
        m_orchestrator = nullptr;
    }

    if (aborted) {
        m_phase = Phase::Idle;
        setControlsEnabled(true);
        m_stopBtn->setEnabled(false);
        m_exportBtn->setEnabled(m_hasSession);
        setStatus(QStringLiteral("Stopped"));
        return;
    }

    // Start the sustained phase at optimal load.
    // Clear probe-phase artifacts from the UI so only sustained results appear.
    m_intervalTable->setRowCount(0);
    m_rawOutput->clear();
    m_runningPeakBps = 0.0;
    setMetricLabel(m_ovPeak,   QStringLiteral("Peak Throughput"),   QStringLiteral("—"));
    setMetricLabel(m_ovStable, QStringLiteral("Stable Throughput"), QStringLiteral("—"));
    setMetricLabel(m_ovLoss,   QStringLiteral("Loss"),              QStringLiteral("—"));
    setMetricLabel(m_ovJitter, QStringLiteral("Jitter / Retrans"),  QStringLiteral("—"));

    IperfGuiConfig cfg = m_baseConfig;
    cfg.parallel  = m_optimalParallel;
    cfg.duration  = durationPresetToSeconds(m_baseConfig.durationPreset);
    if (m_baseConfig.trafficType == TrafficType::Udp && m_optimalUdpBps > 0.0) {
        cfg.bitrateBps = static_cast<quint64>(m_optimalUdpBps);
    }

    m_phase = Phase::Sustaining;
    setStatus(QStringLiteral("Sustaining at optimal load (parallel=%1)\u2026")
        .arg(m_optimalParallel));

    m_bridge->setConfiguration(cfg);
    m_bridge->start();
}

void TestPage::onResultTabChanged(int index)
{
    m_resultsStack->setCurrentIndex(index);
}

// ---------------------------------------------------------------------------
// Config builder
// ---------------------------------------------------------------------------
IperfGuiConfig TestPage::buildConfig() const
{
    IperfGuiConfig cfg;

    const bool isClient = m_clientBtn->isChecked();
    cfg.mode = isClient ? IperfGuiConfig::Mode::Client : IperfGuiConfig::Mode::Server;

    // ── Server mode: only listen address + port matter.
    //    Traffic type, packet size, duration, direction are all determined
    //    by the remote client; do not read Client-side widgets here.
    if (!isClient) {
        // Server mode: listen address comes from the NIC selector.
        // An empty data value means "all interfaces" (0.0.0.0).
        cfg.listenAddress = m_nicSelector
            ? m_nicSelector->currentData().toString()
            : QString();
        cfg.port = (m_expertMode && m_customPortSpin && m_customPortSpin->value() > 0)
                   ? m_customPortSpin->value() : 5201;
        if (m_expertMode && m_bindAddrEdit) {
            cfg.bindAddress = m_bindAddrEdit->text().trimmed();
        }
        cfg.jsonStream           = true;
        cfg.jsonStreamFullOutput = true;
        cfg.forceFlush           = true;
        return cfg;
    }

    // ── Client mode ──────────────────────────────────────────────────────────
    cfg.trafficMode = TrafficMode::Single; // Mixed is not yet implemented (v2)

    cfg.trafficType = m_trafficType
        ? m_trafficType->currentData().value<TrafficType>() : TrafficType::Tcp;
    cfg.packetSize  = m_packetSize
        ? m_packetSize->currentData().value<PacketSize>() : PacketSize::B1518;

    cfg.protocol  = (cfg.trafficType == TrafficType::Udp)
        ? IperfGuiConfig::Protocol::Udp : IperfGuiConfig::Protocol::Tcp;
    // blockSize = application write block (UDP≈datagram; TCP = app write, not wire frame)
    cfg.blockSize = packetSizeToBytes(cfg.packetSize, 1518);

    // Duration
    cfg.durationPreset = DurationPreset::H1;
    if (m_durationGroup) {
        if (auto *chk = m_durationGroup->checkedButton()) {
            cfg.durationPreset = chk->property("durationPreset").value<DurationPreset>();
        }
    }
    cfg.duration = durationPresetToSeconds(cfg.durationPreset);

    // Direction
    cfg.direction = Direction::Uplink;
    if (m_directionGroup) {
        if (auto *chk = m_directionGroup->checkedButton()) {
            cfg.direction = chk->property("direction").value<Direction>();
        }
    }
    cfg.reverse       = (cfg.direction == Direction::Downlink);
    cfg.bidirectional = (cfg.direction == Direction::Bidirectional);

    // Connection
    cfg.serverAddress = m_serverAddress ? m_serverAddress->text().trimmed() : QString();
    cfg.host          = cfg.serverAddress;

    // Port: always iperf default for TCP/UDP; expert override takes precedence
    cfg.port = (m_expertMode && m_customPortSpin && m_customPortSpin->value() > 0)
               ? m_customPortSpin->value() : 5201;

    // Expert overrides
    if (m_expertMode && m_bindAddrEdit) {
        cfg.bindAddress = m_bindAddrEdit->text().trimmed();
    }

    cfg.getServerOutput      = true;
    cfg.jsonStream           = true;
    cfg.jsonStreamFullOutput = true;
    cfg.udpCounters64Bit     = true;
    cfg.forceFlush           = true;

    return cfg;
}

// ---------------------------------------------------------------------------
int TestPage::packetSizeToBytes(PacketSize ps, int custom)
{
    switch (ps) {
    case PacketSize::B64:    return 64;
    case PacketSize::B128:   return 128;
    case PacketSize::B256:   return 256;
    case PacketSize::B512:   return 512;
    case PacketSize::B1024:  return 1024;
    case PacketSize::B1518:  return 1518;
    case PacketSize::Jumbo:  return 9000;
    case PacketSize::Custom: return qMax(64, custom);
    }
    return 1518;
}

int TestPage::durationPresetToSeconds(DurationPreset dp)
{
    switch (dp) {
    case DurationPreset::Min5:       return 300;
    case DurationPreset::Min30:      return 1800;
    case DurationPreset::H1:         return 3600;
    case DurationPreset::H6:         return 21600;
    case DurationPreset::H24:        return 86400;
    case DurationPreset::Continuous: return 0;
    }
    return 3600;
}

int TestPage::defaultPortForTrafficType(TrafficType tt)
{
    Q_UNUSED(tt)
    // Always use the iperf3 default port.  HTTP/HTTPS/DNS/FTP traffic types
    // are not yet implemented and cannot be selected; do NOT pre-assign their
    // well-known ports (80/443/53/21) — that would risk hitting live services.
    return 5201;
}

// ---------------------------------------------------------------------------
void TestPage::addMixRow(TrafficType type, PacketSize ps, int ratio)
{
    MixRowWidgets row;
    row.container = new QWidget(m_mixContainer);
    auto *hl = new QHBoxLayout(row.container);
    hl->setContentsMargins(0, 2, 0, 2);
    hl->setSpacing(6);

    row.typeCombo = makeTrafficTypeCombo(row.container);
    row.sizeCombo = makePacketSizeCombo(row.container);

    row.ratioSpin = new QSpinBox(row.container);
    row.ratioSpin->setRange(1, 100);
    row.ratioSpin->setValue(ratio);
    row.ratioSpin->setSuffix(QStringLiteral("%"));
    row.ratioSpin->setFixedWidth(72);

    auto *removeBtn = new QPushButton(QStringLiteral("\xc3\x97"), row.container); // ×
    removeBtn->setFixedSize(24, 24);
    removeBtn->setToolTip(QStringLiteral("Remove row"));

    // Set initial selections
    if (int idx = row.typeCombo->findData(QVariant::fromValue(type)); idx >= 0) {
        row.typeCombo->setCurrentIndex(idx);
    }
    if (int idx = row.sizeCombo->findData(QVariant::fromValue(ps)); idx >= 0) {
        row.sizeCombo->setCurrentIndex(idx);
    }

    hl->addWidget(row.typeCombo, 2);
    hl->addWidget(row.sizeCombo, 2);
    hl->addWidget(row.ratioSpin, 1);
    hl->addWidget(removeBtn, 0);

    // Insert before trailing stretch
    m_mixLayout->insertWidget(m_mixLayout->count() - 1, row.container);
    m_mixRows.push_back(row);

    connect(row.ratioSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &TestPage::updateMixTotal);

    QWidget *cptr = row.container;
    connect(removeBtn, &QPushButton::clicked, this, [this, cptr]() {
        for (int i = 0; i < m_mixRows.size(); ++i) {
            if (m_mixRows[i].container == cptr) {
                m_mixLayout->removeWidget(cptr);
                cptr->deleteLater();
                m_mixRows.removeAt(i);
                updateMixTotal();
                break;
            }
        }
    });
    updateMixTotal();
}

// ---------------------------------------------------------------------------
QVector<TrafficMixEntry> TestPage::buildMixEntries() const
{
    QVector<TrafficMixEntry> out;
    for (const auto &row : m_mixRows) {
        TrafficMixEntry e;
        e.trafficType  = row.typeCombo->currentData().value<TrafficType>();
        e.packetSize   = row.sizeCombo->currentData().value<PacketSize>();
        e.ratioPercent = row.ratioSpin->value();
        out.push_back(e);
    }
    return out;
}

// ---------------------------------------------------------------------------
void TestPage::addIntervalRow(const IperfGuiEvent &event)
{
    const QStringList sumKeys = {
        QStringLiteral("sum"), QStringLiteral("sum_sent"), QStringLiteral("sum_received"),
    };
    QVariantMap sum;
    for (const QString &k : sumKeys) {
        const QVariant v = event.fields.value(k);
        if (v.isValid() && v.canConvert<QVariantMap>()) { sum = v.toMap(); break; }
    }
    if (sum.isEmpty()) { return; }

    // Rolling window: drop oldest rows above the cap to bound memory on long tests.
    constexpr int kMaxRows = 3600; // 1 h at 1-second intervals
    while (m_intervalTable->rowCount() >= kMaxRows) {
        m_intervalTable->removeRow(0);
    }
    const int row = m_intervalTable->rowCount();
    m_intervalTable->insertRow(row);

    auto mkItem = [](const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        return item;
    };

    const double tStart = sum.value(QStringLiteral("start")).toDouble();
    const double tEnd   = sum.value(QStringLiteral("end")).toDouble();
    m_intervalTable->setItem(row, 0,
        mkItem(QStringLiteral("%1–%2 s").arg(tStart, 0, 'f', 1).arg(tEnd, 0, 'f', 1)));

    m_intervalTable->setItem(row, 1,
        mkItem(iperfHumanBitsPerSecond(sum.value(QStringLiteral("bits_per_second")).toDouble())));

    if (sum.contains(QStringLiteral("retransmits"))) {
        m_intervalTable->setItem(row, 2,
            mkItem(QString::number(sum.value(QStringLiteral("retransmits")).toInt())));
    } else if (sum.contains(QStringLiteral("lost_packets"))) {
        m_intervalTable->setItem(row, 2,
            mkItem(QStringLiteral("%1 (%2%)")
                .arg(sum.value(QStringLiteral("lost_packets")).toInt())
                .arg(sum.value(QStringLiteral("lost_percent")).toDouble(), 0, 'f', 2)));
    } else {
        m_intervalTable->setItem(row, 2, mkItem(QStringLiteral("—")));
    }

    if (sum.contains(QStringLiteral("jitter_ms"))) {
        m_intervalTable->setItem(row, 3,
            mkItem(QStringLiteral("%1 ms")
                .arg(sum.value(QStringLiteral("jitter_ms")).toDouble(), 0, 'f', 3)));
    } else {
        m_intervalTable->setItem(row, 3, mkItem(QStringLiteral("—")));
    }

    const QString dk = event.fields.value(QStringLiteral("summary_key")).toString();
    // Map iperf3 summary_key names to readable direction arrows (U+2191 ↑ / U+2193 ↓ / U+2195 ↕).
    // Avoid raw UTF-8 byte sequences inside QStringLiteral — use \uXXXX escapes instead.
    QString dirText;
    if (dk.isEmpty() || dk == QLatin1String("sum")) {
        dirText = QStringLiteral("\u2192");          // → (unknown / single-dir)
    } else if (dk.contains(QLatin1String("reverse")) || dk == QLatin1String("sum_received")) {
        dirText = QStringLiteral("\u2193");          // ↓ (downlink / reverse)
    } else if (dk == QLatin1String("sum_sent")) {
        dirText = QStringLiteral("\u2191");          // ↑ (uplink / forward)
    } else {
        dirText = dk;                                // raw key (future iperf versions)
    }
    m_intervalTable->setItem(row, 4, mkItem(dirText));

    m_intervalTable->scrollToBottom();
}

// ---------------------------------------------------------------------------
void TestPage::applyOverviewFromEvent(const IperfGuiEvent &event)
{
    const QStringList sumKeys = {
        QStringLiteral("sum"), QStringLiteral("sum_sent"), QStringLiteral("sum_received"),
    };
    for (const QString &k : sumKeys) {
        const QVariant v = event.fields.value(k);
        if (!v.isValid() || !v.canConvert<QVariantMap>()) { continue; }
        const QVariantMap sum = v.toMap();
        if (sum.contains(QStringLiteral("bits_per_second"))) {
            const double bps = sum.value(QStringLiteral("bits_per_second")).toDouble();
            // Track the running maximum so the "Peak" card shows the highest value seen
            if (bps > m_runningPeakBps) {
                m_runningPeakBps = bps;
                setMetricLabel(m_ovPeak, QStringLiteral("Peak Throughput"),
                    iperfHumanBitsPerSecond(m_runningPeakBps));
            }
            // Show the current interval throughput in the Stable card during the test
            setMetricLabel(m_ovStable, QStringLiteral("Current"),
                iperfHumanBitsPerSecond(bps));
        }
        if (sum.contains(QStringLiteral("jitter_ms"))) {
            setMetricLabel(m_ovJitter, QStringLiteral("Jitter"),
                QStringLiteral("%1 ms")
                .arg(sum.value(QStringLiteral("jitter_ms")).toDouble(), 0, 'f', 3));
        }
        if (sum.contains(QStringLiteral("retransmits"))) {
            setMetricLabel(m_ovJitter, QStringLiteral("Retrans"),
                QString::number(sum.value(QStringLiteral("retransmits")).toInt()));
        }
        if (sum.contains(QStringLiteral("lost_percent"))) {
            setMetricLabel(m_ovLoss, QStringLiteral("Loss"),
                iperfHumanPercent(sum.value(QStringLiteral("lost_percent")).toDouble()));
        }
        break;
    }
}

// ---------------------------------------------------------------------------
void TestPage::applyOverviewFromSession(const IperfSessionRecord &record)
{
    setMetricLabel(m_ovPeak,   QStringLiteral("Peak Throughput"),
                   iperfHumanBitsPerSecond(record.peakBps));
    setMetricLabel(m_ovStable, QStringLiteral("Stable Throughput"),
                   iperfHumanBitsPerSecond(record.stableBps));
    setMetricLabel(m_ovLoss,   QStringLiteral("Loss"),
                   record.lossPercent > 0.0
                   ? iperfHumanPercent(record.lossPercent)
                   : QStringLiteral("—"));
}

// ---------------------------------------------------------------------------
void TestPage::setStatus(const QString &text)
{
    m_statusLabel->setText(text);
}

// ---------------------------------------------------------------------------
void TestPage::setControlsEnabled(bool enabled)
{
    m_startBtn->setEnabled(enabled);
    m_clientBtn->setEnabled(enabled);
    m_serverBtn->setEnabled(enabled);
    m_singleModeBtn->setEnabled(enabled);
    m_mixedModeBtn->setEnabled(enabled);
    if (m_serverAddress) { m_serverAddress->setEnabled(enabled); }
    if (m_nicSelector)   { m_nicSelector->setEnabled(enabled); }
    if (m_trafficType)   { m_trafficType->setEnabled(enabled); }
    if (m_packetSize)    { m_packetSize->setEnabled(enabled); }
    if (m_durationGroup) {
        for (auto *btn : m_durationGroup->buttons()) { btn->setEnabled(enabled); }
    }
    if (m_directionGroup) {
        for (auto *btn : m_directionGroup->buttons()) { btn->setEnabled(enabled); }
    }
    for (const auto &row : m_mixRows) {
        if (row.container) { row.container->setEnabled(enabled); }
    }
}

// ---------------------------------------------------------------------------
QString TestPage::buildCsvContent() const
{
    const int rows = m_intervalTable->rowCount();
    const int cols = m_intervalTable->columnCount();
    QString csv;
    QTextStream ts(&csv);

    QStringList hdrs;
    for (int c = 0; c < cols; ++c) {
        auto *h = m_intervalTable->horizontalHeaderItem(c);
        hdrs << (h ? h->text() : QString());
    }
    ts << hdrs.join(QLatin1Char(',')) << QLatin1Char('\n');

    for (int r = 0; r < rows; ++r) {
        QStringList cells;
        for (int c = 0; c < cols; ++c) {
            auto *item = m_intervalTable->item(r, c);
            QString cell = item ? item->text() : QString();
            if (cell.contains(QLatin1Char(',')) || cell.contains(QLatin1Char('"'))) {
                cell = QStringLiteral("\"%1\"")
                    .arg(cell.replace(QLatin1Char('"'), QStringLiteral("\"\"")));
            }
            cells << cell;
        }
        ts << cells.join(QLatin1Char(',')) << QLatin1Char('\n');
    }
    return csv;
}

// ============================================================================
// HistoryPage
// ============================================================================
HistoryPage::HistoryPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(8);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    m_list = new QListWidget(splitter);
    m_list->setMinimumWidth(260);
    splitter->addWidget(m_list);

    m_detail = new QPlainTextEdit(splitter);
    m_detail->setReadOnly(true);
    m_detail->setFont(QFont(QStringLiteral("Courier New"), 9));
    m_detail->setPlaceholderText(QStringLiteral("Select a session to view details"));
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    auto *bar = new QHBoxLayout;
    m_exportJson = new QPushButton(QStringLiteral("Export JSON"), this);
    m_exportCsv  = new QPushButton(QStringLiteral("Export CSV"),  this);
    m_clearBtn   = new QPushButton(QStringLiteral("Clear All"),   this);
    m_exportJson->setEnabled(false);
    m_exportCsv->setEnabled(false);
    m_clearBtn->setEnabled(false);
    bar->addWidget(m_exportJson);
    bar->addWidget(m_exportCsv);
    bar->addStretch();
    bar->addWidget(m_clearBtn);
    root->addLayout(bar);

    connect(m_list, &QListWidget::currentRowChanged,
            this, &HistoryPage::onSelectionChanged);
    connect(m_exportJson, &QPushButton::clicked, this, &HistoryPage::onExportJson);
    connect(m_exportCsv,  &QPushButton::clicked, this, &HistoryPage::onExportCsv);
    connect(m_clearBtn,   &QPushButton::clicked, this, &HistoryPage::onClearAll);
}

void HistoryPage::bindBridge(IperfCoreBridge *bridge) { m_bridge = bridge; }

void HistoryPage::appendSession(const IperfSessionRecord &record)
{
    // Enforce the retention cap from Settings → Result Retention.
    // Read fresh from QSettings each time so a change takes effect immediately
    // without requiring a restart.
    {
        QSettings s;
        s.beginGroup(QStringLiteral("preferences"));
        const int retention = qBound(1, s.value(QStringLiteral("retention"), 200).toInt(), 2000);
        s.endGroup();

        while (m_records.size() >= retention) {
            m_records.removeFirst();
            delete m_list->takeItem(0);
        }
    }

    m_records.push_back(record);
    m_list->addItem(buildSessionSummaryLine(record));
    m_clearBtn->setEnabled(true);
}

void HistoryPage::onSelectionChanged()
{
    const int idx = m_list->currentRow();
    if (idx < 0 || idx >= m_records.size()) {
        m_detail->clear();
        m_exportJson->setEnabled(false);
        m_exportCsv->setEnabled(false);
        return;
    }
    m_detail->setPlainText(buildDetailText(m_records.at(idx)));
    m_exportJson->setEnabled(true);
    m_exportCsv->setEnabled(true);
}

void HistoryPage::onExportJson()
{
    const int idx = m_list->currentRow();
    if (idx < 0 || idx >= m_records.size()) { return; }
    const auto &rec = m_records.at(idx);
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export JSON"),
        QStringLiteral("%1/iperf_%2.json")
            .arg(QDir::homePath(), rec.startedAt.toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        QStringLiteral("JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) { return; }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), f.errorString());
        return;
    }
    f.write(rec.rawJson.toUtf8());
}

void HistoryPage::onExportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export CSV"),
        QDir::homePath() + QStringLiteral("/iperf_history.csv"),
        QStringLiteral("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) { return; }
    QString err;
    if (!writeTextFile(path, buildCsvContent(), &err)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), err);
    }
}

void HistoryPage::onClearAll()
{
    if (QMessageBox::question(
            this, QStringLiteral("Clear History"),
            QStringLiteral("Delete all %1 session records?").arg(m_records.size()),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) { return; }
    m_records.clear();
    m_list->clear();
    m_detail->clear();
    m_exportJson->setEnabled(false);
    m_exportCsv->setEnabled(false);
    m_clearBtn->setEnabled(false);
    if (m_bridge) { m_bridge->clearHistory(); }
}

QString HistoryPage::buildSessionSummaryLine(const IperfSessionRecord &record) const
{
    const bool isSrv = (record.config.mode == IperfGuiConfig::Mode::Server);
    const QString proto = (record.config.protocol == IperfGuiConfig::Protocol::Udp)
        ? QStringLiteral("UDP") : QStringLiteral("TCP");
    const QString ep = isSrv
        ? (record.config.listenAddress.isEmpty() ? QStringLiteral("0.0.0.0") : record.config.listenAddress)
        : record.config.serverAddress;
    return QStringLiteral("[%1]  %2  %3  %4  Peak %5")
        .arg(record.startedAt.toString(QStringLiteral("MM-dd HH:mm")),
             isSrv ? QStringLiteral("SRV") : QStringLiteral("CLT"),
             proto, ep,
             iperfHumanBitsPerSecond(record.peakBps));
}

QString HistoryPage::buildDetailText(const IperfSessionRecord &record) const
{
    QString out;
    QTextStream ts(&out);
    ts << "Time:     " << record.startedAt.toString(Qt::ISODate) << "\n"
       << "Status:   " << record.statusText << "\n"
       << "Mode:     " << (record.config.mode == IperfGuiConfig::Mode::Server ? "Server" : "Client") << "\n"
       << "Protocol: " << (record.config.protocol == IperfGuiConfig::Protocol::Udp ? "UDP" : "TCP") << "\n"
       << "Endpoint: " << record.config.host << ":" << record.config.port << "\n"
       << "Duration: " << record.config.duration << " s\n"
       << "Parallel: " << record.config.parallel << "\n"
       << "\n"
       << "Peak:     " << iperfHumanBitsPerSecond(record.peakBps) << "\n"
       << "Stable:   " << iperfHumanBitsPerSecond(record.stableBps) << "\n";
    if (record.lossPercent > 0.0) {
        ts << "Loss:     " << iperfHumanPercent(record.lossPercent) << "\n";
    }
    ts << "\n--- Raw JSON ---\n" << record.rawJson;
    return out;
}

QString HistoryPage::buildCsvContent() const
{
    QString csv;
    QTextStream ts(&csv);
    ts << "Time,Mode,Protocol,Endpoint,Duration(s),Parallel,Peak(bps),Stable(bps),Loss(%),Status\n";
    for (const auto &rec : m_records) {
        ts << rec.startedAt.toString(Qt::ISODate) << ","
           << (rec.config.mode == IperfGuiConfig::Mode::Server ? "Server" : "Client") << ","
           << (rec.config.protocol == IperfGuiConfig::Protocol::Udp ? "UDP" : "TCP") << ","
           << rec.config.host << ":" << rec.config.port << ","
           << rec.config.duration << "," << rec.config.parallel << ","
           << rec.peakBps << "," << rec.stableBps << "," << rec.lossPercent << ","
           << "\"" << QString(rec.statusText).replace(QLatin1Char('"'), QStringLiteral("\"\"")) << "\"\n";
    }
    return csv;
}

// ============================================================================
// SettingsPage
// ============================================================================
SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);

    auto *fl = new QFormLayout;
    fl->setSpacing(10);

    m_theme = new QComboBox(this);
    m_theme->addItem(QStringLiteral("System default"));
    m_theme->addItem(QStringLiteral("Light"));
    m_theme->addItem(QStringLiteral("Dark"));
    fl->addRow(QStringLiteral("Theme:"), m_theme);

    m_retentionSpin = new QSpinBox(this);
    m_retentionSpin->setRange(10, 2000);
    m_retentionSpin->setValue(200);
    m_retentionSpin->setSuffix(QStringLiteral(" sessions"));
    fl->addRow(QStringLiteral("Result Retention:"), m_retentionSpin);

    auto *pathRow = new QHBoxLayout;
    m_exportFolder = new QLineEdit(this);
    m_exportFolder->setPlaceholderText(QDir::homePath());
    m_browseBtn = new QPushButton(QStringLiteral("Browse\u2026"), this);
    pathRow->addWidget(m_exportFolder, 1);
    pathRow->addWidget(m_browseBtn);
    fl->addRow(QStringLiteral("Default Export Folder:"), pathRow);

    root->addLayout(fl);

    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    m_expertCheck = new QCheckBox(
        QStringLiteral("Show Expert Controls  (custom port, bind address, force IPv4/IPv6)"),
        this);
    root->addWidget(m_expertCheck);

    auto *btnRow = new QHBoxLayout;
    auto *applyBtn = new QPushButton(QStringLiteral("Apply"), this);
    auto *resetBtn = new QPushButton(QStringLiteral("Reset to Defaults"), this);
    btnRow->addStretch();
    btnRow->addWidget(applyBtn);
    btnRow->addWidget(resetBtn);
    root->addLayout(btnRow);
    root->addStretch();

    auto *sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep2);

    // ── About section ────────────────────────────────────────────────────────
    m_buildInfo   = new QLabel(this);
    m_runtimeInfo = new QLabel(this);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#0a0;"));
    root->addWidget(m_buildInfo);
    root->addWidget(m_runtimeInfo);
    root->addWidget(m_statusLabel);

    m_buildInfo->setText(
        QStringLiteral("Build: IperfWin v1.0  (libiperf 3.21+, Qt %1)")
        .arg(QString::fromLatin1(QT_VERSION_STR)));
    m_runtimeInfo->setText(
        QStringLiteral("Platform: %1 / %2")
        .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()));

    auto *aboutBtn = new QPushButton(QStringLiteral("About IperfWin\xe2\x80\xa6"), this);
    aboutBtn->setFixedWidth(160);
    connect(aboutBtn, &QPushButton::clicked, this, [this]() {
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
    });
    root->addWidget(aboutBtn);

    connect(applyBtn,    &QPushButton::clicked, this, &SettingsPage::onApply);
    connect(resetBtn,    &QPushButton::clicked, this, &SettingsPage::onReset);
    connect(m_browseBtn, &QPushButton::clicked, this, &SettingsPage::onBrowseExportFolder);
    connect(m_expertCheck, &QCheckBox::toggled, this, &SettingsPage::expertModeChanged);
}

void SettingsPage::bindBridge(IperfCoreBridge *bridge) { m_bridge = bridge; }

void SettingsPage::loadSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("preferences"));
    m_theme->setCurrentIndex(s.value(QStringLiteral("theme"), 0).toInt());
    m_retentionSpin->setValue(s.value(QStringLiteral("retention"), 200).toInt());
    m_exportFolder->setText(s.value(QStringLiteral("exportFolder")).toString());
    m_expertCheck->setChecked(s.value(QStringLiteral("expertMode"), false).toBool());
    s.endGroup();
}

void SettingsPage::saveSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("preferences"));
    s.setValue(QStringLiteral("theme"),        m_theme->currentIndex());
    s.setValue(QStringLiteral("retention"),    m_retentionSpin->value());
    s.setValue(QStringLiteral("exportFolder"), m_exportFolder->text());
    s.setValue(QStringLiteral("expertMode"),   m_expertCheck->isChecked());
    s.endGroup();
    s.sync();
}

void SettingsPage::onApply()
{
    saveSettings();
    m_statusLabel->setText(QStringLiteral("Settings saved."));
    QTimer::singleShot(2500, m_statusLabel, [this]() { m_statusLabel->clear(); });
    emit expertModeChanged(m_expertCheck->isChecked());
}

void SettingsPage::onReset()
{
    m_theme->setCurrentIndex(0);
    m_retentionSpin->setValue(200);
    m_exportFolder->clear();
    m_expertCheck->setChecked(false);
    onApply();
}

void SettingsPage::onBrowseExportFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Export Folder"),
        m_exportFolder->text().isEmpty() ? QDir::homePath() : m_exportFolder->text());
    if (!dir.isEmpty()) { m_exportFolder->setText(dir); }
}
