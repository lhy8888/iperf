#include "IperfPages.h"

#include "IperfCoreBridge.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QAbstractItemView>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QTime>
#include <QSysInfo>
#include <QStringList>
#include <QVariant>

namespace {

static QVariantMap summaryFieldsFromEvent(const IperfGuiEvent &event)
{
    const QStringList summaryKeys = {
        QStringLiteral("summary"),
        QStringLiteral("sum"),
        QStringLiteral("sum_sent"),
        QStringLiteral("sum_received"),
        QStringLiteral("sum_bidir_reverse"),
        QStringLiteral("sum_sent_bidir_reverse"),
        QStringLiteral("sum_received_bidir_reverse"),
    };

    for (const QString &key : summaryKeys) {
        const QVariant value = event.fields.value(key);
        if (value.isValid() && value.canConvert<QVariantMap>()) {
            return value.toMap();
        }
    }
    return QVariantMap();
}

static QVariantMap cpuFieldsFromEvent(const IperfGuiEvent &event)
{
    const QVariant value = event.fields.value(QStringLiteral("cpu_utilization_percent"));
    if (value.isValid() && value.canConvert<QVariantMap>()) {
        return value.toMap();
    }
    return QVariantMap();
}

static QString kindLabel(const IperfGuiEvent &event)
{
    switch (event.kind) {
    case IperfEventKind::Started:
        return QStringLiteral("Started");
    case IperfEventKind::Interval:
        return QStringLiteral("Interval");
    case IperfEventKind::Summary:
        return QStringLiteral("Summary");
    case IperfEventKind::Error:
        return QStringLiteral("Error");
    case IperfEventKind::Finished:
        return QStringLiteral("Finished");
    case IperfEventKind::Info:
    default:
        return QStringLiteral("Info");
    }
}

static QString eventMessageText(const IperfGuiEvent &event)
{
    if (!event.message.isEmpty()) {
        return event.message;
    }
    if (!event.eventName.isEmpty()) {
        return event.eventName;
    }
    return QStringLiteral("-");
}

static QString sessionTargetText(const IperfSessionRecord &record)
{
    if (record.config.mode == IperfGuiConfig::Mode::Server) {
        return QStringLiteral("Server :%1").arg(record.config.port);
    }
    return QStringLiteral("Client %1:%2")
        .arg(iperfTargetName(record.config))
        .arg(record.config.port);
}

static QString sessionSummaryText(const IperfSessionRecord &record)
{
    const QVariantMap fields = record.finalFields;
    const QVariantMap summary = [&fields]() -> QVariantMap {
        const QStringList keys = {
            QStringLiteral("summary"),
            QStringLiteral("sum"),
            QStringLiteral("sum_sent"),
            QStringLiteral("sum_received"),
            QStringLiteral("sum_bidir_reverse"),
            QStringLiteral("sum_sent_bidir_reverse"),
            QStringLiteral("sum_received_bidir_reverse"),
        };
        for (const QString &key : keys) {
            const QVariant value = fields.value(key);
            if (value.isValid() && value.canConvert<QVariantMap>()) {
                return value.toMap();
            }
        }
        return QVariantMap();
    }();
    if (!summary.isEmpty() && summary.contains(QStringLiteral("bits_per_second"))) {
        return iperfHumanBitsPerSecond(summary.value(QStringLiteral("bits_per_second")).toDouble());
    }
    return record.statusText.isEmpty() ? QStringLiteral("Completed") : record.statusText;
}

static void setSpinRange(QSpinBox *spin, int minimum, int maximum, int step = 1)
{
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
}

static QLineEdit *makeLineEdit(const QString &placeholder = QString())
{
    auto *edit = new QLineEdit;
    if (!placeholder.isEmpty()) {
        edit->setPlaceholderText(placeholder);
    }
    return edit;
}

static QSpinBox *makeSpinBox(int minimum, int maximum, int step = 1)
{
    auto *spin = new QSpinBox;
    setSpinRange(spin, minimum, maximum, step);
    return spin;
}

static QComboBox *makeComboBox(const QStringList &items)
{
    auto *combo = new QComboBox;
    combo->addItems(items);
    return combo;
}

static QString formatSessionRow(const IperfSessionRecord &record)
{
    const QString protocol = iperfProtocolName(record.config.protocol);
    const QString when = record.startedAt.isValid()
        ? record.startedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QStringLiteral("unknown time");
    return QStringLiteral("%1  %2  %3  %4")
        .arg(when, sessionTargetText(record), protocol, sessionSummaryText(record));
}

static QJsonObject eventToJson(const IperfGuiEvent &event)
{
    QJsonObject object;
    object.insert(QStringLiteral("kind"), kindLabel(event));
    object.insert(QStringLiteral("event_name"), event.eventName);
    object.insert(QStringLiteral("message"), event.message);
    object.insert(QStringLiteral("raw_json"), event.rawJson);
    if (event.receivedAt.isValid()) {
        object.insert(QStringLiteral("received_at"), event.receivedAt.toString(Qt::ISODateWithMs));
    }
    if (!event.fields.isEmpty()) {
        object.insert(QStringLiteral("fields"), QJsonObject::fromVariantMap(event.fields));
    }
    return object;
}

static QJsonObject sessionToJson(const IperfSessionRecord &record)
{
    QJsonObject object;
    object.insert(QStringLiteral("started_at"),
                  record.startedAt.isValid() ? record.startedAt.toString(Qt::ISODateWithMs) : QString());
    object.insert(QStringLiteral("mode"), iperfModeName(record.config.mode));
    object.insert(QStringLiteral("protocol"), iperfProtocolName(record.config.protocol));
    object.insert(QStringLiteral("family"), iperfFamilyName(record.config.family));
    object.insert(QStringLiteral("target"), sessionTargetText(record));
    object.insert(QStringLiteral("host"), record.config.host);
    object.insert(QStringLiteral("bind_address"), record.config.bindAddress);
    object.insert(QStringLiteral("bind_dev"), record.config.bindDev);
    object.insert(QStringLiteral("title"), record.config.title);
    object.insert(QStringLiteral("extra_data"), record.config.extraData);
    object.insert(QStringLiteral("congestion_control"), record.config.congestionControl);
    object.insert(QStringLiteral("timestamp_format"), record.config.timestampFormat);
    object.insert(QStringLiteral("port"), record.config.port);
    object.insert(QStringLiteral("bind_port"), record.config.bindPort);
    object.insert(QStringLiteral("duration"), record.config.duration);
    object.insert(QStringLiteral("parallel"), record.config.parallel);
    object.insert(QStringLiteral("block_size"), record.config.blockSize);
    object.insert(QStringLiteral("window_size"), record.config.windowSize);
    object.insert(QStringLiteral("mss"), record.config.mss);
    object.insert(QStringLiteral("reporter_interval_ms"), record.config.reporterIntervalMs);
    object.insert(QStringLiteral("stats_interval_ms"), record.config.statsIntervalMs);
    object.insert(QStringLiteral("pacing_timer_us"), record.config.pacingTimerUs);
    object.insert(QStringLiteral("connect_timeout_ms"), record.config.connectTimeoutMs);
    object.insert(QStringLiteral("tos"), record.config.tos);
    object.insert(QStringLiteral("bitrate_bps"), static_cast<double>(record.config.bitrateBps));
    object.insert(QStringLiteral("reverse"), record.config.reverse);
    object.insert(QStringLiteral("bidirectional"), record.config.bidirectional);
    object.insert(QStringLiteral("one_off"), record.config.oneOff);
    object.insert(QStringLiteral("no_delay"), record.config.noDelay);
    object.insert(QStringLiteral("get_server_output"), record.config.getServerOutput);
    object.insert(QStringLiteral("json_stream"), record.config.jsonStream);
    object.insert(QStringLiteral("json_stream_full_output"), record.config.jsonStreamFullOutput);
    object.insert(QStringLiteral("udp_counters_64bit"), record.config.udpCounters64Bit);
    object.insert(QStringLiteral("zero_copy"), record.config.zeroCopy);
    object.insert(QStringLiteral("timestamps"), record.config.timestamps);
    object.insert(QStringLiteral("repeating_payload"), record.config.repeatingPayload);
    object.insert(QStringLiteral("skip_rx_copy"), record.config.skipRxCopy);
    object.insert(QStringLiteral("mptcp"), record.config.mptcp);
    object.insert(QStringLiteral("dont_fragment"), record.config.dontFragment);
    object.insert(QStringLiteral("force_flush"), record.config.forceFlush);
    object.insert(QStringLiteral("exit_code"), record.exitCode);
    object.insert(QStringLiteral("status_text"), record.statusText);
    object.insert(QStringLiteral("raw_json"), record.rawJson);
    if (!record.finalFields.isEmpty()) {
        object.insert(QStringLiteral("final_fields"), QJsonObject::fromVariantMap(record.finalFields));
    }
    if (!record.events.isEmpty()) {
        QJsonArray events;
        for (const IperfGuiEvent &event : record.events) {
            events.append(eventToJson(event));
        }
        object.insert(QStringLiteral("events"), events);
    }
    return object;
}

static QJsonDocument historyDocument(const QVector<IperfSessionRecord> &records)
{
    QJsonArray sessions;
    for (const IperfSessionRecord &record : records) {
        sessions.append(sessionToJson(record));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("exported_at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("session_count"), records.size());
    root.insert(QStringLiteral("sessions"), sessions);
    return QJsonDocument(root);
}

static bool writeTextFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    if (file.write(data) != data.size()) {
        return false;
    }
    return file.commit();
}

static bool writeJsonFile(const QString &path, const QJsonDocument &document)
{
    return writeTextFile(path, document.toJson(QJsonDocument::Indented));
}

} // namespace

QuickTestPage::QuickTestPage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setSpacing(14);

    auto *topRow = new QHBoxLayout;

    auto *sessionBox = new QGroupBox(tr("Session"), this);
    auto *sessionForm = new QFormLayout(sessionBox);
    const IperfGuiConfig defaults;
    m_mode = new QComboBox(sessionBox);
    m_mode->addItems({tr("Client"), tr("Server")});
    sessionForm->addRow(tr("Mode"), m_mode);

    m_protocol = new QComboBox(sessionBox);
    m_protocol->addItems({tr("TCP"), tr("UDP")});
    sessionForm->addRow(tr("Protocol"), m_protocol);

    m_family = new QComboBox(sessionBox);
    m_family->addItems({tr("Any"), tr("IPv4"), tr("IPv6")});
    sessionForm->addRow(tr("Family"), m_family);

    m_host = makeLineEdit(tr("server host or IP"));
    sessionForm->addRow(tr("Host"), m_host);

    m_port = makeSpinBox(1, 65535);
    m_port->setValue(defaults.port);
    sessionForm->addRow(tr("Port"), m_port);

    m_duration = makeSpinBox(0, 86400);
    m_duration->setValue(defaults.duration);
    sessionForm->addRow(tr("Duration (s)"), m_duration);

    m_parallel = makeSpinBox(1, 128);
    m_parallel->setValue(1);
    sessionForm->addRow(tr("Parallel"), m_parallel);

    m_bitrate = makeLineEdit(tr("bps, leave blank for default"));
    sessionForm->addRow(tr("Bitrate"), m_bitrate);

    m_reverse = new QCheckBox(tr("Reverse"), sessionBox);
    m_bidirectional = new QCheckBox(tr("Bidirectional"), sessionBox);
    sessionForm->addRow(m_reverse);
    sessionForm->addRow(m_bidirectional);

    auto *buttonsRow = new QHBoxLayout;
    m_start = new QPushButton(tr("Start"), sessionBox);
    m_stop = new QPushButton(tr("Stop"), sessionBox);
    m_export = new QPushButton(tr("Export JSON"), sessionBox);
    buttonsRow->addWidget(m_start);
    buttonsRow->addWidget(m_stop);
    buttonsRow->addWidget(m_export);
    buttonsRow->addStretch(1);
    sessionForm->addRow(buttonsRow);

    auto *summaryBox = new QGroupBox(tr("Summary"), this);
    auto *summaryGrid = new QGridLayout(summaryBox);
    summaryGrid->addWidget(new QLabel(tr("State")), 0, 0);
    summaryGrid->addWidget(new QLabel(tr("Target")), 1, 0);
    summaryGrid->addWidget(new QLabel(tr("Throughput")), 0, 2);
    summaryGrid->addWidget(new QLabel(tr("Bytes")), 1, 2);
    summaryGrid->addWidget(new QLabel(tr("CPU")), 2, 0);
    summaryGrid->addWidget(new QLabel(tr("Events")), 2, 2);
    m_stateValue = new QLabel(tr("Idle"), summaryBox);
    m_targetValue = new QLabel(tr("-"), summaryBox);
    m_rateValue = new QLabel(tr("-"), summaryBox);
    m_bytesValue = new QLabel(tr("-"), summaryBox);
    m_cpuValue = new QLabel(tr("-"), summaryBox);
    m_eventsValue = new QLabel(tr("0"), summaryBox);
    summaryGrid->addWidget(m_stateValue, 0, 1);
    summaryGrid->addWidget(m_targetValue, 1, 1);
    summaryGrid->addWidget(m_rateValue, 0, 3);
    summaryGrid->addWidget(m_bytesValue, 1, 3);
    summaryGrid->addWidget(m_cpuValue, 2, 1);
    summaryGrid->addWidget(m_eventsValue, 2, 3);

    topRow->addWidget(sessionBox, 2);
    topRow->addWidget(summaryBox, 1);
    outer->addLayout(topRow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({tr("Time"), tr("Kind"), tr("Event"), tr("Message")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    outer->addWidget(m_table, 2);

    m_rawJson = new QPlainTextEdit(this);
    m_rawJson->setReadOnly(true);
    m_rawJson->setPlaceholderText(tr("Latest JSON payload"));
    m_rawJson->setMinimumHeight(180);
    outer->addWidget(m_rawJson, 1);

    connect(m_start, &QPushButton::clicked, this, [this]() {
        if (m_bridge == nullptr) {
            return;
        }
        m_bridge->setConfiguration(configuration());
        m_bridge->start();
    });
    connect(m_stop, &QPushButton::clicked, this, [this]() {
        if (m_bridge != nullptr) {
            m_bridge->stop();
        }
    });
    connect(m_export, &QPushButton::clicked, this, [this]() {
        if (m_rawJson->toPlainText().isEmpty()) {
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, tr("Export JSON"), QString(), tr("JSON Files (*.json);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << m_rawJson->toPlainText();
        }
    });

    loadConfiguration(IperfGuiConfig());
}

void
QuickTestPage::bindBridge(IperfCoreBridge *bridge)
{
    if (m_bridge == bridge) {
        return;
    }
    m_bridge = bridge;
    if (m_bridge == nullptr) {
        return;
    }

    connect(m_bridge, &IperfCoreBridge::configurationChanged, this, &QuickTestPage::loadConfiguration);
    connect(m_bridge, &IperfCoreBridge::eventReceived, this, &QuickTestPage::appendEvent);
    connect(m_bridge, &IperfCoreBridge::sessionCompleted, this, &QuickTestPage::appendSession);
    connect(m_bridge, &IperfCoreBridge::runningChanged, this, [this](bool running) {
        m_start->setEnabled(!running);
        m_stop->setEnabled(running);
    });
    connect(m_bridge, &IperfCoreBridge::statusMessageChanged, this, [this](const QString &message) {
        m_stateValue->setText(message);
    });
    connect(m_bridge, &IperfCoreBridge::stateChanged, this, [this](const QString &state) {
        m_stateValue->setText(state);
    });
    loadConfiguration(m_bridge->configuration());
}

void
QuickTestPage::loadConfiguration(const IperfGuiConfig &config)
{
    m_mode->setCurrentIndex(config.mode == IperfGuiConfig::Mode::Server ? 1 : 0);
    m_protocol->setCurrentIndex(config.protocol == IperfGuiConfig::Protocol::Udp ? 1 : 0);
    switch (config.family) {
    case IperfGuiConfig::AddressFamily::IPv4:
        m_family->setCurrentIndex(1);
        break;
    case IperfGuiConfig::AddressFamily::IPv6:
        m_family->setCurrentIndex(2);
        break;
    default:
        m_family->setCurrentIndex(0);
        break;
    }
    m_host->setText(config.host);
    m_port->setValue(config.port);
    m_duration->setValue(config.duration);
    m_parallel->setValue(config.parallel);
    m_bitrate->setText(config.bitrateBps > 0 ? QString::number(config.bitrateBps) : QString());
    m_reverse->setChecked(config.reverse);
    m_bidirectional->setChecked(config.bidirectional);
    if (config.mode == IperfGuiConfig::Mode::Server) {
        m_targetValue->setText(QStringLiteral("Server :%1").arg(config.port));
    } else {
        m_targetValue->setText(QStringLiteral("Client %1:%2").arg(iperfTargetName(config)).arg(config.port));
    }
}

IperfGuiConfig
QuickTestPage::configuration() const
{
    IperfGuiConfig config = m_bridge != nullptr ? m_bridge->configuration() : IperfGuiConfig();

    config.mode = m_mode->currentIndex() == 1 ? IperfGuiConfig::Mode::Server : IperfGuiConfig::Mode::Client;
    config.protocol = m_protocol->currentIndex() == 1 ? IperfGuiConfig::Protocol::Udp : IperfGuiConfig::Protocol::Tcp;
    switch (m_family->currentIndex()) {
    case 1:
        config.family = IperfGuiConfig::AddressFamily::IPv4;
        break;
    case 2:
        config.family = IperfGuiConfig::AddressFamily::IPv6;
        break;
    default:
        config.family = IperfGuiConfig::AddressFamily::Any;
        break;
    }
    config.host = m_host->text().trimmed();
    config.port = m_port->value();
    config.duration = m_duration->value();
    config.parallel = m_parallel->value();
    config.reverse = m_reverse->isChecked();
    config.bidirectional = m_bidirectional->isChecked();

    const QString bitrateText = m_bitrate->text().trimmed();
    bool ok = false;
    const quint64 bitrate = bitrateText.isEmpty() ? 0 : bitrateText.toULongLong(&ok);
    config.bitrateBps = ok ? bitrate : 0;

    return config;
}

void
QuickTestPage::appendLogLine(const QString &text)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    const QStringList pieces = text.split(QLatin1Char('\t'));
    for (int column = 0; column < 4; ++column) {
        const QString value = column < pieces.size() ? pieces.at(column) : QString();
        m_table->setItem(row, column, new QTableWidgetItem(value));
    }
    m_table->scrollToBottom();
}

void
QuickTestPage::appendEvent(const IperfGuiEvent &event)
{
    const QString timestamp = event.receivedAt.isValid()
        ? event.receivedAt.toString(QStringLiteral("HH:mm:ss"))
        : QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    appendLogLine(QStringLiteral("%1\t%2\t%3\t%4")
                  .arg(timestamp, kindLabel(event), event.eventName, eventMessageText(event)));
    m_eventsValue->setText(QString::number(m_table->rowCount()));
    m_rawJson->setPlainText(event.rawJson);
    updateSummaryFromFields(event.fields);
}

void
QuickTestPage::appendSession(const IperfSessionRecord &record)
{
    updateSummaryFromRecord(record);
    if (!record.rawJson.isEmpty()) {
        m_rawJson->setPlainText(record.rawJson);
    }
    m_stateValue->setText(record.statusText.isEmpty() ? tr("Completed") : record.statusText);
}

void
QuickTestPage::updateSummaryFromFields(const QVariantMap &fields)
{
    const QVariantMap summary = [&fields]() -> QVariantMap {
        const QStringList keys = {
            QStringLiteral("summary"),
            QStringLiteral("sum"),
            QStringLiteral("sum_sent"),
            QStringLiteral("sum_received"),
            QStringLiteral("sum_bidir_reverse"),
            QStringLiteral("sum_sent_bidir_reverse"),
            QStringLiteral("sum_received_bidir_reverse"),
        };
        for (const QString &key : keys) {
            const QVariant value = fields.value(key);
            if (value.isValid() && value.canConvert<QVariantMap>()) {
                return value.toMap();
            }
        }
        return QVariantMap();
    }();

    if (!summary.isEmpty()) {
        if (summary.contains(QStringLiteral("bits_per_second"))) {
            m_rateValue->setText(iperfHumanBitsPerSecond(summary.value(QStringLiteral("bits_per_second")).toDouble()));
        }
        if (summary.contains(QStringLiteral("bytes"))) {
            m_bytesValue->setText(iperfHumanBytes(summary.value(QStringLiteral("bytes")).toLongLong()));
        }
    }

    const QVariantMap cpu = fields.value(QStringLiteral("cpu_utilization_percent")).toMap();
    if (cpu.contains(QStringLiteral("host_total"))) {
        m_cpuValue->setText(iperfHumanPercent(cpu.value(QStringLiteral("host_total")).toDouble()));
    }
}

void
QuickTestPage::updateSummaryFromRecord(const IperfSessionRecord &record)
{
    const QVariantMap fields = record.finalFields;
    if (fields.contains(QStringLiteral("cpu_utilization_percent"))) {
        const QVariantMap cpu = fields.value(QStringLiteral("cpu_utilization_percent")).toMap();
        if (cpu.contains(QStringLiteral("host_total"))) {
            m_cpuValue->setText(iperfHumanPercent(cpu.value(QStringLiteral("host_total")).toDouble()));
        }
    }
    updateSummaryFromFields(fields);
}

AdvancedClientPage::AdvancedClientPage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setSpacing(12);

    auto *title = new QLabel(tr("Advanced client settings"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600;"));
    outer->addWidget(title);

    m_status = new QLabel(tr("No bridge connected"), this);
    outer->addWidget(m_status);

    auto *formBox = new QGroupBox(tr("Core options"), this);
    auto *form = new QFormLayout(formBox);

    m_family = new QComboBox(formBox);
    m_family->addItems({tr("Any"), tr("IPv4"), tr("IPv6")});
    form->addRow(tr("Family"), m_family);

    m_bindAddress = makeLineEdit(tr("bind address"));
    form->addRow(tr("Bind address"), m_bindAddress);

    m_bindDev = makeLineEdit(tr("bind device"));
    form->addRow(tr("Bind device"), m_bindDev);

    m_bindPort = makeSpinBox(0, 65535);
    form->addRow(tr("Bind port"), m_bindPort);

    m_blockSize = makeSpinBox(0, 16 * 1024 * 1024);
    form->addRow(tr("Block size"), m_blockSize);

    m_windowSize = makeSpinBox(0, 16 * 1024 * 1024);
    form->addRow(tr("Window size"), m_windowSize);

    m_mss = makeSpinBox(0, 65535);
    form->addRow(tr("MSS"), m_mss);

    m_reporterInterval = makeSpinBox(0, 60000);
    form->addRow(tr("Reporter interval (ms)"), m_reporterInterval);

    m_statsInterval = makeSpinBox(0, 60000);
    form->addRow(tr("Stats interval (ms)"), m_statsInterval);

    m_pacingTimer = makeSpinBox(0, 1000000);
    form->addRow(tr("Pacing timer (us)"), m_pacingTimer);

    m_connectTimeout = makeSpinBox(-1, 600000);
    form->addRow(tr("Connect timeout (ms)"), m_connectTimeout);

    m_tos = makeSpinBox(0, 255);
    form->addRow(tr("TOS"), m_tos);

    m_title = makeLineEdit(tr("session title"));
    form->addRow(tr("Title"), m_title);

    m_extraData = makeLineEdit(tr("extra data"));
    form->addRow(tr("Extra data"), m_extraData);

    m_congestionControl = makeLineEdit(tr("congestion algorithm"));
    form->addRow(tr("Congestion control"), m_congestionControl);

    m_timestampFormat = makeLineEdit(tr("timestamp format"));
    form->addRow(tr("Timestamp format"), m_timestampFormat);

    m_bitrate = makeLineEdit(tr("bps"));
    form->addRow(tr("Bitrate"), m_bitrate);

    outer->addWidget(formBox);

    auto *flagsBox = new QGroupBox(tr("Flags"), this);
    auto *flagsLayout = new QGridLayout(flagsBox);
    m_noDelay = new QCheckBox(tr("No delay"), flagsBox);
    m_reverse = new QCheckBox(tr("Reverse"), flagsBox);
    m_bidirectional = new QCheckBox(tr("Bidirectional"), flagsBox);
    m_oneOff = new QCheckBox(tr("One off"), flagsBox);
    m_getServerOutput = new QCheckBox(tr("Get server output"), flagsBox);
    m_jsonStream = new QCheckBox(tr("JSON stream"), flagsBox);
    m_jsonStreamFullOutput = new QCheckBox(tr("JSON stream full output"), flagsBox);
    m_udpCounters64Bit = new QCheckBox(tr("64-bit UDP counters"), flagsBox);
    m_timestamps = new QCheckBox(tr("Timestamps"), flagsBox);
    m_repeatingPayload = new QCheckBox(tr("Repeating payload"), flagsBox);
    m_skipRxCopy = new QCheckBox(tr("Skip RX copy"), flagsBox);
    m_zeroCopy = new QCheckBox(tr("Zero copy"), flagsBox);
    m_dontFragment = new QCheckBox(tr("Don't fragment"), flagsBox);
    m_forceFlush = new QCheckBox(tr("Force flush"), flagsBox);
    m_mptcp = new QCheckBox(tr("MPTCP"), flagsBox);
    const QList<QCheckBox *> checks = {
        m_noDelay, m_reverse, m_bidirectional, m_oneOff,
        m_getServerOutput, m_jsonStream, m_jsonStreamFullOutput,
        m_udpCounters64Bit, m_timestamps, m_repeatingPayload,
        m_skipRxCopy, m_zeroCopy, m_dontFragment, m_forceFlush, m_mptcp,
    };
    int row = 0;
    int column = 0;
    for (QCheckBox *check : checks) {
        flagsLayout->addWidget(check, row, column);
        ++column;
        if (column == 2) {
            column = 0;
            ++row;
        }
    }
#if !defined(HAVE_MSG_TRUNC) || !HAVE_MSG_TRUNC
    m_skipRxCopy->setEnabled(false);
    m_skipRxCopy->setToolTip(tr("MSG_TRUNC is not available on this build"));
#endif
#if !defined(HAVE_IPPROTO_MPTCP) || !HAVE_IPPROTO_MPTCP
    m_mptcp->setEnabled(false);
    m_mptcp->setToolTip(tr("MPTCP is not available on this build"));
#endif
#if !defined(HAVE_SSL) || !HAVE_SSL
    flagsLayout->addWidget(new QLabel(tr("Authentication options are not available in this build."), flagsBox), row + 1, 0, 1, 2);
#endif
    outer->addWidget(flagsBox);

    auto *actionRow = new QHBoxLayout;
    auto *applyButton = new QPushButton(tr("Apply"), this);
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    actionRow->addWidget(applyButton);
    actionRow->addWidget(refreshButton);
    actionRow->addStretch(1);
    outer->addLayout(actionRow);
    outer->addStretch(1);

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        applyConfiguration();
    });
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        if (m_bridge != nullptr) {
            loadConfiguration(m_bridge->configuration());
        }
    });

    loadConfiguration(IperfGuiConfig());
}

void
AdvancedClientPage::bindBridge(IperfCoreBridge *bridge)
{
    m_bridge = bridge;
    if (m_bridge == nullptr) {
        return;
    }

    connect(m_bridge, &IperfCoreBridge::configurationChanged, this, &AdvancedClientPage::loadConfiguration);
    connect(m_bridge, &IperfCoreBridge::stateChanged, this, [this](const QString &state) {
        m_status->setText(state);
    });
    connect(m_bridge, &IperfCoreBridge::runningChanged, this, [this](bool running) {
        m_status->setText(running ? tr("Running") : tr("Idle"));
    });
    loadConfiguration(m_bridge->configuration());
}

void
AdvancedClientPage::loadConfiguration(const IperfGuiConfig &config)
{
    switch (config.family) {
    case IperfGuiConfig::AddressFamily::IPv4:
        m_family->setCurrentIndex(1);
        break;
    case IperfGuiConfig::AddressFamily::IPv6:
        m_family->setCurrentIndex(2);
        break;
    default:
        m_family->setCurrentIndex(0);
        break;
    }
    m_bindAddress->setText(config.bindAddress);
    m_bindDev->setText(config.bindDev);
    m_bindPort->setValue(config.bindPort);
    m_blockSize->setValue(config.blockSize);
    m_windowSize->setValue(config.windowSize);
    m_mss->setValue(config.mss);
    m_reporterInterval->setValue(config.reporterIntervalMs);
    m_statsInterval->setValue(config.statsIntervalMs);
    m_pacingTimer->setValue(config.pacingTimerUs);
    m_connectTimeout->setValue(config.connectTimeoutMs);
    m_tos->setValue(config.tos);
    m_title->setText(config.title);
    m_extraData->setText(config.extraData);
    m_congestionControl->setText(config.congestionControl);
    m_timestampFormat->setText(config.timestampFormat);
    m_bitrate->setText(config.bitrateBps > 0 ? QString::number(config.bitrateBps) : QString());
    m_noDelay->setChecked(config.noDelay);
    m_reverse->setChecked(config.reverse);
    m_bidirectional->setChecked(config.bidirectional);
    m_oneOff->setChecked(config.oneOff);
    m_getServerOutput->setChecked(config.getServerOutput);
    m_jsonStream->setChecked(config.jsonStream);
    m_jsonStreamFullOutput->setChecked(config.jsonStreamFullOutput);
    m_udpCounters64Bit->setChecked(config.udpCounters64Bit);
    m_timestamps->setChecked(config.timestamps);
    m_repeatingPayload->setChecked(config.repeatingPayload);
    m_skipRxCopy->setChecked(config.skipRxCopy);
    m_zeroCopy->setChecked(config.zeroCopy);
    m_dontFragment->setChecked(config.dontFragment);
    m_forceFlush->setChecked(config.forceFlush);
    m_mptcp->setChecked(config.mptcp);
}

IperfGuiConfig
AdvancedClientPage::configuration() const
{
    IperfGuiConfig config = m_bridge != nullptr ? m_bridge->configuration() : IperfGuiConfig();

    switch (m_family->currentIndex()) {
    case 1:
        config.family = IperfGuiConfig::AddressFamily::IPv4;
        break;
    case 2:
        config.family = IperfGuiConfig::AddressFamily::IPv6;
        break;
    default:
        config.family = IperfGuiConfig::AddressFamily::Any;
        break;
    }
    config.bindAddress = m_bindAddress->text().trimmed();
    config.bindDev = m_bindDev->text().trimmed();
    config.bindPort = m_bindPort->value();
    config.blockSize = m_blockSize->value();
    config.windowSize = m_windowSize->value();
    config.mss = m_mss->value();
    config.reporterIntervalMs = m_reporterInterval->value();
    config.statsIntervalMs = m_statsInterval->value();
    config.pacingTimerUs = m_pacingTimer->value();
    config.connectTimeoutMs = m_connectTimeout->value();
    config.tos = m_tos->value();
    config.title = m_title->text().trimmed();
    config.extraData = m_extraData->text().trimmed();
    config.congestionControl = m_congestionControl->text().trimmed();
    config.timestampFormat = m_timestampFormat->text();
    config.noDelay = m_noDelay->isChecked();
    config.reverse = m_reverse->isChecked();
    config.bidirectional = m_bidirectional->isChecked();
    config.oneOff = m_oneOff->isChecked();
    config.getServerOutput = m_getServerOutput->isChecked();
    config.jsonStream = m_jsonStream->isChecked();
    config.jsonStreamFullOutput = m_jsonStreamFullOutput->isChecked();
    config.udpCounters64Bit = m_udpCounters64Bit->isChecked();
    config.timestamps = m_timestamps->isChecked();
    config.repeatingPayload = m_repeatingPayload->isChecked();
    config.skipRxCopy = m_skipRxCopy->isChecked();
    config.zeroCopy = m_zeroCopy->isChecked();
    config.dontFragment = m_dontFragment->isChecked();
    config.forceFlush = m_forceFlush->isChecked();
    config.mptcp = m_mptcp->isChecked();

    const QString bitrateText = m_bitrate->text().trimmed();
    bool ok = false;
    const quint64 bitrate = bitrateText.isEmpty() ? 0 : bitrateText.toULongLong(&ok);
    config.bitrateBps = ok ? bitrate : 0;

    return config;
}

void
AdvancedClientPage::applyConfiguration()
{
    if (m_bridge == nullptr) {
        return;
    }
    m_bridge->setConfiguration(configuration());
    m_status->setText(tr("Applied"));
}

ServerPage::ServerPage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setSpacing(12);

    auto *title = new QLabel(tr("Server mode"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600;"));
    outer->addWidget(title);

    auto *formBox = new QGroupBox(tr("Server settings"), this);
    auto *form = new QFormLayout(formBox);

    m_family = new QComboBox(formBox);
    m_family->addItems({tr("Any"), tr("IPv4"), tr("IPv6")});
    form->addRow(tr("Family"), m_family);

    m_bindAddress = makeLineEdit(tr("0.0.0.0 or ::"));
    form->addRow(tr("Bind address"), m_bindAddress);

    m_port = makeSpinBox(1, 65535);
    m_port->setValue(IperfGuiConfig().port);
    form->addRow(tr("Port"), m_port);

    m_oneOff = new QCheckBox(tr("One off"), formBox);
    form->addRow(m_oneOff);

    auto *actionRow = new QHBoxLayout;
    m_start = new QPushButton(tr("Start server"), formBox);
    m_stop = new QPushButton(tr("Stop"), formBox);
    actionRow->addWidget(m_start);
    actionRow->addWidget(m_stop);
    actionRow->addStretch(1);
    form->addRow(actionRow);

    outer->addWidget(formBox);

    auto *summaryBox = new QGroupBox(tr("Server summary"), this);
    auto *summaryGrid = new QGridLayout(summaryBox);
    summaryGrid->addWidget(new QLabel(tr("State")), 0, 0);
    summaryGrid->addWidget(new QLabel(tr("Port")), 0, 2);
    summaryGrid->addWidget(new QLabel(tr("Clients")), 1, 0);
    summaryGrid->addWidget(new QLabel(tr("CPU")), 1, 2);
    m_stateValue = new QLabel(tr("Idle"), summaryBox);
    m_portValue = new QLabel(tr("-"), summaryBox);
    m_clientsValue = new QLabel(tr("0"), summaryBox);
    m_cpuValue = new QLabel(tr("-"), summaryBox);
    summaryGrid->addWidget(m_stateValue, 0, 1);
    summaryGrid->addWidget(m_portValue, 0, 3);
    summaryGrid->addWidget(m_clientsValue, 1, 1);
    summaryGrid->addWidget(m_cpuValue, 1, 3);
    outer->addWidget(summaryBox);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(tr("Server events and output"));
    outer->addWidget(m_log, 1);

    connect(m_start, &QPushButton::clicked, this, [this]() {
        if (m_bridge == nullptr) {
            return;
        }
        m_bridge->setConfiguration(configuration());
        m_bridge->start();
    });
    connect(m_stop, &QPushButton::clicked, this, [this]() {
        if (m_bridge != nullptr) {
            m_bridge->stop();
        }
    });

    loadConfiguration(IperfGuiConfig());
}

void
ServerPage::bindBridge(IperfCoreBridge *bridge)
{
    m_bridge = bridge;
    if (m_bridge == nullptr) {
        return;
    }

    connect(m_bridge, &IperfCoreBridge::configurationChanged, this, &ServerPage::loadConfiguration);
    connect(m_bridge, &IperfCoreBridge::eventReceived, this, &ServerPage::appendEvent);
    connect(m_bridge, &IperfCoreBridge::sessionCompleted, this, &ServerPage::appendSession);
    connect(m_bridge, &IperfCoreBridge::runningChanged, this, [this](bool running) {
        m_start->setEnabled(!running);
        m_stop->setEnabled(running);
        m_stateValue->setText(running ? tr("Running") : tr("Idle"));
    });
    connect(m_bridge, &IperfCoreBridge::statusMessageChanged, this, [this](const QString &message) {
        m_stateValue->setText(message);
    });
    loadConfiguration(m_bridge->configuration());
}

void
ServerPage::loadConfiguration(const IperfGuiConfig &config)
{
    switch (config.family) {
    case IperfGuiConfig::AddressFamily::IPv4:
        m_family->setCurrentIndex(1);
        break;
    case IperfGuiConfig::AddressFamily::IPv6:
        m_family->setCurrentIndex(2);
        break;
    default:
        m_family->setCurrentIndex(0);
        break;
    }
    m_bindAddress->setText(config.bindAddress);
    m_port->setValue(config.port);
    m_oneOff->setChecked(config.oneOff);
    m_portValue->setText(QString::number(config.port));
}

IperfGuiConfig
ServerPage::configuration() const
{
    IperfGuiConfig config = m_bridge != nullptr ? m_bridge->configuration() : IperfGuiConfig();
    config.mode = IperfGuiConfig::Mode::Server;
    config.protocol = m_bridge != nullptr ? m_bridge->configuration().protocol : IperfGuiConfig::Protocol::Tcp;
    switch (m_family->currentIndex()) {
    case 1:
        config.family = IperfGuiConfig::AddressFamily::IPv4;
        break;
    case 2:
        config.family = IperfGuiConfig::AddressFamily::IPv6;
        break;
    default:
        config.family = IperfGuiConfig::AddressFamily::Any;
        break;
    }
    config.bindAddress = m_bindAddress->text().trimmed();
    config.port = m_port->value();
    config.oneOff = m_oneOff->isChecked();
    return config;
}

void
ServerPage::appendEvent(const IperfGuiEvent &event)
{
    const QString line = QStringLiteral("[%1] %2: %3")
        .arg(event.receivedAt.isValid() ? event.receivedAt.toString(QStringLiteral("HH:mm:ss")) : QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
             kindLabel(event),
             eventMessageText(event));
    m_log->appendPlainText(line);
    updateSummaryFromFields(event.fields);
}

void
ServerPage::appendSession(const IperfSessionRecord &record)
{
    updateSummaryFromRecord(record);
    if (!record.rawJson.isEmpty()) {
        m_log->appendPlainText(tr("Session JSON captured (%1 bytes)").arg(record.rawJson.size()));
    }
    m_stateValue->setText(record.statusText.isEmpty() ? tr("Completed") : record.statusText);
}

void
ServerPage::updateSummaryFromFields(const QVariantMap &fields)
{
    if (fields.contains(QStringLiteral("connected"))) {
        m_clientsValue->setText(QString::number(fields.value(QStringLiteral("connected")).toList().size()));
    }
    const QVariantMap cpu = fields.value(QStringLiteral("cpu_utilization_percent")).toMap();
    if (cpu.contains(QStringLiteral("host_total"))) {
        m_cpuValue->setText(iperfHumanPercent(cpu.value(QStringLiteral("host_total")).toDouble()));
    }
}

void
ServerPage::updateSummaryFromRecord(const IperfSessionRecord &record)
{
    if (!record.finalFields.isEmpty()) {
        updateSummaryFromFields(record.finalFields);
    }
}

HistoryPage::HistoryPage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QHBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->setMinimumWidth(320);
    outer->addWidget(m_list, 1);

    auto *right = new QVBoxLayout;
    m_details = new QPlainTextEdit(this);
    m_details->setReadOnly(true);
    right->addWidget(m_details, 1);

    auto *buttons = new QHBoxLayout;
    m_export = new QPushButton(tr("Export selected JSON"), this);
    m_exportAll = new QPushButton(tr("Export all JSON"), this);
    m_clear = new QPushButton(tr("Clear history"), this);
    buttons->addWidget(m_export);
    buttons->addWidget(m_exportAll);
    buttons->addWidget(m_clear);
    buttons->addStretch(1);
    right->addLayout(buttons);

    outer->addLayout(right, 2);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        showRecord(row);
    });
    connect(m_export, &QPushButton::clicked, this, [this]() {
        exportSelected();
    });
    connect(m_exportAll, &QPushButton::clicked, this, [this]() {
        exportAll();
    });
    connect(m_clear, &QPushButton::clicked, this, [this]() {
        clearHistory();
    });
}

void
HistoryPage::bindBridge(IperfCoreBridge *bridge)
{
    m_bridge = bridge;
    if (m_bridge == nullptr) {
        return;
    }

    connect(m_bridge, &IperfCoreBridge::sessionCompleted, this, &HistoryPage::appendSession);
    m_records = m_bridge->history();
    m_list->clear();
    for (const IperfSessionRecord &record : m_records) {
        m_list->addItem(recordTitle(record));
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(m_list->count() - 1);
    }
}

void
HistoryPage::appendSession(const IperfSessionRecord &record)
{
    m_records.push_back(record);
    m_list->addItem(recordTitle(record));
    m_list->setCurrentRow(m_list->count() - 1);
}

void
HistoryPage::clearHistory()
{
    if (m_bridge != nullptr) {
        m_bridge->clearHistory();
    }
    m_records.clear();
    m_list->clear();
    m_details->clear();
}

void
HistoryPage::showRecord(int index)
{
    if (index < 0 || index >= m_records.size()) {
        m_details->clear();
        return;
    }

    const IperfSessionRecord &record = m_records.at(index);
    QString text;
    QTextStream stream(&text);
    stream << "Started: " << record.startedAt.toString(Qt::ISODate) << '\n';
    stream << "Mode: " << iperfModeName(record.config.mode) << '\n';
    stream << "Protocol: " << iperfProtocolName(record.config.protocol) << '\n';
    stream << "Target: " << sessionTargetText(record) << '\n';
    stream << "Exit code: " << record.exitCode << '\n';
    stream << "Status: " << record.statusText << '\n';
    stream << "Events: " << record.events.size() << '\n';
    stream << '\n';
    stream << record.rawJson;
    m_details->setPlainText(text);
}

void
HistoryPage::exportSelected()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_records.size()) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export session JSON"), QString(), tr("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    const IperfSessionRecord &record = m_records.at(row);
    const QByteArray payload = record.rawJson.isEmpty()
        ? QJsonDocument(sessionToJson(record)).toJson(QJsonDocument::Indented)
        : record.rawJson.toUtf8();

    if (!writeTextFile(path, payload)) {
        return;
    }
}

void
HistoryPage::exportAll()
{
    if (m_records.isEmpty()) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export history JSON"), QString(), tr("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    if (!writeJsonFile(path, historyDocument(m_records))) {
        return;
    }
}

QString
HistoryPage::recordTitle(const IperfSessionRecord &record) const
{
    return formatSessionRow(record);
}

namespace {

static int modeToIndex(IperfGuiConfig::Mode mode)
{
    return mode == IperfGuiConfig::Mode::Server ? 1 : 0;
}

static IperfGuiConfig::Mode modeFromIndex(int index)
{
    return index == 1 ? IperfGuiConfig::Mode::Server : IperfGuiConfig::Mode::Client;
}

static int protocolToIndex(IperfGuiConfig::Protocol protocol)
{
    return protocol == IperfGuiConfig::Protocol::Udp ? 1 : 0;
}

static IperfGuiConfig::Protocol protocolFromIndex(int index)
{
    return index == 1 ? IperfGuiConfig::Protocol::Udp : IperfGuiConfig::Protocol::Tcp;
}

static int familyToIndex(IperfGuiConfig::AddressFamily family)
{
    switch (family) {
    case IperfGuiConfig::AddressFamily::IPv4:
        return 1;
    case IperfGuiConfig::AddressFamily::IPv6:
        return 2;
    case IperfGuiConfig::AddressFamily::Any:
    default:
        return 0;
    }
}

static IperfGuiConfig::AddressFamily familyFromIndex(int index)
{
    switch (index) {
    case 1:
        return IperfGuiConfig::AddressFamily::IPv4;
    case 2:
        return IperfGuiConfig::AddressFamily::IPv6;
    case 0:
    default:
        return IperfGuiConfig::AddressFamily::Any;
    }
}

} // namespace

void
SettingsPage::bindBridge(IperfCoreBridge *bridge)
{
    m_bridge = bridge;
}

void
SettingsPage::loadSettings()
{
    const IperfGuiConfig defaults;
    QSettings settings;

    settings.beginGroup(QStringLiteral("settings"));
    settings.beginGroup(QStringLiteral("defaults"));

    m_mode->setCurrentIndex(modeToIndex(static_cast<IperfGuiConfig::Mode>(
        settings.value(QStringLiteral("mode"), modeToIndex(defaults.mode)).toInt())));
    m_protocol->setCurrentIndex(protocolToIndex(static_cast<IperfGuiConfig::Protocol>(
        settings.value(QStringLiteral("protocol"), protocolToIndex(defaults.protocol)).toInt())));
    m_family->setCurrentIndex(familyToIndex(static_cast<IperfGuiConfig::AddressFamily>(
        settings.value(QStringLiteral("family"), familyToIndex(defaults.family)).toInt())));
    m_host->setText(settings.value(QStringLiteral("host"), defaults.host).toString());
    m_port->setValue(settings.value(QStringLiteral("port"), defaults.port).toInt());
    m_duration->setValue(settings.value(QStringLiteral("duration"), defaults.duration).toInt());
    m_parallel->setValue(settings.value(QStringLiteral("parallel"), defaults.parallel).toInt());
    m_bitrate->setText(settings.value(QStringLiteral("bitrate_bps"), QString()).toString());
    m_reverse->setChecked(settings.value(QStringLiteral("reverse"), defaults.reverse).toBool());
    m_bidirectional->setChecked(settings.value(QStringLiteral("bidirectional"), defaults.bidirectional).toBool());

    settings.endGroup();
    settings.endGroup();

    if (m_status != nullptr) {
        m_status->setText(tr("Loaded saved defaults"));
    }
}

void
SettingsPage::saveSettings()
{
    const IperfGuiConfig config = configuration();
    QSettings settings;

    settings.beginGroup(QStringLiteral("settings"));
    settings.beginGroup(QStringLiteral("defaults"));
    settings.setValue(QStringLiteral("mode"), modeToIndex(config.mode));
    settings.setValue(QStringLiteral("protocol"), protocolToIndex(config.protocol));
    settings.setValue(QStringLiteral("family"), familyToIndex(config.family));
    settings.setValue(QStringLiteral("host"), config.host);
    settings.setValue(QStringLiteral("port"), config.port);
    settings.setValue(QStringLiteral("duration"), config.duration);
    settings.setValue(QStringLiteral("parallel"), config.parallel);
    settings.setValue(QStringLiteral("bitrate_bps"), config.bitrateBps > 0 ? QString::number(config.bitrateBps) : QString());
    settings.setValue(QStringLiteral("reverse"), config.reverse);
    settings.setValue(QStringLiteral("bidirectional"), config.bidirectional);
    settings.endGroup();
    settings.endGroup();
    settings.sync();

    if (m_status != nullptr) {
        m_status->setText(tr("Saved defaults"));
    }
}

IperfGuiConfig
SettingsPage::configuration() const
{
    IperfGuiConfig config;
    const QString bitrateText = m_bitrate->text().trimmed();
    bool ok = false;

    config.mode = modeFromIndex(m_mode->currentIndex());
    config.protocol = protocolFromIndex(m_protocol->currentIndex());
    config.family = familyFromIndex(m_family->currentIndex());
    config.host = m_host->text().trimmed();
    config.port = m_port->value();
    config.duration = m_duration->value();
    config.parallel = m_parallel->value();
    if (!bitrateText.isEmpty()) {
        config.bitrateBps = bitrateText.toULongLong(&ok);
        if (!ok) {
            config.bitrateBps = 0;
        }
    }
    config.reverse = m_reverse->isChecked();
    config.bidirectional = m_bidirectional->isChecked();
    return config;
}

void
SettingsPage::applyConfiguration()
{
    saveSettings();
    if (m_bridge != nullptr) {
        m_bridge->setConfiguration(configuration());
        if (m_status != nullptr) {
            m_status->setText(tr("Applied to current session"));
        }
    } else if (m_status != nullptr) {
        m_status->setText(tr("Saved defaults"));
    }
}

void
SettingsPage::resetDefaults()
{
    const IperfGuiConfig defaults;

    m_mode->setCurrentIndex(modeToIndex(defaults.mode));
    m_protocol->setCurrentIndex(protocolToIndex(defaults.protocol));
    m_family->setCurrentIndex(familyToIndex(defaults.family));
    m_host->clear();
    m_port->setValue(defaults.port);
    m_duration->setValue(defaults.duration);
    m_parallel->setValue(defaults.parallel);
    m_bitrate->clear();
    m_reverse->setChecked(defaults.reverse);
    m_bidirectional->setChecked(defaults.bidirectional);
    if (m_status != nullptr) {
        m_status->setText(tr("Reset to built-in defaults"));
    }
}

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setSpacing(12);

    auto *title = new QLabel(tr("Settings & startup defaults"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600;"));
    outer->addWidget(title);

    auto *defaultsBox = new QGroupBox(tr("Startup defaults"), this);
    auto *form = new QFormLayout(defaultsBox);

    m_mode = makeComboBox({tr("Client"), tr("Server")});
    m_protocol = makeComboBox({tr("TCP"), tr("UDP")});
    m_family = makeComboBox({tr("Any"), tr("IPv4"), tr("IPv6")});
    m_host = makeLineEdit(tr("localhost"));
    m_port = makeSpinBox(1, 65535);
    m_duration = makeSpinBox(1, 3600);
    m_parallel = makeSpinBox(1, 64);
    m_bitrate = makeLineEdit(tr("bps, optional"));
    m_reverse = new QCheckBox(tr("Reverse"), defaultsBox);
    m_bidirectional = new QCheckBox(tr("Bidirectional"), defaultsBox);

    m_port->setValue(5201);
    m_duration->setValue(10);
    m_parallel->setValue(1);

    form->addRow(tr("Mode"), m_mode);
    form->addRow(tr("Protocol"), m_protocol);
    form->addRow(tr("Family"), m_family);
    form->addRow(tr("Host"), m_host);
    form->addRow(tr("Port"), m_port);
    form->addRow(tr("Duration"), m_duration);
    form->addRow(tr("Parallel"), m_parallel);
    form->addRow(tr("Bitrate"), m_bitrate);
    form->addRow(m_reverse);
    form->addRow(m_bidirectional);

    auto *buttonRow = new QHBoxLayout;
    m_apply = new QPushButton(tr("Apply to current session"), this);
    m_save = new QPushButton(tr("Save defaults"), this);
    m_reset = new QPushButton(tr("Reset built-in defaults"), this);
    buttonRow->addWidget(m_apply);
    buttonRow->addWidget(m_save);
    buttonRow->addWidget(m_reset);
    buttonRow->addStretch(1);

    m_runtimeInfo = new QLabel(this);
    m_buildInfo = new QLabel(this);
    m_featureNotes = new QPlainTextEdit(this);
    m_featureNotes->setReadOnly(true);
    m_featureNotes->setMinimumHeight(220);
    m_status = new QLabel(this);

    m_runtimeInfo->setText(QStringLiteral("Qt %1 | Platform %2 | Product %3 %4")
                           .arg(QString::fromLatin1(QT_VERSION_STR),
                                QGuiApplication::platformName(),
                                QSysInfo::prettyProductName(),
                                QSysInfo::currentCpuArchitecture()));
    m_buildInfo->setText(QStringLiteral("Build target: Windows UCRT64 Qt6 Widgets client/server GUI"));
    m_featureNotes->setPlainText(QStringLiteral(
        "These defaults are stored locally with Qt settings.\n"
        "Use Apply to push them into the current bridge, or Save to keep them for the next launch.\n"
        "Window geometry and the selected page are persisted by the main window."));

    m_status->setText(tr("Ready"));

    connect(m_apply, &QPushButton::clicked, this, &SettingsPage::applyConfiguration);
    connect(m_save, &QPushButton::clicked, this, &SettingsPage::saveSettings);
    connect(m_reset, &QPushButton::clicked, this, &SettingsPage::resetDefaults);

    outer->addWidget(defaultsBox);
    outer->addLayout(buttonRow);
    outer->addWidget(m_runtimeInfo);
    outer->addWidget(m_buildInfo);
    outer->addWidget(m_featureNotes, 1);
    outer->addWidget(m_status);

    loadSettings();
}
