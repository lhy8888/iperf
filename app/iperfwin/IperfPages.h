#pragma once

#include "IperfGuiTypes.h"

#include <QWidget>

class IperfCoreBridge;
class IperfTestOrchestrator;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QStackedWidget;
class QTabBar;
class QTableWidget;
class QVBoxLayout;

// ---------------------------------------------------------------------------
// TestPage — unified test control page (Client + Server + inline results)
// ---------------------------------------------------------------------------
class TestPage : public QWidget
{
    Q_OBJECT
public:
    explicit TestPage(QWidget *parent = nullptr);

    void bindBridge(IperfCoreBridge *bridge);
    void loadSettings(QSettings &s);
    void saveSettings(QSettings &s) const;
    void setExpertMode(bool expert);

signals:
    void sessionRecorded(const IperfSessionRecord &record);

private slots:
    void onRoleChanged();
    void onTrafficModeChanged();
    void onStartClicked();
    void onStopClicked();
    void onExportClicked();
    void onAddMixRow();
    void updateMixTotal();
    void onBridgeRunningChanged(bool running);
    void onEventReceived(const IperfGuiEvent &event);
    void onSessionCompleted(const IperfSessionRecord &record);
    void onOrchestratorStepStarted(int step, const QString &description);
    void onOrchestratorStepCompleted(int step, double stableBps, double lossPercent);
    void onOrchestratorFoundMax(double stableBps, double peakBps, int optimalParallel, double maxUdpBps);
    void onOrchestratorFinished(bool aborted);
    void onResultTabChanged(int index);

private:
    QWidget *buildClientArea();
    QWidget *buildServerArea();
    QWidget *buildResultsArea();
    QWidget *buildOverviewTab();
    QWidget *buildDetailsTab();
    QWidget *buildRawTab();

    IperfGuiConfig buildConfig() const;
    static int     packetSizeToBytes(PacketSize ps, int custom);
    static int     durationPresetToSeconds(DurationPreset dp);
    static int     defaultPortForTrafficType(TrafficType tt);

    void addMixRow(TrafficType type = TrafficType::Tcp,
                   PacketSize ps   = PacketSize::B1518,
                   int ratio       = 25);
    QVector<TrafficMixEntry> buildMixEntries() const;

    void addIntervalRow(const IperfGuiEvent &event);
    void applyOverviewFromSession(const IperfSessionRecord &record);
    void applyOverviewFromEvent(const IperfGuiEvent &event);
    void setStatus(const QString &text);
    void setControlsEnabled(bool enabled);
    void populateNicSelector();
    QString buildCsvContent() const;

    // Bridge / orchestrator
    IperfCoreBridge       *m_bridge       = nullptr;
    IperfTestOrchestrator *m_orchestrator = nullptr;

    // Test state
    enum class Phase { Idle, Probing, Sustaining };
    Phase           m_phase          = Phase::Idle;
    IperfGuiConfig  m_baseConfig;
    int             m_optimalParallel = 1;
    double          m_optimalUdpBps   = 0.0;
    bool            m_expertMode      = false;

    // Role toggle
    QPushButton    *m_clientBtn   = nullptr;
    QPushButton    *m_serverBtn   = nullptr;
    QStackedWidget *m_roleStack   = nullptr;   // 0=client area, 1=server area

    // Client area
    QPushButton    *m_singleModeBtn      = nullptr;
    QPushButton    *m_mixedModeBtn       = nullptr;
    QLineEdit      *m_serverAddress      = nullptr;
    QStackedWidget *m_trafficModeStack   = nullptr;  // 0=single, 1=mixed

    // Single mode widgets
    QComboBox *m_trafficType = nullptr;
    QComboBox *m_packetSize  = nullptr;

    // Mixed mode widgets
    QWidget    *m_mixContainer  = nullptr;
    QVBoxLayout *m_mixLayout    = nullptr;
    QLabel     *m_mixTotalLabel = nullptr;

    struct MixRowWidgets {
        QWidget   *container = nullptr;
        QComboBox *typeCombo = nullptr;
        QComboBox *sizeCombo = nullptr;
        QSpinBox  *ratioSpin = nullptr;
    };
    QVector<MixRowWidgets> m_mixRows;

    // Duration / Direction button groups
    QButtonGroup *m_durationGroup  = nullptr;
    QButtonGroup *m_directionGroup = nullptr;

    // Server area – NIC selector (populated from QNetworkInterface)
    QComboBox *m_nicSelector = nullptr;

    // Expert panel
    QFrame    *m_expertPanel    = nullptr;
    QSpinBox  *m_customPortSpin = nullptr;
    QLineEdit *m_bindAddrEdit   = nullptr;

    // Action bar
    QPushButton *m_startBtn    = nullptr;
    QPushButton *m_stopBtn     = nullptr;
    QPushButton *m_exportBtn   = nullptr;
    QLabel      *m_statusLabel = nullptr;

    // Results area
    QTabBar        *m_resultTabBar  = nullptr;
    QStackedWidget *m_resultsStack  = nullptr;

    // Overview tab labels
    QLabel *m_ovPeak   = nullptr;
    QLabel *m_ovStable = nullptr;
    QLabel *m_ovLoss   = nullptr;
    QLabel *m_ovJitter = nullptr;

    // Details tab
    QTableWidget *m_intervalTable = nullptr;

    // Raw tab
    QPlainTextEdit *m_rawOutput = nullptr;

    IperfSessionRecord m_lastSession;
    double m_runningPeakBps = 0.0; // updated live during the sustain phase
    bool m_hasSession = false;
};

// ---------------------------------------------------------------------------
// HistoryPage — past session records
// ---------------------------------------------------------------------------
class HistoryPage : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPage(QWidget *parent = nullptr);

    void bindBridge(IperfCoreBridge *bridge);
    void appendSession(const IperfSessionRecord &record);

private slots:
    void onSelectionChanged();
    void onExportJson();
    void onExportCsv();
    void onClearAll();

private:
    QString buildSessionSummaryLine(const IperfSessionRecord &record) const;
    QString buildDetailText(const IperfSessionRecord &record) const;
    QString buildCsvContent() const;

    IperfCoreBridge *m_bridge = nullptr;

    QListWidget    *m_list      = nullptr;
    QPlainTextEdit *m_detail    = nullptr;
    QPushButton    *m_exportJson = nullptr;
    QPushButton    *m_exportCsv  = nullptr;
    QPushButton    *m_clearBtn   = nullptr;

    QVector<IperfSessionRecord> m_records;
};

// ---------------------------------------------------------------------------
// SettingsPage — application preferences + expert mode toggle
// ---------------------------------------------------------------------------
class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

    void bindBridge(IperfCoreBridge *bridge);
    void loadSettings();
    void saveSettings();

signals:
    void expertModeChanged(bool enabled);

private slots:
    void onApply();
    void onReset();
    void onBrowseExportFolder();

private:
    IperfCoreBridge *m_bridge = nullptr;

    QComboBox   *m_theme         = nullptr;
    QSpinBox    *m_retentionSpin = nullptr;
    QLineEdit   *m_exportFolder  = nullptr;
    QPushButton *m_browseBtn     = nullptr;
    QCheckBox   *m_expertCheck   = nullptr;
    QLabel      *m_buildInfo     = nullptr;
    QLabel      *m_runtimeInfo   = nullptr;
    QLabel      *m_statusLabel   = nullptr;
};
