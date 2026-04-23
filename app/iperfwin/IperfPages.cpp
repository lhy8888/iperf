#include "IperfPages.h"

#include "IperfCoreBridge.h"
#include "IperfTestOrchestrator.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <QAbstractSocket>
#include <QButtonGroup>
#include <QClipboard>
#include <QGuiApplication>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QTcpSocket>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QHostAddress>
#include <QLineEdit>
#include <QListWidget>
#include <QLayout>
#include <QMessageBox>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSharedPointer>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QComboBox>
#include <QStandardItemModel>
#include <QVariantList>
#include <QSysInfo>
#include <QTabBar>
#include <QAbstractTableModel>
#include <QTableView>
#include <QTextStream>
#include <QApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPalette>
#include <QStyleHints>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr char kIperfWinVersion[] = "1.1.2";
}

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
        QStringLiteral("<b>%1</b><br><span style='font-size:18px;'>--</span>").arg(name),
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

static QString exportAddressValue(const QString &value, const ExportOptions &options)
{
    return exportRedactedAddress(value, options);
}

static QString exportEndpointValue(const IperfGuiConfig &config, const ExportOptions &options)
{
    const QString resolved = !config.resolvedHostForRun.isEmpty()
        ? config.resolvedHostForRun
        : config.host;
    const QString endpoint = !resolved.isEmpty()
        ? resolved
        : (config.mode == IperfGuiConfig::Mode::Server ? config.listenAddress
                                                       : config.serverAddress);
    return exportRedactedAddress(endpoint, options);
}

static QString exportPasswordLabel(const QString &value, const ExportOptions &options)
{
    return options.includeSecrets ? value : QStringLiteral("[hidden]");
}

static std::optional<ExportOptions> chooseExportOptions(QWidget *parent, const QString &exportKind)
{
    QMessageBox box(parent);
    box.setWindowTitle(QStringLiteral("Export %1").arg(exportKind));
    box.setText(QStringLiteral("Choose export mode for %1.").arg(exportKind));
    auto *shareBtn = box.addButton(QStringLiteral("Share-safe"), QMessageBox::AcceptRole);
    auto *internalBtn = box.addButton(QStringLiteral("Internal"), QMessageBox::DestructiveRole);
    auto *cancelBtn = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(shareBtn);
    box.exec();
    if (box.clickedButton() == cancelBtn) {
        return std::nullopt;
    }
    if (box.clickedButton() == internalBtn) {
        return internalExportOptions();
    }
    return shareSafeExportOptions();
}

static QVariantMap firstSummaryMap(const QVariantMap &fields)
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
        const QVariant v = fields.value(key);
        if (v.isValid() && v.canConvert<QVariantMap>()) {
            return v.toMap();
        }
    }
    return {};
}

static QString csvEscapeCell(QString cell)
{
    if (cell.contains(QLatin1Char('"'))) {
        cell.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    }
    if (cell.contains(QLatin1Char(',')) || cell.contains(QLatin1Char('"'))
        || cell.contains(QLatin1Char('\n')) || cell.contains(QLatin1Char('\r'))) {
        cell = QStringLiteral("\"%1\"").arg(cell);
    }
    return cell;
}

static QString buildSessionReportMarkdown(const IperfSessionRecord &record, const ExportOptions &options)
{
    const bool isSrv = (record.config.mode == IperfGuiConfig::Mode::Server);
    const bool isMixed = (record.config.trafficMode == TrafficMode::Mixed
                          && !record.config.mixEntries.isEmpty());
    const QString endpoint = exportEndpointValue(record.config, options);
    const QString resolvedHost = exportAddressValue(record.config.resolvedHostForRun, options);
    const QVariantMap summary = firstSummaryMap(record.finalFields);
    const QString stateText = iperfRunStateText(record.runState, record.runStateDetail, record.escapedByLongjmp);

    QString out;
    QTextStream ts(&out);
    ts << "# IperfWin Report\n\n";
    ts << "- Export mode: " << (options.safeForSharing ? QStringLiteral("Share-safe") : QStringLiteral("Internal")) << "\n";
    ts << "- Time: " << record.startedAt.toString(Qt::ISODate) << "\n";
    ts << "- State: " << stateText << "\n";
    ts << "- Exit code: " << record.exitCode << "\n";
    if (!record.statusText.isEmpty() && record.statusText != stateText) {
        ts << "- Status text: " << record.statusText << "\n";
    }
    if (!record.diagnosticText.isEmpty()) {
        ts << "- Diagnostic: " << record.diagnosticText << "\n";
    }
    ts << "- Compatibility escape: " << (record.escapedByLongjmp ? QStringLiteral("yes") : QStringLiteral("no")) << "\n";
    ts << "- Probe session: " << (record.config.probeSession ? QStringLiteral("yes") : QStringLiteral("no")) << "\n";
    ts << "\n## Configuration\n\n";
    ts << "| Field | Value |\n";
    ts << "| --- | --- |\n";
    auto row = [&ts](const QString &field, QString value) {
        value.replace(QLatin1Char('|'), QStringLiteral("\\|"));
        ts << "| " << field << " | " << value << " |\n";
    };
    row(QStringLiteral("Mode"), iperfModeName(record.config.mode));
    row(QStringLiteral("Protocol"), iperfProtocolName(record.config.protocol));
    row(QStringLiteral("Traffic Mode"), trafficModeName(record.config.trafficMode));
    row(QStringLiteral("Traffic Type"), isMixed ? QStringLiteral("Mixed") : trafficTypeName(record.config.trafficType));
    row(QStringLiteral("Packet / Block Size"),
        isMixed
            ? QStringLiteral("Mixed")
            : QStringLiteral("%1 (%2 bytes)").arg(packetSizeName(record.config.packetSize))
                  .arg(record.config.blockSize));
    row(QStringLiteral("Direction"), directionName(record.config.direction));
    row(QStringLiteral("Endpoint"),
        endpoint.isEmpty() ? QStringLiteral("-")
                           : QStringLiteral("%1:%2").arg(endpoint).arg(record.config.port));
    row(QStringLiteral("Resolved Host For Run"), resolvedHost.isEmpty() ? QStringLiteral("-") : resolvedHost);
    row(QStringLiteral("Listen Address"),
        record.config.listenAddress.isEmpty()
            ? QStringLiteral("0.0.0.0")
            : exportAddressValue(record.config.listenAddress, options));
    row(QStringLiteral("Bind Address"),
        record.config.bindAddress.isEmpty()
            ? QStringLiteral("-")
            : exportAddressValue(record.config.bindAddress, options));
    row(QStringLiteral("Bind Device"),
        record.config.bindDev.isEmpty()
            ? QStringLiteral("-")
            : exportAddressValue(record.config.bindDev, options));
    row(QStringLiteral("Family"), iperfFamilyName(record.config.family));
    row(QStringLiteral("Force Family"), iperfFamilyName(record.config.forceFamily));
    row(QStringLiteral("Duration"),
        QStringLiteral("%1 (%2 s)").arg(durationPresetName(record.config.durationPreset))
            .arg(record.config.duration));
    row(QStringLiteral("Parallel"), QString::number(record.config.parallel));
    row(QStringLiteral("Bitrate"), iperfHumanBitsPerSecond(static_cast<double>(record.config.bitrateBps)));
    row(QStringLiteral("Preflight Status"),
        record.config.preflightStatus.isEmpty() ? QStringLiteral("-") : record.config.preflightStatus);
    row(QStringLiteral("Preflight Target"),
        record.config.preflightResolvedTargetAddress.isEmpty()
            ? QStringLiteral("-")
            : exportAddressValue(record.config.preflightResolvedTargetAddress, options));
    row(QStringLiteral("Preflight Source"),
        record.config.preflightSourceAddress.isEmpty()
            ? QStringLiteral("-")
            : exportAddressValue(record.config.preflightSourceAddress, options));
    row(QStringLiteral("Preflight Valid"), record.config.preflightValid ? QStringLiteral("yes") : QStringLiteral("no"));
    row(QStringLiteral("Preflight Family Match"),
        record.config.preflightFamilyMatch ? QStringLiteral("yes") : QStringLiteral("no"));
    row(QStringLiteral("Mixed Snapshot"),
        isMixed
            ? trafficMixEntriesText(record.config.mixEntries).replace(QLatin1Char('\n'), QStringLiteral(" | "))
            : QStringLiteral("-"));
    if (isMixed) {
        row(QStringLiteral("Mixed Rows"), QString::number(record.config.mixEntries.size()));
        const QVariantMap mixed = record.finalFields.value(QStringLiteral("mixed_bundle")).toMap();
        if (!mixed.isEmpty()) {
            row(QStringLiteral("Mixed Bundle"),
                QStringLiteral("%1 rows, %2 s")
                    .arg(mixed.value(QStringLiteral("row_count")).toInt())
                    .arg(mixed.value(QStringLiteral("total_duration_s")).toDouble(), 0, 'f', 0));
            const QString summary = mixed.value(QStringLiteral("summary")).toString();
            if (!summary.isEmpty()) {
                row(QStringLiteral("Mixed Summary"), summary);
            }
        }
    }

    ts << "\n## Results\n\n";
    ts << "| Field | Value |\n";
    ts << "| --- | --- |\n";
    row(QStringLiteral("Peak Throughput"), iperfHumanBitsPerSecond(record.peakBps));
    row(QStringLiteral("Stable Throughput"), iperfHumanBitsPerSecond(record.stableBps));
    row(QStringLiteral("Loss"), iperfHumanPercent(record.lossPercent));
    row(QStringLiteral("Intervals Captured"), QString::number(record.intervalArchive.size()));
    if (summary.contains(QStringLiteral("jitter_ms"))) {
        row(QStringLiteral("Jitter"),
            QStringLiteral("%1 ms").arg(summary.value(QStringLiteral("jitter_ms")).toDouble(), 0, 'f', 3));
    } else {
        row(QStringLiteral("Jitter"), QStringLiteral("-"));
    }
    if (summary.contains(QStringLiteral("retransmits"))) {
        row(QStringLiteral("Retransmits"), QString::number(summary.value(QStringLiteral("retransmits")).toInt()));
    } else if (summary.contains(QStringLiteral("lost_packets"))) {
        row(QStringLiteral("Retransmits"), QString::number(summary.value(QStringLiteral("lost_packets")).toInt()));
    } else {
        row(QStringLiteral("Retransmits"), QStringLiteral("-"));
    }
    if (!record.finalFields.isEmpty()) {
        row(QStringLiteral("Final Fields"), QString::number(record.finalFields.size()));
    }

    if (options.includeRawJson && !record.rawJson.isEmpty()) {
        ts << "\n## Raw JSON\n\n";
        ts << "```json\n" << record.rawJson << "\n```\n";
    }

    return out;
}

} // namespace

// ============================================================================
// IntervalArchiveModel
// ============================================================================
class IntervalArchiveModel : public QAbstractTableModel
{
public:
    enum Column {
        TimeRange = 0,
        Throughput,
        RetransmitsOrLoss,
        Jitter,
        Direction,
        ColumnCount
    };

    explicit IntervalArchiveModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_samples.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_samples.size()) {
            return {};
        }
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
            return {};
        }

        const IperfIntervalSample &sample = m_samples.at(index.row());
        switch (index.column()) {
        case TimeRange:
            return QStringLiteral("%1 - %2 s")
                .arg(sample.startSec, 0, 'f', 1)
                .arg(sample.endSec, 0, 'f', 1);
        case Throughput:
            return iperfHumanBitsPerSecond(sample.throughputBps);
        case RetransmitsOrLoss:
            if (sample.retransmits >= 0) {
                if (sample.lossPercent >= 0.0) {
                    return QStringLiteral("%1 (%2%)")
                        .arg(sample.retransmits)
                        .arg(sample.lossPercent, 0, 'f', 2);
                }
                return QString::number(sample.retransmits);
            }
            if (sample.lossPercent >= 0.0) {
                return QStringLiteral("%1%").arg(sample.lossPercent, 0, 'f', 2);
            }
            return QStringLiteral("--");
        case Jitter:
            return sample.jitterMs >= 0.0
                ? QStringLiteral("%1 ms").arg(sample.jitterMs, 0, 'f', 3)
                : QStringLiteral("--");
        case Direction:
            return sample.direction.isEmpty()
                ? (sample.summaryKey.isEmpty() ? QStringLiteral("--") : sample.summaryKey)
                : sample.direction;
        case ColumnCount:
        default:
            break;
        }
        return {};
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole) {
            return {};
        }
        if (orientation == Qt::Horizontal) {
            switch (section) {
            case TimeRange: return QStringLiteral("Time (s)");
            case Throughput: return QStringLiteral("Throughput");
            case RetransmitsOrLoss: return QStringLiteral("Retrans / Lost");
            case Jitter: return QStringLiteral("Jitter");
            case Direction: return QStringLiteral("Dir");
            default: break;
            }
        }
        return section + 1;
    }

    void clear()
    {
        beginResetModel();
        m_samples.clear();
        endResetModel();
    }

    void appendSample(const IperfIntervalSample &sample)
    {
        if (m_samples.size() >= kMaxRows) {
            beginRemoveRows(QModelIndex(), 0, 0);
            m_samples.removeFirst();
            endRemoveRows();
        }
        const int row = m_samples.size();
        beginInsertRows(QModelIndex(), row, row);
        m_samples.push_back(sample);
        endInsertRows();
    }

private:
    QVector<IperfIntervalSample> m_samples;
    static constexpr int kMaxRows = 3600;
};

// ============================================================================
// ThroughputChart 闂?lightweight QPainter line chart, no Qt Charts required
// Defined in the .cpp so the header only needs a forward declaration.
// ============================================================================
class ThroughputChart : public QWidget
{
    // No Q_OBJECT 闂?no signals/slots needed; pure paintEvent widget.
public:
    explicit ThroughputChart(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(130);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    // Call once per interval event (one sample 闂?one second of test)
    void addSample(double bps)
    {
        if (m_samples.size() >= kMaxSamples) { m_samples.removeFirst(); }
        m_samples.append(bps);
        update();
    }

    void clear()
    {
        m_samples.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRect r = rect();
        // Margins: left for Y labels, bottom for X labels
        const int ml = 58, mr = 8, mt = 6, mb = 18;
        const QRect plot(ml, mt, r.width() - ml - mr, r.height() - mt - mb);
        if (plot.width() < 10 || plot.height() < 10) { return; }

        p.fillRect(r, QColor(0xf8, 0xf8, 0xf8));
        p.setPen(QPen(QColor(0xcc, 0xcc, 0xcc), 1));
        p.drawRect(plot.adjusted(-1, -1, 0, 0));

        const int totalCount = m_samples.size();
        if (totalCount < 1) { return; }

        // Downsample to the current plot width so a 24h run remains readable
        // without forcing the widget to render tens of thousands of points.
        const int visCount = qMin(totalCount, plot.width());
        if (visCount < 1) { return; }
        const double samplesPerBin = double(totalCount) / double(visCount);
        QVector<double> visSamples;
        visSamples.reserve(visCount);
        for (int i = 0; i < visCount; ++i) {
            const int begin = static_cast<int>(std::floor(double(i) * samplesPerBin));
            const int end = static_cast<int>(std::floor(double(i + 1) * samplesPerBin));
            const int last = qMin(end, totalCount);
            double sum = 0.0;
            int count = 0;
            for (int j = begin; j < last; ++j) {
                sum += m_samples.at(j);
                ++count;
            }
            visSamples.append(count > 0 ? (sum / count) : 0.0);
        }

        // Y scale: find max of visible samples, then round up to a "nice" ceiling
        double dataMax = 1.0;
        for (double sample : visSamples) {
            if (sample > dataMax) { dataMax = sample; }
        }
        const double yMax = niceScale(dataMax);

        // Horizontal grid lines (0%, 25%, 50%, 75%, 100%)
        QFont sf = p.font();
        sf.setPointSize(7);
        p.setFont(sf);
        for (int g = 0; g <= 4; ++g) {
            const double frac = g / 4.0;
            const int y = plot.bottom() - qRound(frac * plot.height());
            if (g > 0 && g < 4) {
                p.setPen(QPen(QColor(0xe0, 0xe0, 0xe0), 1, Qt::DashLine));
                p.drawLine(plot.left(), y, plot.right(), y);
            }
            p.setPen(QColor(0x99, 0x99, 0x99));
            const QString lbl = shortBps(frac * yMax);
            p.drawText(QRect(0, y - 8, ml - 4, 16),
                       Qt::AlignRight | Qt::AlignVCenter, lbl);
        }

        if (visCount < 1) { return; }

        // X-axis tick marks every 10/60/300/1800/7200 seconds depending on span
        const int totalSecs = totalCount;
        const int xStep = totalSecs <= 60    ? 10 :
                          totalSecs <= 300   ? 60 :
                          totalSecs <= 3600  ? 300 :
                          totalSecs <= 21600 ? 1800 : 7200;
        p.setPen(QColor(0x99, 0x99, 0x99));
        for (int s = 0; s <= totalSecs; s += xStep) {
            const int px = (visCount <= 1) ? plot.left()
                : plot.left() + qRound(double(s) / qMax(1, totalSecs) * plot.width());
            const QString lbl = QStringLiteral("%1s").arg(s);
            p.drawText(QRect(px - 18, plot.bottom() + 2, 36, mb - 2),
                       Qt::AlignHCenter | Qt::AlignTop, lbl);
        }

        // Build polyline
        QPolygonF poly;
        poly.reserve(visCount);
        for (int i = 0; i < visCount; ++i) {
            const double bps = visSamples.at(i);
            const double fx = (visCount <= 1) ? plot.left()
                : plot.left() + double(i) / (visCount - 1) * plot.width();
            const double fy = plot.bottom() - (bps / yMax) * plot.height();
            poly.append({fx, fy});
        }

        // Filled area under the curve
        QPolygonF fill = poly;
        fill.prepend({poly.first().x(), double(plot.bottom())});
        fill.append ({poly.last().x(),  double(plot.bottom())});
        QLinearGradient grad(0, plot.top(), 0, plot.bottom());
        grad.setColorAt(0.0, QColor(0x00, 0x88, 0xff, 80));
        grad.setColorAt(1.0, QColor(0x00, 0x88, 0xff, 10));
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawPolygon(fill);

        // Line
        p.setPen(QPen(QColor(0x00, 0x66, 0xcc), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(poly);
    }

private:
    static constexpr int kMaxSamples = 86400; // 24 hours at 1-s intervals

    // Round maxBps up to the nearest 1/2/5 闁?10^N
    static double niceScale(double maxBps)
    {
        if (maxBps <= 0.0) { return 1e6; }
        const double mag  = std::pow(10.0, std::floor(std::log10(maxBps)));
        const double norm = maxBps / mag;
        double nice = 10.0;
        if      (norm <= 1.0) { nice = 1.0; }
        else if (norm <= 2.0) { nice = 2.0; }
        else if (norm <= 5.0) { nice = 5.0; }
        return nice * mag;
    }

    // Compact axis label: "500M", "1.2G", "300K", etc.
    static QString shortBps(double bps)
    {
        if (bps == 0.0) { return QStringLiteral("0"); }
        if (bps >= 1e9) {
            return QStringLiteral("%1G").arg(bps / 1e9, 0, 'f', bps >= 10e9 ? 0 : 1);
        }
        if (bps >= 1e6) {
            return QStringLiteral("%1M").arg(bps / 1e6, 0, 'f', bps >= 10e6 ? 0 : 1);
        }
        if (bps >= 1e3) {
            return QStringLiteral("%1K").arg(bps / 1e3, 0, 'f', 0);
        }
        return QStringLiteral("%1").arg(qRound(bps));
    }

    QVector<double> m_samples;
};

// ============================================================================
// TestPage
// ============================================================================
TestPage::TestPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);
    root->setSizeConstraint(QLayout::SetMinAndMaxSize);

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?Role toggle 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜?
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

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?Role stacked area (client / server) 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸?
    m_roleStack = new QStackedWidget(this);
    m_roleStack->addWidget(buildClientArea());   // 0
    m_roleStack->addWidget(buildServerArea());   // 1
    root->addWidget(m_roleStack);

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?Expert panel (hidden by default) 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑?
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

        m_forceFamilyCombo = new QComboBox(m_expertPanel);
        m_forceFamilyCombo->addItems({QStringLiteral("Any"), QStringLiteral("IPv4"), QStringLiteral("IPv6")});
        fl->addRow(QStringLiteral("Force Family:"), m_forceFamilyCombo);
    }
    root->addWidget(m_expertPanel);

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?Action bar 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕
    {
        auto *bar = new QHBoxLayout;
        bar->setSpacing(8);
        m_startBtn  = new QPushButton(QStringLiteral("Start Test"), this);
        m_stopBtn   = new QPushButton(QStringLiteral("Stop"),       this);
        m_exportBtn = new QPushButton(QStringLiteral("Export Report"),  this);
        m_startBtn->setFixedHeight(32);
        m_stopBtn->setFixedHeight(32);
        m_exportBtn->setFixedHeight(32);
        m_stopBtn->setEnabled(false);
        m_exportBtn->setEnabled(false);
        m_statusLabel = new QLabel(QStringLiteral("Idle"), this);
        m_statusLabel->setStyleSheet(iperfRunStateBadgeStyle(IperfRunState::Idle));
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

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?Results area 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜?
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

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?Server address (editable combo + star button) 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑?
    {
        auto *bar = new QHBoxLayout;
        bar->setSpacing(6);

        m_serverAddress = new QComboBox(w);
        m_serverAddress->setEditable(true);
        m_serverAddress->setInsertPolicy(QComboBox::NoInsert);
        m_serverAddress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (m_serverAddress->lineEdit()) {
            m_serverAddress->lineEdit()->setPlaceholderText(
                QStringLiteral("Server IP or hostname"));
        }

        m_starBtn = new QPushButton(QStringLiteral("\u2606"), w);  // 闂?(empty star)
        m_starBtn->setFixedSize(28, 28);
        m_starBtn->setCheckable(true);
        m_starBtn->setToolTip(QStringLiteral("Star this target to keep it at the top of the list"));
        m_starBtn->setStyleSheet(
            QStringLiteral("QPushButton{border:1px solid #bbb;border-radius:4px;"
                           "font-size:14px;padding:0;background:#f5f5f5;}"
                           "QPushButton:checked{background:#ffe066;border-color:#cca800;"
                           "color:#7a5c00;}"
                           "QPushButton:hover{background:#e8e8e8;}"));

        bar->addWidget(new QLabel(QStringLiteral("Server Address:"), w));
        bar->addWidget(m_serverAddress, 1);
        bar->addWidget(m_starBtn);
        vl->addLayout(bar);

        // Pre-flight status label (shown below the address bar)
        m_preflightLabel = new QLabel(w);
        m_preflightLabel->setStyleSheet(
            QStringLiteral("color:#888; font-size:11px; margin-top:0px;"));
        m_preflightLabel->setText(QString());
        vl->addWidget(m_preflightLabel);

        // 800 ms debounce timer
        m_preflightTimer = new QTimer(this);
        m_preflightTimer->setSingleShot(true);
        m_preflightTimer->setInterval(800);

        // Populate combo from persisted recent targets
        loadRecentTargets();

        connect(m_serverAddress, &QComboBox::currentTextChanged,
                this, &TestPage::onAddressTextChanged);

        // When user picks from the dropdown, set edit text to bare address
        connect(m_serverAddress, QOverload<int>::of(&QComboBox::activated),
                this, [this](int idx) {
            const QString addr = m_serverAddress->itemData(idx).toString();
            if (!addr.isEmpty()) {
                // Temporarily block signals so we don't double-fire onAddressTextChanged
                m_serverAddress->blockSignals(true);
                m_serverAddress->setEditText(addr);
                m_serverAddress->blockSignals(false);
                updateStarButton(addr);
                // Restart preflight with the newly-selected address
                if (m_preflightTimer) { m_preflightTimer->start(); }
                setPreflightStatus(QStringLiteral("Checking\u2026"), QStringLiteral("#888"));
            }
        });

        connect(m_starBtn,        &QPushButton::clicked,
                this, &TestPage::onStarClicked);
        connect(m_preflightTimer, &QTimer::timeout,
                this, &TestPage::onPreflightTimerFired);
    }

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?Client NIC selector (source interface) 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕
    {
        auto *bar = new QHBoxLayout;
        bar->setSpacing(6);
        m_clientNicSelector = new QComboBox(w);
        m_clientNicSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        populateClientNicSelector();
        auto *nicLabel = new QLabel(QStringLiteral("Source NIC:"), w);
        nicLabel->setToolTip(
            QStringLiteral("Local network interface to send traffic from.\n"
                           "\"Auto\" lets the OS pick based on routing tables."));
        bar->addWidget(nicLabel);
        bar->addWidget(m_clientNicSelector, 1);
        vl->addLayout(bar);
    }

    // Traffic-mode stacked (0=single, 1=mixed)
    m_trafficModeStack = new QStackedWidget(w);

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?[0] Single 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜?
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
        auto *psLabel = new QLabel(QStringLiteral("Block / Datagram Size:"), sw);
        psLabel->setToolTip(
            QStringLiteral("UDP: controls datagram size (close to on-wire packet size).\n"
                           "TCP: controls application write block size.\n"
                           "Actual wire frames are shaped by MSS, TSO/GSO and NIC offload."));
        pl->addWidget(psLabel);
        pl->addWidget(m_packetSize);
        hl->addLayout(tl);
        hl->addLayout(pl);
        hl->addStretch();
        m_trafficModeStack->addWidget(sw);
    }

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?[1] Mixed 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸?
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
        makeH(QStringLiteral("Block / Datagram Size"),  2);
        makeH(QStringLiteral("Ratio"),        1);
        hdr->addSpacing(28);
        mvl->addLayout(hdr);

        m_mixContainer = new QWidget(mw);
        m_mixContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_mixLayout    = new QVBoxLayout(m_mixContainer);
        m_mixLayout->setContentsMargins(0, 0, 0, 0);
        m_mixLayout->setSpacing(2);
        m_mixLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
        mvl->addWidget(m_mixContainer);

        // Footer
        auto *footer = new QHBoxLayout;
        auto *addBtn = new QPushButton(QStringLiteral("+ Add Row"), mw);
        addBtn->setFixedHeight(26);
        m_mixTotalLabel = new QLabel(QStringLiteral("Total: 0%"), mw);
        footer->addWidget(addBtn);
        footer->addStretch();
        footer->addWidget(m_mixTotalLabel);
        mvl->addLayout(footer);

        auto *mixNote = new QLabel(
            QStringLiteral("Mixed mode runs all rows internally as a bundle across "
                           "the selected duration. History and reports keep the full "
                           "mix table."),
            mw);
        mixNote->setWordWrap(true);
        mixNote->setStyleSheet(QStringLiteral("color:#9a6700;"));
        mvl->addWidget(mixNote);

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
            { "Continuous", DurationPreset::Continuous },
        };
        for (const auto &e : entries) {
            auto *btn = makeToggleBtn(QString::fromUtf8(e.lbl), m_durationGroup, w);
            btn->setProperty("durationPreset", QVariant::fromValue(e.preset));
            // Default: 1 h; long enough for stability testing without committing to 24h
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
            // Default: Bidirectional; measures full-duplex path capacity
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

    // Connected client info (updated live when a client connects)
    {
        m_serverClientLabel = new QLabel(w);
        m_serverClientLabel->setStyleSheet(
            QStringLiteral("color:#007700; font-size:12px; font-weight:bold; "
                           "padding:4px 0px;"));
        m_serverClientLabel->setText(QString());
        vl->addWidget(m_serverClientLabel);
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
    // "All interfaces" sentinel 闂?listenAddress will be empty 闂?0.0.0.0
    m_nicSelector->addItem(QStringLiteral("All interfaces  (0.0.0.0)"), QString());

    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp)) { continue; }
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) { continue; }
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            // Skip link-local IPv6 (fe80::闂? 闂?rarely useful for testing
            if (addr.isLinkLocal()) { continue; }
            const QString display = QStringLiteral("%1   %2")
                .arg(iface.humanReadableName(), addr.toString());
            m_nicSelector->addItem(display, addr.toString());
        }
    }
}

// ---------------------------------------------------------------------------
void TestPage::populateClientNicSelector()
{
    if (!m_clientNicSelector) { return; }
    m_clientNicSelector->clear();
    // "Auto" sentinel 闂?bindAddress will be empty 闂?OS picks source IP via routing
    m_clientNicSelector->addItem(QStringLiteral("Auto  (OS routing)"), QString());

    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp)) { continue; }
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) { continue; }
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            if (addr.isLinkLocal()) { continue; }
            const QString display = QStringLiteral("%1   %2")
                .arg(iface.humanReadableName(), addr.toString());
            m_clientNicSelector->addItem(display, addr.toString());
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

    // Throughput chart 闂?always visible, above the tabs
    m_throughputChart = new ThroughputChart(w);
    vl->addWidget(m_throughputChart);

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
    m_intervalTable = new QTableView(this);
    m_intervalModel = new IntervalArchiveModel(m_intervalTable);
    m_intervalTable->setModel(m_intervalModel);
    m_intervalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_intervalTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_intervalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_intervalTable->setAlternatingRowColors(true);
    m_intervalTable->setWordWrap(false);
    m_intervalTable->verticalHeader()->setVisible(false);
    m_intervalTable->horizontalHeader()->setStretchLastSection(true);
    m_intervalTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_intervalTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_intervalTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_intervalTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_intervalTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
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
        const QString saved = s.value(QStringLiteral("test/serverAddress")).toString();
        if (!saved.isEmpty()) {
            m_serverAddress->setEditText(saved);
            updateStarButton(saved);
        }
    }
    if (m_customPortSpin) {
        m_customPortSpin->setValue(s.value(QStringLiteral("test/customPort"), 0).toInt());
    }
    if (m_bindAddrEdit) {
        m_bindAddrEdit->setText(s.value(QStringLiteral("test/bindAddress")).toString());
    }
    if (m_forceFamilyCombo) {
        m_forceFamilyCombo->setCurrentIndex(
            familyToIndex(static_cast<IperfGuiConfig::AddressFamily>(
                s.value(QStringLiteral("test/forceFamily"), familyToIndex(IperfGuiConfig::AddressFamily::Any)).toInt())));
    }
    // Server NIC selection: restore by stored IP address
    if (m_nicSelector) {
        const QString savedIp = s.value(QStringLiteral("test/nicAddress")).toString();
        if (!savedIp.isEmpty()) {
            const int idx = m_nicSelector->findData(savedIp);
            if (idx >= 0) { m_nicSelector->setCurrentIndex(idx); }
        }
    }
    // Client NIC selection
    if (m_clientNicSelector) {
        const QString savedClientIp = s.value(QStringLiteral("test/clientNicAddress")).toString();
        if (!savedClientIp.isEmpty()) {
            const int idx = m_clientNicSelector->findData(savedClientIp);
            if (idx >= 0) { m_clientNicSelector->setCurrentIndex(idx); }
        }
    }
}

void TestPage::saveSettings(QSettings &s) const
{
    if (m_serverAddress) {
        s.setValue(QStringLiteral("test/serverAddress"),
                   m_serverAddress->currentText().trimmed());
    }
    if (m_customPortSpin) {
        s.setValue(QStringLiteral("test/customPort"), m_customPortSpin->value());
    }
    if (m_bindAddrEdit) {
        s.setValue(QStringLiteral("test/bindAddress"), m_bindAddrEdit->text().trimmed());
    }
    if (m_forceFamilyCombo) {
        s.setValue(QStringLiteral("test/forceFamily"), m_forceFamilyCombo->currentIndex());
    }
    if (m_nicSelector) {
        s.setValue(QStringLiteral("test/nicAddress"),
                   m_nicSelector->currentData().toString());
    }
    if (m_clientNicSelector) {
        s.setValue(QStringLiteral("test/clientNicAddress"),
                   m_clientNicSelector->currentData().toString());
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
        ? QStringLiteral("Start Test")
        : QStringLiteral("Start Server"));
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

    if (isClient && m_serverAddress->currentText().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Missing Input"),
                             QStringLiteral("Please enter a Server Address."));
        return;
    }

    // Mixed mode validation
    if (isClient && m_mixedModeBtn->isChecked()) {
        int total = 0;
        for (const auto &row : m_mixRows) { total += row.ratioSpin->value(); }
        if (total != 100) {
            QMessageBox::warning(this, QStringLiteral("Mixed Mode"),
                QStringLiteral("Traffic ratios must sum to 100%% (current: %1%%).").arg(total));
            return;
        }
    }

    // Record this target in the recent-targets list so it appears in the dropdown next time
    if (isClient) {
        addRecentTarget(m_serverAddress->currentText().trimmed());
    }

    m_baseConfig = buildEffectiveConfigForPreflight();
    applyPreflightResult(m_baseConfig);
    m_bridge->setConfiguration(m_baseConfig);

    setControlsEnabled(false);
    m_stopBtn->setEnabled(true);
    m_exportBtn->setEnabled(false);
    m_rawOutput->clear();
    if (m_intervalModel) {
        m_intervalModel->clear();
    }
    m_runningPeakBps = 0.0;
    if (m_throughputChart)   { m_throughputChart->clear(); }
    if (m_serverClientLabel) { m_serverClientLabel->clear(); }

    // Reset overview
    setMetricLabel(m_ovPeak,   QStringLiteral("Peak Throughput"),   QStringLiteral("--"));
    setMetricLabel(m_ovStable, QStringLiteral("Stable Throughput"), QStringLiteral("--"));
    setMetricLabel(m_ovLoss,   QStringLiteral("Loss"),              QStringLiteral("--"));
    setMetricLabel(m_ovJitter, QStringLiteral("Jitter / Retrans"),  QStringLiteral("--"));

    if (isClient) {
        if (m_mixedModeBtn && m_mixedModeBtn->isChecked()) {
            m_phase = Phase::MixedRunning;
            m_mixedAbortRequested = false;
            m_mixedStepRecords.clear();
            m_mixedPlan = buildMixedStepConfigs(m_baseConfig);
            m_mixedContinuous = (m_baseConfig.duration == 0);
            m_mixedStartedAt = QDateTime::currentDateTime();
            m_mixedStepIndex = 0;

            if (m_mixedPlan.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Mixed Mode"),
                                     QStringLiteral("Mixed mode requires at least one row."));
                setControlsEnabled(true);
                m_stopBtn->setEnabled(false);
                setStatus(IperfRunState::Failed, QStringLiteral("No mixed rows"));
                return;
            }

            setStatus(IperfRunState::Sustaining, mixedStepStatusText(0));
            startMixedStep();
            return;
        }

        m_phase = Phase::Probing;
        setStatus(IperfRunState::Probing);

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
        setStatus(IperfRunState::Listening,
                  QStringLiteral("%1:%2")
                      .arg(m_baseConfig.listenAddress.isEmpty()
                           ? QStringLiteral("0.0.0.0")
                           : m_baseConfig.listenAddress)
                      .arg(m_baseConfig.port));
        m_bridge->start();
    }
}

// ---------------------------------------------------------------------------
void TestPage::onStopClicked()
{
    m_serverPersist = false;   // prevent server auto-restart on this stop
    if (m_phase == Phase::MixedRunning) {
        m_mixedAbortRequested = true;
    }
    if (m_orchestrator != nullptr && m_orchestrator->isRunning()) {
        m_orchestrator->abort();
    } else if (m_bridge != nullptr && m_bridge->isRunning()) {
        m_bridge->stop();
    }
    setStatus(IperfRunState::Stopping);
}

// ---------------------------------------------------------------------------
void TestPage::onExportClicked()
{
    if (!m_hasSession) { return; }
    const std::optional<ExportOptions> options = chooseExportOptions(this, QStringLiteral("Report"));
    if (!options.has_value()) { return; }
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Report"),
        QStringLiteral("%1/iperf_report_%2_%3.md")
            .arg(QDir::homePath(),
                 m_lastSession.startedAt.toString(QStringLiteral("yyyyMMdd_HHmmss")),
                 options->safeForSharing ? QStringLiteral("share") : QStringLiteral("internal")),
        QStringLiteral("Markdown files (*.md);;All files (*)"));
    if (path.isEmpty()) { return; }
    QString err;
    if (!writeTextFile(path, buildReportMarkdown(m_lastSession, *options), &err)) {
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
    if (m_mixContainer) {
        m_mixContainer->adjustSize();
        m_mixContainer->updateGeometry();
    }
    if (layout()) {
        layout()->invalidate();
        layout()->activate();
    }
    updateGeometry();
}

// ---------------------------------------------------------------------------
void TestPage::onBridgeRunningChanged(bool running)
{
    if (running) {
        // Bridge started a new probe step or the sustain phase 闂?keep UI locked.
        if (m_phase == Phase::Probing) {
            // Each probe step is a fresh 5-second run; clear the table so
            // timestamps don't repeat (0闂? s, then 0闂? s again, 闂?.
            if (m_intervalModel) {
                m_intervalModel->clear();
            }
        }
        if (m_phase == Phase::Probing || m_phase == Phase::Sustaining) {
            setControlsEnabled(false);
            m_stopBtn->setEnabled(true);
        }
    } else {
        // Bridge stopped.
        if (m_phase == Phase::Sustaining) {
            // Check whether the stop was user-initiated (explicit Stop click sets
            // the bridge run state to Stopping/Stopped) vs natural completion.
            const IperfSessionRecord session = m_bridge ? m_bridge->currentSession()
                                                         : IperfSessionRecord();
            const bool explicitStop = session.runState == IperfRunState::Stopping
                                   || session.runState == IperfRunState::Stopped;

            if (m_serverPersist && m_serverBtn && m_serverBtn->isChecked() && !explicitStop) {
                // Server mode: session finished naturally 闂?schedule an auto-
                // restart so the server keeps accepting clients without a manual
                // Start click.
                //
                // IMPORTANT: this slot is invoked synchronously (direct
                // connection, same thread) from inside
                // IperfCoreBridge::finishSessionOnGuiThread(), which still holds
                // m_mutex at this point.  Calling bridge->start() here directly
                // would try to re-lock the same non-recursive mutex 闂?deadlock.
                // QTimer::singleShot(0) defers to the next event-loop iteration,
                // after finishSessionOnGuiThread() has returned and released the
                // lock.
                setStatus(IperfRunState::Listening, QStringLiteral("waiting for client"));
                QTimer::singleShot(0, this, [this]() {
                    if (!m_serverPersist || !m_bridge || m_bridge->isRunning()) {
                        return; // stop was requested or bridge already restarted
                    }
                    if (m_intervalModel) {
                        m_intervalModel->clear();
                    }
                    m_rawOutput->clear();
                    m_runningPeakBps = 0.0;
                    if (m_serverClientLabel) { m_serverClientLabel->clear(); }
                    if (m_throughputChart)   { m_throughputChart->clear(); }
                    setMetricLabel(m_ovPeak,   QStringLiteral("Peak Throughput"),   QStringLiteral("--"));
                    setMetricLabel(m_ovStable, QStringLiteral("Stable Throughput"), QStringLiteral("--"));
                    setMetricLabel(m_ovLoss,   QStringLiteral("Loss"),              QStringLiteral("--"));
                    setMetricLabel(m_ovJitter, QStringLiteral("Jitter / Retrans"),  QStringLiteral("--"));
                    m_bridge->start();
                });
                return;
            }

            // Sustain finished (or explicitly stopped) 闂?back to Idle.
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
    // Server mode: parse client connection info from the "start" event
    if (event.kind == IperfEventKind::Started && m_serverClientLabel) {
        const QVariantList connected =
            event.fields.value(QStringLiteral("connected")).toList();
        if (!connected.isEmpty()) {
            const QVariantMap conn = connected.constFirst().toMap();
            const QString remoteHost = conn.value(QStringLiteral("remote_host")).toString();
            const int remotePort     = conn.value(QStringLiteral("remote_port")).toInt();
            if (!remoteHost.isEmpty()) {
                m_serverClientLabel->setText(
                    QStringLiteral("\u25cf  Client connected: %1 : %2")
                    .arg(remoteHost).arg(remotePort));
            }
        }
    }

    if (event.kind == IperfEventKind::Interval) {
        addIntervalRow(event);
        applyOverviewFromEvent(event);

        // Feed the throughput chart 闂?pick the first sum key with a bps value
        if (m_throughputChart) {
            const QStringList sumKeys = {
                QStringLiteral("sum"), QStringLiteral("sum_sent"),
                QStringLiteral("sum_received"),
            };
            for (const QString &k : sumKeys) {
                const QVariant v = event.fields.value(k);
                if (!v.isValid() || !v.canConvert<QVariantMap>()) { continue; }
                const double bps =
                    v.toMap().value(QStringLiteral("bits_per_second")).toDouble();
                if (bps > 0.0) {
                    m_throughputChart->addSample(bps);
                    break;
                }
            }
        }
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
    if (record.config.suppressHistory && m_phase == Phase::MixedRunning) {
        m_mixedStepRecords.push_back(record);
        applyOverviewFromSession(record);

        const bool failed = (record.runState == IperfRunState::Failed || record.exitCode != 0);
        if (failed || m_mixedAbortRequested) {
            finishMixedBundle(m_mixedAbortRequested);
            return;
        }

        if (m_mixedStepIndex + 1 < m_mixedPlan.size()) {
            ++m_mixedStepIndex;
            setStatus(IperfRunState::Sustaining, mixedStepStatusText(m_mixedStepIndex));
            QTimer::singleShot(0, this, [this]() { startMixedStep(); });
            return;
        }

        if (m_mixedContinuous && !m_mixedAbortRequested) {
            m_mixedStepIndex = 0;
            setStatus(IperfRunState::Sustaining, mixedStepStatusText(0));
            QTimer::singleShot(0, this, [this]() { startMixedStep(); });
            return;
        }

        finishMixedBundle(false);
        return;
    }

    m_lastSession = record;
    applyOverviewFromSession(record);

    if (record.config.probeSession) {
        // Probe steps are live calibration runs. Keep the current summary up to
        // date, but do not promote them into Results/History or export state.
        return;
    }

    m_hasSession  = true;
    emit sessionRecorded(record);

    if (m_phase == Phase::Sustaining) {
        m_exportBtn->setEnabled(true);
        setStatus(record.runState, record.runStateDetail, record.escapedByLongjmp);
    }
    // Phase::Probing: each probe step also emits sessionCompleted, but the
    // orchestrator owns the state machine; do not touch controls here.
}

// ---------------------------------------------------------------------------
void TestPage::onOrchestratorStepStarted(int step, const QString &description)
{
    Q_UNUSED(step)
    setStatus(IperfRunState::Probing, description);
}

void TestPage::onOrchestratorStepCompleted(int step, double stableBps, double lossPercent)
{
    Q_UNUSED(stableBps)
    setStatus(IperfRunState::Probing,
              QStringLiteral("step %1 · loss %2%")
                  .arg(step + 1)
                  .arg(lossPercent, 0, 'f', 2));
}

void TestPage::onOrchestratorFoundMax(double stableBps, double peakBps,
                                       int optimalParallel, double maxUdpBps)
{
    Q_UNUSED(peakBps)
    m_optimalParallel = optimalParallel;
    m_optimalUdpBps   = maxUdpBps;
    Q_UNUSED(stableBps)
    setStatus(IperfRunState::Sustaining,
              QStringLiteral("parallel=%1").arg(optimalParallel));
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
        setStatus(IperfRunState::Stopped);
        return;
    }

    // Start the sustained phase at optimal load.
    // Clear probe-phase artifacts from the UI so only sustained results appear.
    if (m_intervalModel) {
        m_intervalModel->clear();
    }
    m_rawOutput->clear();
    m_runningPeakBps = 0.0;
    if (m_throughputChart) { m_throughputChart->clear(); }
    setMetricLabel(m_ovPeak,   QStringLiteral("Peak Throughput"),   QStringLiteral("--"));
    setMetricLabel(m_ovStable, QStringLiteral("Stable Throughput"), QStringLiteral("--"));
    setMetricLabel(m_ovLoss,   QStringLiteral("Loss"),              QStringLiteral("--"));
    setMetricLabel(m_ovJitter, QStringLiteral("Jitter / Retrans"),  QStringLiteral("--"));

    IperfGuiConfig cfg = m_baseConfig;
    cfg.parallel  = m_optimalParallel;
    cfg.duration  = durationPresetToSeconds(m_baseConfig.durationPreset);
    cfg.probeSession = false;
    if (m_baseConfig.trafficType == TrafficType::Udp && m_optimalUdpBps > 0.0) {
        cfg.bitrateBps = static_cast<quint64>(m_optimalUdpBps);
    }

    m_phase = Phase::Sustaining;
    setStatus(IperfRunState::Sustaining,
              QStringLiteral("parallel=%1").arg(m_optimalParallel));

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

    if (m_expertMode && m_forceFamilyCombo) {
        cfg.forceFamily = familyFromIndex(m_forceFamilyCombo->currentIndex());
    } else {
        cfg.forceFamily = IperfGuiConfig::AddressFamily::Any;
    }
    cfg.family = cfg.forceFamily;
    cfg.probeMaxParallel = 128;
    cfg.udpAutoParallelProbe = true;

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?Server mode: only listen address + port matter.
    //    Traffic type, I/O size, duration, direction are all determined
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

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?Client mode 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜?
    if (m_mixedModeBtn && m_mixedModeBtn->isChecked() && !m_mixRows.isEmpty()) {
        cfg.trafficMode = TrafficMode::Mixed;
        cfg.mixEntries = buildMixEntries();
        if (!cfg.mixEntries.isEmpty()) {
            cfg.trafficType = cfg.mixEntries.constFirst().trafficType;
            cfg.packetSize  = cfg.mixEntries.constFirst().packetSize;
        }
    } else {
        cfg.trafficMode = TrafficMode::Single;
        cfg.mixEntries.clear();
        cfg.trafficType = m_trafficType
            ? m_trafficType->currentData().value<TrafficType>() : TrafficType::Tcp;
        cfg.packetSize  = m_packetSize
            ? m_packetSize->currentData().value<PacketSize>() : PacketSize::B1518;
    }

    cfg.protocol  = (cfg.trafficType == TrafficType::Udp)
        ? IperfGuiConfig::Protocol::Udp : IperfGuiConfig::Protocol::Tcp;
    cfg.probeMaxParallel = 128;
    cfg.udpAutoParallelProbe = (cfg.protocol == IperfGuiConfig::Protocol::Udp);
    // blockSize = application write block (UDP datagram; TCP app write block, not wire frame)
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
    cfg.serverAddress = m_serverAddress ? m_serverAddress->currentText().trimmed() : QString();
    cfg.host          = cfg.serverAddress;

    // Port: always iperf default for TCP/UDP; expert override takes precedence
    cfg.port = (m_expertMode && m_customPortSpin && m_customPortSpin->value() > 0)
               ? m_customPortSpin->value() : 5201;

    // Bind address / source NIC:
    //   1. Expert panel explicit text takes highest priority.
    //   2. Client NIC selector (if non-Auto) overrides when expert panel is empty.
    if (m_expertMode && m_bindAddrEdit && !m_bindAddrEdit->text().trimmed().isEmpty()) {
        cfg.bindAddress = m_bindAddrEdit->text().trimmed();
    } else if (m_clientNicSelector && m_clientNicSelector->currentIndex() > 0) {
        cfg.bindAddress = m_clientNicSelector->currentData().toString();
    }

    cfg.getServerOutput      = true;
    cfg.jsonStream           = true;
    cfg.jsonStreamFullOutput = true;
    cfg.udpCounters64Bit     = true;
    cfg.forceFlush           = true;

    return cfg;
}

QVector<IperfGuiConfig> TestPage::buildMixedStepConfigs(const IperfGuiConfig &baseConfig) const
{
    QVector<IperfGuiConfig> plan;
    const QVector<TrafficMixEntry> entries = buildMixEntries();
    if (entries.isEmpty()) {
        return plan;
    }

    const int totalDuration = baseConfig.duration > 0 ? baseConfig.duration : 300;
    int remaining = totalDuration;

    for (int i = 0; i < entries.size(); ++i) {
        const TrafficMixEntry &entry = entries.at(i);
        IperfGuiConfig step = baseConfig;
        step.trafficMode = TrafficMode::Mixed;
        step.trafficType = entry.trafficType;
        step.packetSize  = entry.packetSize;
        step.protocol    = (entry.trafficType == TrafficType::Udp)
            ? IperfGuiConfig::Protocol::Udp
            : IperfGuiConfig::Protocol::Tcp;
        step.blockSize   = packetSizeToBytes(step.packetSize, baseConfig.customPacketSizeBytes);
        step.duration    = (i == entries.size() - 1)
            ? qMax(1, remaining)
            : qMax(1, static_cast<int>(std::floor(double(totalDuration) * double(entry.ratioPercent) / 100.0)));
        step.parallel    = qMax(1, baseConfig.parallel);
        step.bitrateBps  = (step.protocol == IperfGuiConfig::Protocol::Udp)
            ? (baseConfig.bitrateBps > 0 ? baseConfig.bitrateBps : 100.0e6)
            : 0;
        step.probeSession = false;
        step.suppressHistory = true;
        step.mixEntries = entries;
        plan.push_back(step);

        remaining -= step.duration;
        if (remaining < 1) {
            remaining = 1;
        }
    }

    if (!plan.isEmpty() && totalDuration > 0) {
        const int used = std::accumulate(plan.begin(), plan.end(), 0,
            [](int sum, const IperfGuiConfig &step) { return sum + qMax(1, step.duration); });
        if (used != totalDuration) {
            plan.last().duration = qMax(1, plan.last().duration + (totalDuration - used));
        }
    }

    return plan;
}

QString TestPage::mixedRowSummaryText(const IperfSessionRecord &record)
{
    const QString type = trafficTypeName(record.config.trafficType);
    const QString size = packetSizeName(record.config.packetSize);
    return QStringLiteral("%1 %2 · Peak %3 · Stable %4")
        .arg(type,
             size,
             iperfHumanBitsPerSecond(record.peakBps),
             iperfHumanBitsPerSecond(record.stableBps));
}

QString TestPage::mixedStepStatusText(int stepIndex) const
{
    if (stepIndex < 0 || stepIndex >= m_mixedPlan.size()) {
        return QStringLiteral("Mixed");
    }

    const IperfGuiConfig &cfg = m_mixedPlan.at(stepIndex);
    const int totalSteps = m_mixedPlan.size();
    const int ratio = (stepIndex < m_mixRows.size() && m_mixRows.at(stepIndex).ratioSpin)
        ? m_mixRows.at(stepIndex).ratioSpin->value()
        : 0;
    return QStringLiteral("Mixed %1/%2 · %3 %4 · %5% · %6 s")
        .arg(stepIndex + 1)
        .arg(totalSteps)
        .arg(trafficTypeName(cfg.trafficType))
        .arg(packetSizeName(cfg.packetSize))
        .arg(ratio)
        .arg(cfg.duration);
}

IperfSessionRecord TestPage::buildMixedAggregateRecord() const
{
    IperfSessionRecord record;
    record.startedAt = m_mixedStartedAt.isValid() ? m_mixedStartedAt : QDateTime::currentDateTime();
    record.config = m_baseConfig;
    record.config.trafficMode = TrafficMode::Mixed;
    record.config.mixEntries = buildMixEntries();
    record.config.suppressHistory = false;
    record.config.probeSession = false;
    if (!record.config.mixEntries.isEmpty()) {
        const TrafficMixEntry &first = record.config.mixEntries.constFirst();
        record.config.trafficType = first.trafficType;
        record.config.packetSize = first.packetSize;
        record.config.protocol = (first.trafficType == TrafficType::Udp)
            ? IperfGuiConfig::Protocol::Udp
            : IperfGuiConfig::Protocol::Tcp;
        record.config.blockSize = packetSizeToBytes(first.packetSize, record.config.customPacketSizeBytes);
    }
    record.exitCode = 0;
    record.escapedByLongjmp = false;
    record.runState = IperfRunState::Completed;
    record.runStateDetail = QStringLiteral("mixed bundle");
    record.statusText = iperfRunStateText(record.runState, record.runStateDetail, false);

    QStringList diagnostics;
    QVariantList mixedRows;
    double totalDuration = 0.0;
    double weightedStable = 0.0;
    double weightedLoss = 0.0;
    double lossWeight = 0.0;
    double peakBps = 0.0;
    double offset = 0.0;
    QStringList rawParts;

    for (int i = 0; i < m_mixedStepRecords.size(); ++i) {
        const IperfSessionRecord &step = m_mixedStepRecords.at(i);
        const double stepDuration = qMax(1, step.config.duration);
        totalDuration += stepDuration;
        weightedStable += step.stableBps * stepDuration;
        if (step.lossPercent >= 0.0) {
            weightedLoss += step.lossPercent * stepDuration;
            lossWeight += stepDuration;
        }
        peakBps = qMax(peakBps, step.peakBps);

        QVariantMap row;
        row.insert(QStringLiteral("index"), i + 1);
        row.insert(QStringLiteral("traffic_type"), trafficTypeName(step.config.trafficType));
        row.insert(QStringLiteral("packet_size"), packetSizeName(step.config.packetSize));
        row.insert(QStringLiteral("duration_s"), step.config.duration);
        row.insert(QStringLiteral("peak_bps"), step.peakBps);
        row.insert(QStringLiteral("stable_bps"), step.stableBps);
        row.insert(QStringLiteral("loss_percent"), step.lossPercent);
        row.insert(QStringLiteral("exit_code"), step.exitCode);
        row.insert(QStringLiteral("status_text"), step.statusText);
        mixedRows.push_back(row);

        diagnostics << mixedRowSummaryText(step);

        for (const IperfIntervalSample &sample : step.intervalArchive) {
            IperfIntervalSample adjusted = sample;
            adjusted.startSec += offset;
            adjusted.endSec   += offset;
            record.intervalArchive.push_back(adjusted);
        }
        offset += stepDuration;

        if (!step.rawJson.isEmpty()) {
            rawParts << QStringLiteral("# Step %1\n%2")
                            .arg(i + 1)
                            .arg(step.rawJson);
        }
    }

    if (!m_mixedStepRecords.isEmpty()) {
        record.finalFields = m_mixedStepRecords.constLast().finalFields;
    }

    record.rawJson = rawParts.join(QStringLiteral("\n\n"));

    record.peakBps = peakBps;
    record.stableBps = totalDuration > 0.0 ? (weightedStable / totalDuration) : 0.0;
    record.lossPercent = lossWeight > 0.0 ? (weightedLoss / lossWeight) : -1.0;
    record.diagnosticText = diagnostics.join(QStringLiteral(" | "));

    QVariantMap mixedSummary;
    mixedSummary.insert(QStringLiteral("rows"), mixedRows);
    mixedSummary.insert(QStringLiteral("row_count"), m_mixedStepRecords.size());
    mixedSummary.insert(QStringLiteral("total_duration_s"), totalDuration);
    mixedSummary.insert(QStringLiteral("summary"), record.diagnosticText);
    record.finalFields.insert(QStringLiteral("mixed_bundle"), mixedSummary);
    record.finalFields.insert(QStringLiteral("mixed_rows"), mixedRows);

    return record;
}

void TestPage::startMixedStep()
{
    if (!m_bridge || m_mixedAbortRequested || m_phase != Phase::MixedRunning
        || m_mixedStepIndex < 0 || m_mixedStepIndex >= m_mixedPlan.size()) {
        return;
    }

    const IperfGuiConfig cfg = m_mixedPlan.at(m_mixedStepIndex);
    setStatus(IperfRunState::Sustaining, mixedStepStatusText(m_mixedStepIndex));
    m_bridge->setConfiguration(cfg);
    m_bridge->start();
}

void TestPage::finishMixedBundle(bool aborted)
{
    if (m_phase != Phase::MixedRunning) {
        return;
    }

    if (m_mixedAbortRequested) {
        aborted = true;
    }

    IperfSessionRecord record = buildMixedAggregateRecord();
    const bool stepFailed = (!m_mixedStepRecords.isEmpty()
                             && m_mixedStepRecords.constLast().runState == IperfRunState::Failed);
    if (aborted || stepFailed) {
        record.runState = aborted ? IperfRunState::Stopped : IperfRunState::Failed;
        record.runStateDetail = aborted
            ? QStringLiteral("mixed bundle stopped")
            : QStringLiteral("mixed bundle failed");
        record.statusText = iperfRunStateText(record.runState, record.runStateDetail, false);
        if (record.exitCode == 0) {
            record.exitCode = aborted ? 1 : -1;
        }
    }

    m_lastSession = record;
    applyOverviewFromSession(record);
    m_hasSession = true;
    emit sessionRecorded(record);

    m_phase = Phase::Idle;
    m_mixedPlan.clear();
    m_mixedStepRecords.clear();
    m_mixedStepIndex = -1;
    m_mixedAbortRequested = false;
    m_mixedContinuous = false;

    setControlsEnabled(true);
    m_stopBtn->setEnabled(false);
    m_exportBtn->setEnabled(true);
    setStatus(record.runState, record.runStateDetail, record.escapedByLongjmp);
}

IperfGuiConfig TestPage::buildEffectiveConfigForPreflight() const
{
    return buildConfig();
}

bool TestPage::preflightConfigMatches(const IperfGuiConfig &lhs, const IperfGuiConfig &rhs)
{
    return lhs.mode == rhs.mode
        && lhs.serverAddress == rhs.serverAddress
        && lhs.host == rhs.host
        && lhs.listenAddress == rhs.listenAddress
        && lhs.port == rhs.port
        && lhs.family == rhs.family
        && lhs.forceFamily == rhs.forceFamily
        && lhs.bindAddress == rhs.bindAddress
        && lhs.bindDev == rhs.bindDev;
}

void TestPage::applyPreflightResult(IperfGuiConfig &cfg) const
{
    if (!m_hasLastPreflight) {
        return;
    }
    if (!preflightConfigMatches(cfg, m_lastPreflightConfig)) {
        return;
    }
    cfg.preflightResolvedTargetAddress = m_lastPreflightConfig.preflightResolvedTargetAddress;
    cfg.preflightSourceAddress = m_lastPreflightConfig.preflightSourceAddress;
    cfg.resolvedHostForRun = m_lastPreflightConfig.resolvedHostForRun;
    cfg.preflightStatus = m_lastPreflightConfig.preflightStatus;
    cfg.preflightValid = m_lastPreflightConfig.preflightValid;
    cfg.preflightFamilyMatch = m_lastPreflightConfig.preflightFamilyMatch;
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
    // well-known ports (80/443/53/21) 闂?that would risk hitting live services.
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

    auto *removeBtn = new QPushButton(QStringLiteral("-"), row.container);
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

    m_mixLayout->addWidget(row.container);
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

// ============================================================================
// Recent targets 闂?persistence helpers
// ============================================================================

// Populate the server-address combo from m_recentTargets.
// Starred entries come first (with 闂?prefix), unstarred follow.
// The item's UserData stores the bare IP/hostname so selecting from the list
// sets the edit text to just the address (not the display label).
void TestPage::updateServerAddressCombo()
{
    if (!m_serverAddress) { return; }

    const QString currentText = m_serverAddress->currentText();

    m_serverAddress->blockSignals(true);
    m_serverAddress->clear();

    auto addEntry = [this](const RecentTarget &t) {
        const QString display = t.nickname.isEmpty()
            ? (t.starred ? QStringLiteral("\u2605 %1").arg(t.address)
                         : t.address)
            : (t.starred ? QStringLiteral("\u2605 %1  [%2]").arg(t.nickname, t.address)
                         : QStringLiteral("%1  [%2]").arg(t.nickname, t.address));
        m_serverAddress->addItem(display, t.address);
    };

    for (const auto &t : m_recentTargets) { if (t.starred)  { addEntry(t); } }
    for (const auto &t : m_recentTargets) { if (!t.starred) { addEntry(t); } }

    m_serverAddress->setEditText(currentText);
    m_serverAddress->blockSignals(false);
}

void TestPage::loadRecentTargets()
{
    m_recentTargets.clear();
    QSettings s;
    const int n = s.beginReadArray(QStringLiteral("recentTargets"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        RecentTarget t;
        t.address  = s.value(QStringLiteral("address")).toString();
        t.nickname = s.value(QStringLiteral("nickname")).toString();
        t.starred  = s.value(QStringLiteral("starred"), false).toBool();
        if (!t.address.isEmpty()) { m_recentTargets.push_back(t); }
    }
    s.endArray();
    updateServerAddressCombo();
}

void TestPage::saveRecentTargets()
{
    QSettings s;
    s.beginWriteArray(QStringLiteral("recentTargets"), m_recentTargets.size());
    for (int i = 0; i < m_recentTargets.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("address"),  m_recentTargets[i].address);
        s.setValue(QStringLiteral("nickname"), m_recentTargets[i].nickname);
        s.setValue(QStringLiteral("starred"),  m_recentTargets[i].starred);
    }
    s.endArray();
}

// Called when a test starts 闂?moves this address to the front of the list.
// Starred flag and nickname are preserved if the address already exists.
void TestPage::addRecentTarget(const QString &address)
{
    if (address.isEmpty()) { return; }

    // Check if already present; if so, move to front
    for (int i = 0; i < m_recentTargets.size(); ++i) {
        if (m_recentTargets[i].address == address) {
            const RecentTarget t = m_recentTargets.takeAt(i);
            m_recentTargets.prepend(t);
            saveRecentTargets();
            updateServerAddressCombo();
            return;
        }
    }

    // New entry
    RecentTarget t;
    t.address = address;
    m_recentTargets.prepend(t);

    // Trim: keep all starred entries + up to 20 non-starred (oldest removed first)
    constexpr int kMaxNonStarred = 20;
    int nonStarCount = 0;
    for (int i = m_recentTargets.size() - 1; i >= 0; --i) {
        if (!m_recentTargets[i].starred) {
            if (++nonStarCount > kMaxNonStarred) {
                m_recentTargets.removeAt(i);
            }
        }
    }

    saveRecentTargets();
    updateServerAddressCombo();
}

// Sync the star button checked state & icon to match the stored entry for `address`.
void TestPage::updateStarButton(const QString &address)
{
    if (!m_starBtn) { return; }
    const bool starred = std::any_of(
        m_recentTargets.cbegin(), m_recentTargets.cend(),
        [&address](const RecentTarget &t){ return t.address == address && t.starred; });
    m_starBtn->blockSignals(true);
    m_starBtn->setChecked(starred);
    m_starBtn->setText(starred ? QStringLiteral("\u2605") : QStringLiteral("\u2606"));
    m_starBtn->blockSignals(false);
}

// ---------------------------------------------------------------------------
// Recent-targets slots
// ---------------------------------------------------------------------------

void TestPage::onAddressTextChanged(const QString &text)
{
    // Update the star button to reflect stored state for this address
    updateStarButton(text.trimmed());

    if (m_preflightTimer) {
        m_preflightTimer->stop();
        if (!text.trimmed().isEmpty()) {
            m_preflightTimer->start();
            setPreflightStatus(QStringLiteral("Checking\u2026"), QStringLiteral("#888"));
        } else {
            if (m_preflightLabel) { m_preflightLabel->clear(); }
        }
    }
}

void TestPage::onStarClicked()
{
    if (!m_serverAddress || !m_starBtn) { return; }
    const QString addr = m_serverAddress->currentText().trimmed();
    if (addr.isEmpty()) { return; }

    const bool nowStarred = m_starBtn->isChecked();
    m_starBtn->setText(nowStarred ? QStringLiteral("\u2605") : QStringLiteral("\u2606"));

    // Update or insert the entry
    bool found = false;
    for (auto &t : m_recentTargets) {
        if (t.address == addr) {
            t.starred = nowStarred;
            found = true;
            break;
        }
    }
    if (!found && nowStarred) {
        RecentTarget t;
        t.address = addr;
        t.starred = true;
        m_recentTargets.prepend(t);
    }

    saveRecentTargets();
    updateServerAddressCombo();
}

// ============================================================================
// Pre-flight connectivity check
// ============================================================================

void TestPage::setPreflightStatus(const QString &text, const QString &color)
{
    if (!m_preflightLabel) { return; }
    m_preflightLabel->setText(text);
    m_preflightLabel->setStyleSheet(
        QStringLiteral("color:%1; font-size:11px;").arg(color));
}

void TestPage::onPreflightTimerFired()
{
    if (!m_serverAddress || !m_preflightLabel) { return; }
    const IperfGuiConfig cfg = buildEffectiveConfigForPreflight();
    if (cfg.mode != IperfGuiConfig::Mode::Client) {
        m_preflightLabel->clear();
        return;
    }
    const QString host = cfg.host.trimmed();
    if (host.isEmpty()) { m_preflightLabel->clear(); return; }

    // Cancel any pending DNS lookup
    if (m_dnsLookupId >= 0) {
        QHostInfo::abortHostLookup(m_dnsLookupId);
        m_dnsLookupId = -1;
    }

    m_lastPreflightConfig = cfg;
    m_hasLastPreflight = false;
    switch (cfg.family) {
    case IperfGuiConfig::AddressFamily::IPv4:
        setPreflightStatus(QStringLiteral("Resolving IPv4\u2026"), QStringLiteral("#888"));
        break;
    case IperfGuiConfig::AddressFamily::IPv6:
        setPreflightStatus(QStringLiteral("Resolving IPv6\u2026"), QStringLiteral("#888"));
        break;
    case IperfGuiConfig::AddressFamily::Any:
    default:
        setPreflightStatus(QStringLiteral("Resolving address\u2026"), QStringLiteral("#888"));
        break;
    }
    startPreflightCheck(host);
}

void TestPage::startPreflightCheck(const QString &host)
{
    m_dnsLookupId = QHostInfo::lookupHost(host, this,
        [this](const QHostInfo &info) { onDnsLookupDone(info); });
}

void TestPage::onDnsLookupDone(const QHostInfo &info)
{
    // Ignore stale results (user already typed something new)
    if (info.lookupId() != m_dnsLookupId) { return; }
    m_dnsLookupId = -1;

    IperfGuiConfig cfg = m_lastPreflightConfig;
    if (cfg.mode != IperfGuiConfig::Mode::Client) {
        return;
    }

    if (info.error() != QHostInfo::NoError) {
        cfg.preflightStatus = QStringLiteral("DNS failed");
        cfg.preflightValid = false;
        m_lastPreflightConfig = cfg;
        m_hasLastPreflight = false;
        setPreflightStatus(
            QStringLiteral("\u2717 DNS failed: %1").arg(info.errorString()),
            QStringLiteral("#cc0000"));
        return;
    }

    const auto chosen = chooseAddressForFamily(info.addresses(), cfg.family);
    if (!chosen.has_value()) {
        cfg.preflightStatus = QStringLiteral("No usable address found");
        cfg.preflightValid = false;
        m_lastPreflightConfig = cfg;
        m_hasLastPreflight = false;
        switch (cfg.family) {
        case IperfGuiConfig::AddressFamily::IPv4:
            setPreflightStatus(QStringLiteral("\u2717 No IPv4 address found"), QStringLiteral("#cc0000"));
            break;
        case IperfGuiConfig::AddressFamily::IPv6:
            setPreflightStatus(QStringLiteral("\u2717 No IPv6 address found"), QStringLiteral("#cc0000"));
            break;
        case IperfGuiConfig::AddressFamily::Any:
        default:
            setPreflightStatus(QStringLiteral("\u2717 No usable IPv4/IPv6 address found"), QStringLiteral("#cc0000"));
            break;
        }
        return;
    }

    const QHostAddress targetAddr = chosen.value();
    QHostAddress sourceAddr;
    const bool hasSource = !cfg.bindAddress.trimmed().isEmpty();
    if (hasSource) {
        sourceAddr = QHostAddress(cfg.bindAddress.trimmed());
        if (sourceAddr.isNull()) {
            cfg.preflightStatus = QStringLiteral("Invalid source address");
            cfg.preflightValid = false;
            m_lastPreflightConfig = cfg;
            m_hasLastPreflight = false;
            setPreflightStatus(QStringLiteral("\u2717 Selected source NIC address is invalid"),
                               QStringLiteral("#cc0000"));
            return;
        }
        if (sourceAddr.protocol() != targetAddr.protocol()) {
            cfg.preflightStatus = QStringLiteral("Source / target family mismatch");
            cfg.preflightValid = false;
            cfg.preflightFamilyMatch = false;
            m_lastPreflightConfig = cfg;
            m_hasLastPreflight = false;
            setPreflightStatus(
                QStringLiteral("\u2717 Source NIC family does not match target family"),
                QStringLiteral("#cc0000"));
            return;
        }
    }

    cfg.preflightResolvedTargetAddress = targetAddr.toString();
    cfg.preflightSourceAddress = sourceAddr.isNull() ? QString() : sourceAddr.toString();
    cfg.resolvedHostForRun = targetAddr.toString();
    cfg.preflightFamilyMatch = true;
    cfg.preflightValid = true;
    cfg.preflightStatus = QStringLiteral("Ready");
    m_lastPreflightConfig = cfg;
    m_hasLastPreflight = false;

    const int port = cfg.port;
    if (hasSource) {
        setPreflightStatus(
            QStringLiteral("Binding source %1\u2026").arg(sourceAddr.toString()),
            QStringLiteral("#888"));
    }
    setPreflightStatus(
        hasSource
            ? QStringLiteral("Connecting %1 \u2192 %2:%3\u2026")
                  .arg(sourceAddr.toString(), targetAddr.toString())
                  .arg(port)
            : QStringLiteral("Connecting %1:%2\u2026")
                  .arg(targetAddr.toString()).arg(port),
        QStringLiteral("#888"));

    // TCP port reachability check with a 3-second timeout.
    auto *sock = new QTcpSocket(this);
    auto *tmr  = new QTimer(this);
    tmr->setSingleShot(true);
    tmr->setInterval(3000);

    // Shared "done" flag prevents both the error handler and the timeout from
    // firing UI updates or double-deleting the socket.
    auto done = QSharedPointer<bool>::create(false);

    connect(sock, &QTcpSocket::connected, this,
            [this, sock, tmr, done, cfg]() {
        if (*done) { return; }
        *done = true;
        tmr->stop();
        sock->abort();
        sock->deleteLater();
        tmr->deleteLater();
        m_lastPreflightConfig = cfg;
        m_lastPreflightConfig.preflightValid = true;
        m_lastPreflightConfig.preflightStatus = QStringLiteral("Ready");
        m_hasLastPreflight = true;
        setPreflightStatus(QStringLiteral("\u2713 Ready"), QStringLiteral("#007700"));
    });

    connect(sock, &QAbstractSocket::errorOccurred, this,
            [this, sock, tmr, done, cfg](QAbstractSocket::SocketError) mutable {
        if (*done) { return; }
        *done = true;
        tmr->stop();
        sock->deleteLater();
        tmr->deleteLater();
        cfg.preflightValid = false;
        cfg.preflightStatus = sock->errorString();
        m_lastPreflightConfig = cfg;
        m_hasLastPreflight = false;
        setPreflightStatus(
            QStringLiteral("\u26a0 Port unreachable \u2014 is iperf server running?"),
            QStringLiteral("#cc7700"));
    });

    connect(tmr, &QTimer::timeout, this,
            [this, sock, done, cfg]() mutable {
        if (*done) { return; }
        *done = true;
        sock->abort();
        sock->deleteLater();
        if (QObject *s = sender()) { s->deleteLater(); }
        cfg.preflightValid = false;
        cfg.preflightStatus = QStringLiteral("Connection timed out");
        m_lastPreflightConfig = cfg;
        m_hasLastPreflight = false;
        setPreflightStatus(
            QStringLiteral("\u26a0 Connection timed out"),
            QStringLiteral("#cc7700"));
    });

    if (hasSource) {
        if (!sock->bind(sourceAddr, 0)) {
            *done = true;
            cfg.preflightValid = false;
            cfg.preflightStatus = QStringLiteral("Source bind failed");
            m_lastPreflightConfig = cfg;
            m_hasLastPreflight = false;
            sock->deleteLater();
            tmr->deleteLater();
            setPreflightStatus(QStringLiteral("\u2717 Unable to bind to selected source NIC"),
                               QStringLiteral("#cc0000"));
            return;
        }
    }

    sock->connectToHost(targetAddr, static_cast<quint16>(port));
    tmr->start();
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

    IperfIntervalSample sample;
    sample.startSec = sum.value(QStringLiteral("start")).toDouble();
    sample.endSec = sum.value(QStringLiteral("end")).toDouble();
    sample.throughputBps = sum.value(QStringLiteral("bits_per_second")).toDouble();
    if (sum.contains(QStringLiteral("retransmits"))) {
        sample.retransmits = sum.value(QStringLiteral("retransmits")).toInt();
    } else if (sum.contains(QStringLiteral("lost_packets"))) {
        sample.retransmits = sum.value(QStringLiteral("lost_packets")).toInt();
        sample.lossPercent = sum.value(QStringLiteral("lost_percent")).toDouble();
    }
    if (sum.contains(QStringLiteral("jitter_ms"))) {
        sample.jitterMs = sum.value(QStringLiteral("jitter_ms")).toDouble();
    }
    const QString dk = event.fields.value(QStringLiteral("summary_key")).toString();
    // Map iperf3 summary_key names to readable direction arrows (U+2191 闂?/ U+2193 闂?/ U+2195 闂?.
    // Avoid raw UTF-8 byte sequences inside QStringLiteral 闂?use \uXXXX escapes instead.
    QString dirText;
    if (m_baseConfig.bidirectional && (dk == QLatin1String("sum") || dk.contains(QLatin1String("bidir")))) {
        dirText = QStringLiteral("\u2194");
    } else if (dk.isEmpty() || dk == QLatin1String("sum")) {
        dirText = QStringLiteral("\u2192");          // 闂?(unknown / single-dir)
    } else if (dk.contains(QLatin1String("reverse")) || dk == QLatin1String("sum_received")) {
        dirText = QStringLiteral("\u2193");          // 闂?(downlink / reverse)
    } else if (dk == QLatin1String("sum_sent")) {
        dirText = QStringLiteral("\u2191");          // 闂?(uplink / forward)
    } else {
        dirText = dk;                                // raw key (future iperf versions)
    }
    sample.direction = dirText;
    sample.summaryKey = dk;

    if (m_intervalModel) {
        m_intervalModel->appendSample(sample);
        if (m_intervalTable) {
            m_intervalTable->scrollToBottom();
        }
    }
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
                setMetricLabel(m_ovPeak, m_baseConfig.bidirectional
                    ? QStringLiteral("Peak (Bidirectional)")
                    : QStringLiteral("Peak Throughput"),
                    iperfHumanBitsPerSecond(m_runningPeakBps));
            }
            // Show the current interval throughput in the Stable card during the test
            setMetricLabel(m_ovStable, m_baseConfig.bidirectional
                ? QStringLiteral("Current (Bidirectional)")
                : QStringLiteral("Current"),
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
    setMetricLabel(m_ovPeak,   record.config.bidirectional
                   ? QStringLiteral("Peak (Bidirectional)")
                   : QStringLiteral("Peak Throughput"),
                   iperfHumanBitsPerSecond(record.peakBps));
    setMetricLabel(m_ovStable, record.config.bidirectional
                   ? QStringLiteral("Stable (Bidirectional)")
                   : QStringLiteral("Stable Throughput"),
                   iperfHumanBitsPerSecond(record.stableBps));
    setMetricLabel(m_ovLoss,   QStringLiteral("Loss"),
                   record.lossPercent > 0.0
                   ? iperfHumanPercent(record.lossPercent)
        : QStringLiteral("--"));
}

// ---------------------------------------------------------------------------
void TestPage::setStatus(const QString &text)
{
    if (!m_statusLabel) {
        return;
    }
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(iperfRunStateBadgeStyle(IperfRunState::Idle));
}

void TestPage::setStatus(IperfRunState state, const QString &detail, bool legacyLongjmp)
{
    if (!m_statusLabel) {
        return;
    }
    m_statusLabel->setText(iperfRunStateText(state, detail, legacyLongjmp));
    m_statusLabel->setStyleSheet(iperfRunStateBadgeStyle(state, legacyLongjmp));
}

// ---------------------------------------------------------------------------
void TestPage::setControlsEnabled(bool enabled)
{
    // When locking controls (test starting), cancel any in-flight preflight check
    // and clear its status so stale results don't show through the whole test.
    if (!enabled) {
        if (m_preflightTimer) { m_preflightTimer->stop(); }
        if (m_preflightLabel) { m_preflightLabel->clear(); }
    }

    m_startBtn->setEnabled(enabled);
    m_clientBtn->setEnabled(enabled);
    m_serverBtn->setEnabled(enabled);
    m_singleModeBtn->setEnabled(enabled);
    m_mixedModeBtn->setEnabled(enabled);
    if (m_serverAddress)     { m_serverAddress->setEnabled(enabled); }
    if (m_starBtn)           { m_starBtn->setEnabled(enabled); }
    if (m_clientNicSelector) { m_clientNicSelector->setEnabled(enabled); }
    if (m_nicSelector)       { m_nicSelector->setEnabled(enabled); }
    if (m_trafficType)       { m_trafficType->setEnabled(enabled); }
    if (m_packetSize)        { m_packetSize->setEnabled(enabled); }
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
QString TestPage::buildCsvContent(const ExportOptions &options) const
{
    Q_UNUSED(options);
    QString csv;
    QTextStream ts(&csv);

    ts << "Start(s),End(s),Throughput(bps),Jitter(ms),Loss(%),Retransmits,Direction,SummaryKey\n";
    for (const auto &sample : m_lastSession.intervalArchive) {
        const QString jitter = sample.jitterMs >= 0.0 ? QString::number(sample.jitterMs, 'f', 3) : QString();
        const QString loss = sample.lossPercent >= 0.0 ? QString::number(sample.lossPercent, 'f', 2) : QString();
        const QString retrans = sample.retransmits >= 0 ? QString::number(sample.retransmits) : QString();
        ts << sample.startSec << ','
           << sample.endSec << ','
           << sample.throughputBps << ','
           << csvEscapeCell(jitter) << ','
           << csvEscapeCell(loss) << ','
           << csvEscapeCell(retrans) << ','
           << csvEscapeCell(sample.direction) << ','
           << csvEscapeCell(sample.summaryKey) << '\n';
    }
    return csv;
}

QString TestPage::buildReportMarkdown(const IperfSessionRecord &record, const ExportOptions &options) const
{
    return buildSessionReportMarkdown(record, options);
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
    m_exportReport = new QPushButton(QStringLiteral("Export Report"), this);
    m_clearBtn   = new QPushButton(QStringLiteral("Clear All"),   this);
    m_exportJson->setEnabled(false);
    m_exportCsv->setEnabled(false);
    m_exportReport->setEnabled(false);
    m_clearBtn->setEnabled(false);
    bar->addWidget(m_exportJson);
    bar->addWidget(m_exportCsv);
    bar->addWidget(m_exportReport);
    bar->addStretch();
    bar->addWidget(m_clearBtn);
    root->addLayout(bar);

    connect(m_list, &QListWidget::currentRowChanged,
            this, &HistoryPage::onSelectionChanged);
    connect(m_exportJson, &QPushButton::clicked, this, &HistoryPage::onExportJson);
    connect(m_exportCsv,  &QPushButton::clicked, this, &HistoryPage::onExportCsv);
    connect(m_exportReport, &QPushButton::clicked, this, &HistoryPage::onExportReport);
    connect(m_clearBtn,   &QPushButton::clicked, this, &HistoryPage::onClearAll);
}

void HistoryPage::bindBridge(IperfCoreBridge *bridge) { m_bridge = bridge; }

void HistoryPage::appendSession(const IperfSessionRecord &record)
{
    if (record.config.probeSession || record.config.suppressHistory) {
        return;
    }
    // Enforce the retention cap from Settings 闂?Result Retention.
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
        m_exportReport->setEnabled(false);
        return;
    }
    m_detail->setPlainText(buildDetailText(m_records.at(idx)));
    m_exportJson->setEnabled(true);
    m_exportCsv->setEnabled(true);
    m_exportReport->setEnabled(true);
}

void HistoryPage::onExportJson()
{
    const int idx = m_list->currentRow();
    if (idx < 0 || idx >= m_records.size()) { return; }
    const auto &rec = m_records.at(idx);
    const std::optional<ExportOptions> options = chooseExportOptions(this, QStringLiteral("JSON"));
    if (!options.has_value()) { return; }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export JSON"),
        QStringLiteral("%1/iperf_%2_%3.json").arg(
            QDir::homePath(),
            rec.startedAt.toString(QStringLiteral("yyyyMMdd_HHmmss")),
            options->safeForSharing ? QStringLiteral("share") : QStringLiteral("internal")),
        QStringLiteral("JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) { return; }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), f.errorString());
        return;
    }
    const QByteArray payload = QJsonDocument(iperfSessionRecordToJson(rec, *options)).toJson(QJsonDocument::Indented);
    if (f.write(payload) != payload.size()) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), f.errorString());
    }
}

void HistoryPage::onExportCsv()
{
    const std::optional<ExportOptions> options = chooseExportOptions(this, QStringLiteral("CSV"));
    if (!options.has_value()) { return; }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export CSV"),
        QStringLiteral("%1/iperf_history_%2.csv").arg(
            QDir::homePath(),
            options->safeForSharing ? QStringLiteral("share") : QStringLiteral("internal")),
        QStringLiteral("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) { return; }
    QString err;
    if (!writeTextFile(path, buildCsvContent(*options), &err)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), err);
    }
}

void HistoryPage::onExportReport()
{
    const int idx = m_list->currentRow();
    if (idx < 0 || idx >= m_records.size()) { return; }
    const auto &rec = m_records.at(idx);
    const std::optional<ExportOptions> options = chooseExportOptions(this, QStringLiteral("Report"));
    if (!options.has_value()) { return; }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Report"),
        QStringLiteral("%1/iperf_report_%2_%3.md").arg(
            QDir::homePath(),
            rec.startedAt.toString(QStringLiteral("yyyyMMdd_HHmmss")),
            options->safeForSharing ? QStringLiteral("share") : QStringLiteral("internal")),
        QStringLiteral("Markdown files (*.md);;All files (*)"));
    if (path.isEmpty()) { return; }
    QString err;
    if (!writeTextFile(path, buildReportMarkdown(rec, *options), &err)) {
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
    m_exportReport->setEnabled(false);
    m_clearBtn->setEnabled(false);
    if (m_bridge) { m_bridge->clearHistory(); }
}

QString HistoryPage::buildSessionSummaryLine(const IperfSessionRecord &record) const
{
    const bool isSrv = (record.config.mode == IperfGuiConfig::Mode::Server);
    const bool isMixed = (record.config.trafficMode == TrafficMode::Mixed
                          && !record.config.mixEntries.isEmpty());
    const QString proto = (record.config.protocol == IperfGuiConfig::Protocol::Udp)
        ? QStringLiteral("UDP") : QStringLiteral("TCP");
    const QString trafficLabel = isMixed
        ? QStringLiteral("Mixed")
        : proto;
    const QString ep = isSrv
        ? (record.config.listenAddress.isEmpty() ? QStringLiteral("0.0.0.0") : record.config.listenAddress)
        : (!record.config.resolvedHostForRun.isEmpty()
               ? record.config.resolvedHostForRun
               : record.config.serverAddress);
    const QString state = iperfRunStateText(record.runState, record.runStateDetail, record.escapedByLongjmp);
    if (record.config.bidirectional) {
        QString text = QStringLiteral("[%1]  %2  %3  Bidir  %4  Peak %5  %6")
            .arg(record.startedAt.toString(QStringLiteral("MM-dd HH:mm")),
                 isSrv ? QStringLiteral("SRV") : QStringLiteral("CLT"),
                 trafficLabel, ep,
                 iperfHumanBitsPerSecond(record.peakBps),
                 state);
        return text;
    }
    QString text = QStringLiteral("[%1]  %2  %3  %4  Peak %5  %6")
        .arg(record.startedAt.toString(QStringLiteral("MM-dd HH:mm")),
             isSrv ? QStringLiteral("SRV") : QStringLiteral("CLT"),
             trafficLabel, ep,
             iperfHumanBitsPerSecond(record.peakBps),
             state);
    return text;
}

QString HistoryPage::buildDetailText(const IperfSessionRecord &record) const
{
    const bool isSrv = (record.config.mode == IperfGuiConfig::Mode::Server);
    const bool isMixed = (record.config.trafficMode == TrafficMode::Mixed
                          && !record.config.mixEntries.isEmpty());
    const QString epHost = isSrv
        ? (record.config.listenAddress.isEmpty()
               ? QStringLiteral("0.0.0.0")
               : record.config.listenAddress)
        : (!record.config.resolvedHostForRun.isEmpty()
               ? record.config.resolvedHostForRun
               : record.config.host);
    const QString state = iperfRunStateText(record.runState, record.runStateDetail, record.escapedByLongjmp);

    QString out;
    QTextStream ts(&out);
    ts << "Time:     " << record.startedAt.toString(Qt::ISODate) << "\n"
       << "State:    " << state << "\n"
       << "Exit:     " << record.exitCode << "\n"
       << "Status:   " << record.statusText << "\n"
       << "Diagnostic: " << (record.diagnosticText.isEmpty() ? QStringLiteral("-") : record.diagnosticText) << "\n"
       << "Mode:     " << iperfModeName(record.config.mode) << "\n"
       << "Traffic:  " << trafficModeName(record.config.trafficMode)
       << " / " << (isMixed ? QStringLiteral("Mixed") : trafficTypeName(record.config.trafficType)) << "\n"
       << "Block / Datagram Size: "
       << (isMixed ? QStringLiteral("Mixed") : packetSizeName(record.config.packetSize))
       << (isMixed ? QStringLiteral("") : QStringLiteral(" (%1 bytes)").arg(record.config.blockSize)) << "\n"
       << "Family:   " << iperfFamilyName(record.config.family) << "\n"
       << "Force:    " << iperfFamilyName(record.config.forceFamily) << "\n"
       << "Direction:" << directionName(record.config.direction) << "\n"
       << "Endpoint: " << epHost << ":" << record.config.port << "\n"
       << "Resolved Host For Run: "
       << (record.config.resolvedHostForRun.isEmpty() ? QStringLiteral("-") : record.config.resolvedHostForRun) << "\n"
       << "Listen:   " << (record.config.listenAddress.isEmpty() ? QStringLiteral("0.0.0.0") : record.config.listenAddress) << "\n"
       << "Bind:     " << (record.config.bindAddress.isEmpty() ? QStringLiteral("-") : record.config.bindAddress) << "\n"
       << "Preflight Status: " << (record.config.preflightStatus.isEmpty() ? QStringLiteral("-") : record.config.preflightStatus) << "\n"
       << "Preflight Target: " << (record.config.preflightResolvedTargetAddress.isEmpty() ? QStringLiteral("-") : record.config.preflightResolvedTargetAddress) << "\n"
       << "Preflight Source: " << (record.config.preflightSourceAddress.isEmpty() ? QStringLiteral("-") : record.config.preflightSourceAddress) << "\n"
       << "Preflight Valid: " << (record.config.preflightValid ? QStringLiteral("yes") : QStringLiteral("no")) << "\n"
       << "Preflight Family Match: " << (record.config.preflightFamilyMatch ? QStringLiteral("yes") : QStringLiteral("no")) << "\n"
       << "BindDev:  " << (record.config.bindDev.isEmpty() ? QStringLiteral("-") : record.config.bindDev) << "\n"
       << "Duration: " << durationPresetName(record.config.durationPreset)
       << " (" << record.config.duration << " s)\n"
       << "Parallel: " << record.config.parallel << "\n"
       << "Bitrate:  " << iperfHumanBitsPerSecond(static_cast<double>(record.config.bitrateBps)) << "\n"
       << "Rows:     " << (isMixed ? QString::number(record.config.mixEntries.size()) : QStringLiteral("-")) << "\n"
       << "Scope:    " << (record.config.bidirectional ? QStringLiteral("Bidirectional") : QStringLiteral("Single-direction")) << "\n"
       << "Compatibility escape:   " << (record.escapedByLongjmp ? QStringLiteral("yes") : QStringLiteral("no")) << "\n"
       << "Intervals:" << record.intervalArchive.size() << "\n"
       << "\n"
       << "Peak:     " << iperfHumanBitsPerSecond(record.peakBps) << "\n"
       << "Stable:   " << iperfHumanBitsPerSecond(record.stableBps) << "\n";
    if (record.lossPercent > 0.0) {
        ts << "Loss:     " << iperfHumanPercent(record.lossPercent) << "\n";
    }
    if (isMixed) {
        ts << "\nMixed Snapshot:\n" << trafficMixEntriesText(record.config.mixEntries) << "\n";
        if (record.finalFields.contains(QStringLiteral("mixed_bundle"))) {
            const QVariantMap mixed = record.finalFields.value(QStringLiteral("mixed_bundle")).toMap();
            if (!mixed.isEmpty()) {
                ts << "\nMixed Bundle:\n"
                   << "  Rows: " << mixed.value(QStringLiteral("row_count")).toInt() << "\n"
                   << "  Duration(s): " << mixed.value(QStringLiteral("total_duration_s")).toDouble() << "\n";
                const QString summary = mixed.value(QStringLiteral("summary")).toString();
                if (!summary.isEmpty()) {
                    ts << "  Summary: " << summary << "\n";
                }
            }
        }
    }
    ts << "\n--- Raw JSON ---\n" << record.rawJson;
    return out;
}

QString HistoryPage::buildCsvContent(const ExportOptions &options) const
{
    QString csv;
    QTextStream ts(&csv);
    ts << "Time,Mode,Protocol,TrafficMode,TrafficType,PacketSize,BlockSize,Direction,Endpoint,ResolvedHostForRun,ListenAddress,BindAddress,BindDevice,Family,ForceFamily,Duration(s),Parallel,Peak(bps),Stable(bps),Loss(%),ExitCode,EscapedByLongjmp,ProbeSession,PreflightStatus,PreflightValid,PreflightFamilyMatch,PreflightTarget,PreflightSource,MixedSnapshot,Status\n";
    for (const auto &rec : m_records) {
        const bool isSrv = (rec.config.mode == IperfGuiConfig::Mode::Server);
        const bool isMixed = (rec.config.trafficMode == TrafficMode::Mixed
                              && !rec.config.mixEntries.isEmpty());
        const QString epHost = exportEndpointValue(rec.config, options);
        const QString resolvedHost = exportAddressValue(rec.config.resolvedHostForRun, options);
        const QString endpointCell = epHost.isEmpty()
            ? QStringLiteral("-")
            : QStringLiteral("%1:%2").arg(epHost).arg(rec.config.port);
        const QString mixedSnapshot = rec.config.trafficMode == TrafficMode::Mixed && !rec.config.mixEntries.isEmpty()
            ? trafficMixEntriesText(rec.config.mixEntries).replace(QLatin1Char('\n'), QStringLiteral(" | "))
            : QString();
        const QStringList cells = {
            rec.startedAt.toString(Qt::ISODate),
            isSrv ? QStringLiteral("Server") : QStringLiteral("Client"),
            rec.config.protocol == IperfGuiConfig::Protocol::Udp ? QStringLiteral("UDP") : QStringLiteral("TCP"),
            trafficModeName(rec.config.trafficMode),
            isMixed ? QStringLiteral("Mixed") : trafficTypeName(rec.config.trafficType),
            isMixed ? QStringLiteral("Mixed") : packetSizeName(rec.config.packetSize),
            isMixed ? QStringLiteral("0") : QString::number(rec.config.blockSize),
            directionName(rec.config.direction),
            endpointCell,
            resolvedHost,
            exportAddressValue(rec.config.listenAddress, options),
            exportAddressValue(rec.config.bindAddress, options),
            exportAddressValue(rec.config.bindDev, options),
            iperfFamilyName(rec.config.family),
            iperfFamilyName(rec.config.forceFamily),
            QString::number(rec.config.duration),
            QString::number(rec.config.parallel),
            QString::number(rec.peakBps, 'f', 0),
            QString::number(rec.stableBps, 'f', 0),
            QString::number(rec.lossPercent, 'f', 2),
            QString::number(rec.exitCode),
            rec.escapedByLongjmp ? QStringLiteral("yes") : QStringLiteral("no"),
            rec.config.probeSession ? QStringLiteral("yes") : QStringLiteral("no"),
            rec.config.preflightStatus,
            rec.config.preflightValid ? QStringLiteral("yes") : QStringLiteral("no"),
            rec.config.preflightFamilyMatch ? QStringLiteral("yes") : QStringLiteral("no"),
            exportAddressValue(rec.config.preflightResolvedTargetAddress, options),
            exportAddressValue(rec.config.preflightSourceAddress, options),
            mixedSnapshot,
            rec.statusText,
        };
        QStringList escaped;
        escaped.reserve(cells.size());
        for (QString cell : cells) {
            escaped << csvEscapeCell(cell);
        }
        ts << escaped.join(QLatin1Char(',')) << QLatin1Char('\n');
    }
    return csv;
}

QString HistoryPage::buildReportMarkdown(const IperfSessionRecord &record, const ExportOptions &options) const
{
    return buildSessionReportMarkdown(record, options);
}

// ============================================================================
// SettingsPage 闂?helpers
// ============================================================================

// Apply the selected theme (0=System, 1=Light, 2=Dark) to the whole application.
// Called both at startup (loadSettings) and when the user clicks Apply.
static void applyTheme(int themeIndex)
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app) { return; }

    switch (themeIndex) {

    case 1: { // Light 闂?Fusion style, light palette, tell platform we want light
        app->setStyle(QStringLiteral("Fusion"));
        app->setPalette(app->style()->standardPalette());
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
        break;
    }

    case 2: { // Dark 闂?Fusion style + hand-crafted dark palette
        app->setStyle(QStringLiteral("Fusion"));
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

        QPalette dark;
        const QColor winBg(0x35, 0x35, 0x35);
        const QColor editBg(0x23, 0x23, 0x23);
        const QColor btnBg (0x45, 0x45, 0x45);
        const QColor txt   (0xee, 0xee, 0xee);
        const QColor dim   (0x88, 0x88, 0x88);
        const QColor hi    (0x00, 0x66, 0xcc);

        dark.setColor(QPalette::Window,          winBg);
        dark.setColor(QPalette::WindowText,      txt);
        dark.setColor(QPalette::Base,            editBg);
        dark.setColor(QPalette::AlternateBase,   QColor(0x2d, 0x2d, 0x2d));
        dark.setColor(QPalette::ToolTipBase,     editBg);
        dark.setColor(QPalette::ToolTipText,     txt);
        dark.setColor(QPalette::Text,            txt);
        dark.setColor(QPalette::PlaceholderText, dim);
        dark.setColor(QPalette::Button,          btnBg);
        dark.setColor(QPalette::ButtonText,      txt);
        dark.setColor(QPalette::BrightText,      Qt::red);
        dark.setColor(QPalette::Link,            QColor(0x42, 0x9c, 0xff));
        dark.setColor(QPalette::Highlight,       hi);
        dark.setColor(QPalette::HighlightedText, Qt::white);
        dark.setColor(QPalette::Mid,             QColor(0x60, 0x60, 0x60));
        dark.setColor(QPalette::Midlight,        QColor(0x80, 0x80, 0x80));
        dark.setColor(QPalette::Shadow,          QColor(0x14, 0x14, 0x14));
        dark.setColor(QPalette::Dark,            editBg);
        // Disabled role
        dark.setColor(QPalette::Disabled, QPalette::WindowText,  dim);
        dark.setColor(QPalette::Disabled, QPalette::Text,        dim);
        dark.setColor(QPalette::Disabled, QPalette::ButtonText,  dim);
        dark.setColor(QPalette::Disabled, QPalette::Highlight,   QColor(0x44, 0x44, 0x44));
        dark.setColor(QPalette::Disabled, QPalette::HighlightedText, dim);

        app->setPalette(dark);
        break;
    }

    case 0: // System default 闂?restore native Windows style
    default:
        app->setStyle(QStringLiteral("windowsvista"));
        app->setPalette(app->style()->standardPalette());
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Unknown);
        break;
    }
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

    m_expertPreview = new QFrame(this);
    m_expertPreview->setFrameShape(QFrame::StyledPanel);
    m_expertPreview->setFrameShadow(QFrame::Plain);
    m_expertPreview->setStyleSheet(
        QStringLiteral("QFrame{background:#f8fbff;border:1px solid #c7d7ea;border-radius:6px;}"));
    auto *previewLayout = new QVBoxLayout(m_expertPreview);
    previewLayout->setContentsMargins(12, 10, 12, 10);
    previewLayout->setSpacing(4);
    auto *previewTitle = new QLabel(QStringLiteral("<b>Expert controls</b>"), m_expertPreview);
    previewTitle->setTextFormat(Qt::RichText);
    m_expertPreviewLabel = new QLabel(m_expertPreview);
    m_expertPreviewLabel->setWordWrap(true);
    m_expertPreviewLabel->setText(
        QStringLiteral("Enable this to reveal advanced Test page controls such as "
                       "custom port, bind address, and force IPv4/IPv6."));
    previewLayout->addWidget(previewTitle);
    previewLayout->addWidget(m_expertPreviewLabel);
    m_expertPreview->setVisible(false);
    root->addWidget(m_expertPreview);

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

    // 闂佸啿鍘滈崑鎾绘煃閸忓浜?About section 闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕闂佸啿鍘滈崑鎾绘煃閸忓浜鹃梺鍐插帨閸嬫捇鏌嶉崗澶婁壕
    m_buildInfo   = new QLabel(this);
    m_runtimeInfo = new QLabel(this);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#0a0;"));
    root->addWidget(m_buildInfo);
    root->addWidget(m_runtimeInfo);
    root->addWidget(m_statusLabel);

    m_buildInfo->setText(
        QStringLiteral("Build: IperfWin v%1  (libiperf 3.21+, Qt %2)")
        .arg(QString::fromLatin1(kIperfWinVersion),
             QString::fromLatin1(QT_VERSION_STR)));
    m_runtimeInfo->setText(
        QStringLiteral("Platform: %1 / %2")
        .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()));

    auto *aboutBtn = new QPushButton(QStringLiteral("About IperfWin\u2026"), this);
    aboutBtn->setFixedWidth(160);
    connect(aboutBtn, &QPushButton::clicked, this, [this]() {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(QStringLiteral("About IperfWin"));
        box.setTextFormat(Qt::RichText);
        box.setText(
            QStringLiteral("<b>IperfWin v%1</b><br>"
                           "Network throughput test tool powered by libiperf.<br><br>"
                           "Runtime: Qt %2<br>"
                           "Platform: %3 (%4)<br><br>"
                           "Select <b>Test</b> to start a measurement.<br>"
                           "Use <b>Settings > Show Expert Controls</b> for advanced options.")
            .arg(QString::fromLatin1(kIperfWinVersion),
                 QString::fromLatin1(QT_VERSION_STR),
                 QSysInfo::prettyProductName(),
                 QSysInfo::currentCpuArchitecture()));
        box.exec();
    });
    root->addWidget(aboutBtn);

    connect(applyBtn,    &QPushButton::clicked, this, &SettingsPage::onApply);
    connect(resetBtn,    &QPushButton::clicked, this, &SettingsPage::onReset);
    connect(m_browseBtn, &QPushButton::clicked, this, &SettingsPage::onBrowseExportFolder);
    connect(m_expertCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_expertPreview) {
            m_expertPreview->setVisible(enabled);
            m_expertPreview->updateGeometry();
        }
        if (m_expertPreviewLabel) {
            m_expertPreviewLabel->setText(enabled
                ? QStringLiteral("Expert controls are now visible on the Test page.")
                : QStringLiteral("Turn this on to reveal advanced Test page controls."));
        }
    });
    connect(m_expertCheck, &QCheckBox::toggled, this, &SettingsPage::expertModeChanged);
}

void SettingsPage::bindBridge(IperfCoreBridge *bridge) { m_bridge = bridge; }

void SettingsPage::loadSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("preferences"));
    const int themeIdx = s.value(QStringLiteral("theme"), 0).toInt();
    m_theme->setCurrentIndex(themeIdx);
    m_retentionSpin->setValue(s.value(QStringLiteral("retention"), 200).toInt());
    m_exportFolder->setText(s.value(QStringLiteral("exportFolder")).toString());
    m_expertCheck->setChecked(s.value(QStringLiteral("expertMode"), false).toBool());
    s.endGroup();
    applyTheme(themeIdx);   // apply persisted theme at startup
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
    applyTheme(m_theme->currentIndex());
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
