#pragma once

#include "IperfGuiTypes.h"

#include <QWidget>

class IperfCoreBridge;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QLabel;

class QuickTestPage : public QWidget
{
public:
    explicit QuickTestPage(QWidget *parent = nullptr);

    void bindBridge(IperfCoreBridge *bridge);
    void loadConfiguration(const IperfGuiConfig &config);
    IperfGuiConfig configuration() const;
    void appendEvent(const IperfGuiEvent &event);
    void appendSession(const IperfSessionRecord &record);

private:
    void updateSummaryFromFields(const QVariantMap &fields);
    void updateSummaryFromRecord(const IperfSessionRecord &record);
    void appendLogLine(const QString &text);

    IperfCoreBridge *m_bridge = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_protocol = nullptr;
    QComboBox *m_family = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QSpinBox *m_duration = nullptr;
    QSpinBox *m_parallel = nullptr;
    QLineEdit *m_bitrate = nullptr;
    QCheckBox *m_reverse = nullptr;
    QCheckBox *m_bidirectional = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_export = nullptr;
    QLabel *m_stateValue = nullptr;
    QLabel *m_targetValue = nullptr;
    QLabel *m_rateValue = nullptr;
    QLabel *m_bytesValue = nullptr;
    QLabel *m_cpuValue = nullptr;
    QLabel *m_eventsValue = nullptr;
    QTableWidget *m_table = nullptr;
    QPlainTextEdit *m_rawJson = nullptr;
};

class AdvancedClientPage : public QWidget
{
public:
    explicit AdvancedClientPage(QWidget *parent = nullptr);

    void bindBridge(IperfCoreBridge *bridge);
    void loadConfiguration(const IperfGuiConfig &config);
    IperfGuiConfig configuration() const;

private:
    void applyConfiguration();

    IperfCoreBridge *m_bridge = nullptr;
    QComboBox *m_family = nullptr;
    QLineEdit *m_bindAddress = nullptr;
    QLineEdit *m_bindDev = nullptr;
    QSpinBox *m_bindPort = nullptr;
    QSpinBox *m_blockSize = nullptr;
    QSpinBox *m_windowSize = nullptr;
    QSpinBox *m_mss = nullptr;
    QSpinBox *m_reporterInterval = nullptr;
    QSpinBox *m_statsInterval = nullptr;
    QSpinBox *m_pacingTimer = nullptr;
    QSpinBox *m_connectTimeout = nullptr;
    QSpinBox *m_tos = nullptr;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_extraData = nullptr;
    QLineEdit *m_congestionControl = nullptr;
    QLineEdit *m_timestampFormat = nullptr;
    QLineEdit *m_bitrate = nullptr;
    QCheckBox *m_noDelay = nullptr;
    QCheckBox *m_reverse = nullptr;
    QCheckBox *m_bidirectional = nullptr;
    QCheckBox *m_oneOff = nullptr;
    QCheckBox *m_getServerOutput = nullptr;
    QCheckBox *m_jsonStream = nullptr;
    QCheckBox *m_jsonStreamFullOutput = nullptr;
    QCheckBox *m_udpCounters64Bit = nullptr;
    QCheckBox *m_timestamps = nullptr;
    QCheckBox *m_repeatingPayload = nullptr;
    QCheckBox *m_skipRxCopy = nullptr;
    QCheckBox *m_zeroCopy = nullptr;
    QCheckBox *m_dontFragment = nullptr;
    QCheckBox *m_forceFlush = nullptr;
    QCheckBox *m_mptcp = nullptr;
    QLabel *m_status = nullptr;
};

class ServerPage : public QWidget
{
public:
    explicit ServerPage(QWidget *parent = nullptr);

    void bindBridge(IperfCoreBridge *bridge);
    void loadConfiguration(const IperfGuiConfig &config);
    IperfGuiConfig configuration() const;
    void appendEvent(const IperfGuiEvent &event);
    void appendSession(const IperfSessionRecord &record);

private:
    void updateSummaryFromFields(const QVariantMap &fields);
    void updateSummaryFromRecord(const IperfSessionRecord &record);

    IperfCoreBridge *m_bridge = nullptr;
    QComboBox *m_family = nullptr;
    QLineEdit *m_bindAddress = nullptr;
    QSpinBox *m_port = nullptr;
    QCheckBox *m_oneOff = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_stop = nullptr;
    QLabel *m_stateValue = nullptr;
    QLabel *m_portValue = nullptr;
    QLabel *m_clientsValue = nullptr;
    QLabel *m_cpuValue = nullptr;
    QPlainTextEdit *m_log = nullptr;
};

class HistoryPage : public QWidget
{
public:
    explicit HistoryPage(QWidget *parent = nullptr);

    void bindBridge(IperfCoreBridge *bridge);
    void appendSession(const IperfSessionRecord &record);
    void clearHistory();

private:
    void showRecord(int index);
    void exportSelected();
    QString recordTitle(const IperfSessionRecord &record) const;

    IperfCoreBridge *m_bridge = nullptr;
    QListWidget *m_list = nullptr;
    QPlainTextEdit *m_details = nullptr;
    QPushButton *m_export = nullptr;
    QVector<IperfSessionRecord> m_records;
};

class SettingsPage : public QWidget
{
public:
    explicit SettingsPage(QWidget *parent = nullptr);

private:
    QLabel *m_runtimeInfo = nullptr;
    QLabel *m_buildInfo = nullptr;
    QPlainTextEdit *m_featureNotes = nullptr;
};

