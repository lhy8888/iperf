#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QString>
#include <QVariantMap>
#include <QVector>

// ---------------------------------------------------------------------------
// High-level UI enumerations
// ---------------------------------------------------------------------------

// Single vs. mixed traffic mode (main UI toggle)
enum class TrafficMode {
    Single,  // One traffic type + one packet size
    Mixed    // Multiple rows of (type, size, ratio%) — v1: runs dominant type
};

// Supported protocol / traffic types
enum class TrafficType {
    Tcp,    // v1 supported
    Udp,    // v1 supported
    Icmp,   // v2+ placeholder (greyed out in UI)
    Http,   // v2+ placeholder
    Https,  // v2+ placeholder
    Dns,    // v2+ placeholder
    Ftp,    // v2+ placeholder
};

// Packet / segment size presets
enum class PacketSize {
    B64,    // 64 bytes
    B128,   // 128 bytes
    B256,   // 256 bytes
    B512,   // 512 bytes
    B1024,  // 1024 bytes
    B1518,  // Standard Ethernet MTU payload
    Jumbo,  // 9000 bytes (jumbo frame)
    Custom  // Use IperfGuiConfig::customPacketSizeBytes
};

// Test duration presets
enum class DurationPreset {
    Min5,       // 5 minutes  (300 s)
    Min30,      // 30 minutes (1800 s)
    H1,         // 1 hour     (3600 s)
    H6,         // 6 hours    (21600 s)
    H24,        // 24 hours   (86400 s)
    Continuous  // Run until Stop (duration = 0 in iperf)
};

// Traffic direction (client perspective)
enum class Direction {
    Uplink,        // Client → Server (iperf normal)
    Downlink,      // Server → Client (iperf --reverse)
    Bidirectional  // Both directions (iperf --bidir)
};

// One row in the Mixed traffic table
struct TrafficMixEntry {
    TrafficType trafficType  = TrafficType::Tcp;
    PacketSize  packetSize   = PacketSize::B1518;
    int         ratioPercent = 25;  // All rows must sum to 100
};

// ---------------------------------------------------------------------------
// Core configuration passed to IperfCoreBridge
// ---------------------------------------------------------------------------
struct IperfGuiConfig
{
    enum class Mode { Client, Server };
    enum class Protocol { Tcp, Udp };
    enum class AddressFamily { Any, IPv4, IPv6 };

    // ── Role ──────────────────────────────────────────────────────────────
    Mode mode = Mode::Client;

    // ── High-level traffic configuration (UI-facing) ──────────────────────
    TrafficMode    trafficMode    = TrafficMode::Single;
    TrafficType    trafficType    = TrafficType::Tcp;
    PacketSize     packetSize     = PacketSize::B1518;
    DurationPreset durationPreset = DurationPreset::H1;
    Direction      direction      = Direction::Bidirectional;

    // Mixed mode rows (used when trafficMode == Mixed)
    QVector<TrafficMixEntry> mixEntries;

    // ── Connection (UI-facing aliases) ────────────────────────────────────
    QString serverAddress;   // Client: target host / IP  → resolved into host
    QString listenAddress;   // Server: listen address (empty → 0.0.0.0)

    // ── Expert controls (hidden unless Settings → Show Expert Controls) ───
    int           customPort   = 0;               // 0 = auto per protocol
    AddressFamily forceFamily  = AddressFamily::Any;
    int           customPacketSizeBytes = 1518;    // used when packetSize==Custom

    // ── Internal / computed fields (resolved by TestPage::buildConfig) ────
    // Bridge reads these directly; they are filled from the high-level fields
    // before setConfiguration() is called.
    Protocol protocol    = Protocol::Tcp;  // resolved from trafficType
    QString  host;                          // = serverAddress
    int      port        = 5201;            // auto from trafficType or customPort
    int      duration    = 3600;            // resolved from durationPreset
    int      parallel    = 1;               // set by orchestrator during TCP climb
    int      blockSize   = 0;              // resolved from packetSize
    quint64  bitrateBps  = 0;             // set by orchestrator for UDP binary search
    bool     reverse        = false;        // resolved from direction
    bool     bidirectional  = false;        // resolved from direction

    // ── Address family (resolved from forceFamily by buildConfig) ─────────
    AddressFamily family = AddressFamily::Any;

    // ── Other iperf params (fixed defaults, not exposed in main UI) ───────
    QString bindAddress;
    QString bindDev;
    QString congestionControl;
    QString timestampFormat;
    QString title;
    QString extraData;
    // Auth (SSL, v1 not used)
    QString serverAuthUsers;
    QString clientUsername;
    QString clientPassword;
    QString clientPublicKey;
    QString serverPrivateKey;

    int  bindPort            = 0;
    int  windowSize          = 0;
    int  mss                 = 0;
    int  reporterIntervalMs  = 1000;
    int  statsIntervalMs     = 1000;
    int  pacingTimerUs       = 1000;
    int  connectTimeoutMs    = -1;
    int  tos                 = 0;

    bool oneOff              = false;
    bool noDelay             = false;
    bool getServerOutput     = true;
    bool jsonStream          = true;
    bool jsonStreamFullOutput = true;
    bool udpCounters64Bit    = true;
    bool zeroCopy            = false;
    bool timestamps          = false;
    bool repeatingPayload    = false;
    bool skipRxCopy          = false;
    bool mptcp               = false;
    bool dontFragment        = false;
    bool forceFlush          = true;
    bool usePkcs1Padding     = false;
    bool probeSession        = false;   // transient: true while the orchestrator is probing
};

// ---------------------------------------------------------------------------
// Event model
// ---------------------------------------------------------------------------
enum class IperfEventKind {
    Info,
    Started,
    Interval,
    Summary,
    Error,
    Finished
};

struct IperfGuiEvent
{
    IperfEventKind kind = IperfEventKind::Info;
    QString eventName;
    QString message;
    QString rawJson;
    QJsonObject rawObject;
    QVariantMap fields;
    QDateTime receivedAt;
};

struct IperfSessionRecord
{
    QDateTime startedAt;
    IperfGuiConfig config;
    int exitCode = 0;
    QString statusText;
    QString rawJson;
    QVector<IperfGuiEvent> events;
    QVariantMap finalFields;

    // Computed from interval events at session completion
    double peakBps       = 0.0;  // max(interval.bits_per_second)
    double stableBps     = 0.0;  // mean of middle 60% intervals
    double lossPercent   = 0.0;  // UDP: final loss percent
    int optimalParallel  = 1;    // FindMax TCP: optimal parallel stream count
};

// ---------------------------------------------------------------------------
// Helper inline functions
// ---------------------------------------------------------------------------
inline QString iperfModeName(IperfGuiConfig::Mode mode)
{
    return mode == IperfGuiConfig::Mode::Server
        ? QStringLiteral("Server")
        : QStringLiteral("Client");
}

inline QString iperfProtocolName(IperfGuiConfig::Protocol protocol)
{
    return protocol == IperfGuiConfig::Protocol::Udp
        ? QStringLiteral("UDP")
        : QStringLiteral("TCP");
}

inline QString iperfTargetName(const IperfGuiConfig &config)
{
    if (config.mode == IperfGuiConfig::Mode::Server) {
        return config.listenAddress.isEmpty()
            ? QStringLiteral("0.0.0.0")
            : config.listenAddress;
    }
    return config.serverAddress.isEmpty()
        ? QStringLiteral("localhost")
        : config.serverAddress;
}

inline QString iperfHumanScaled(double value, const QString &unit, double scale, const QString &prefix)
{
    const QLocale locale;
    return locale.toString(value / scale, 'f', 2) + QLatin1Char(' ') + prefix + unit;
}

inline QString iperfHumanBytes(qint64 bytes)
{
    const double absValue = bytes < 0 ? -static_cast<double>(bytes) : static_cast<double>(bytes);
    if (absValue >= 1024.0 * 1024.0 * 1024.0)
        return iperfHumanScaled(static_cast<double>(bytes), QStringLiteral("B"), 1024.0*1024.0*1024.0, QStringLiteral("G"));
    if (absValue >= 1024.0 * 1024.0)
        return iperfHumanScaled(static_cast<double>(bytes), QStringLiteral("B"), 1024.0*1024.0, QStringLiteral("M"));
    if (absValue >= 1024.0)
        return iperfHumanScaled(static_cast<double>(bytes), QStringLiteral("B"), 1024.0, QStringLiteral("K"));
    return QLocale().toString(bytes) + QStringLiteral(" B");
}

inline QString iperfHumanBitsPerSecond(double bps)
{
    const double absValue = bps < 0.0 ? -bps : bps;
    if (absValue >= 1000.0 * 1000.0 * 1000.0)
        return iperfHumanScaled(bps, QStringLiteral("bps"), 1000.0*1000.0*1000.0, QStringLiteral("G"));
    if (absValue >= 1000.0 * 1000.0)
        return iperfHumanScaled(bps, QStringLiteral("bps"), 1000.0*1000.0, QStringLiteral("M"));
    if (absValue >= 1000.0)
        return iperfHumanScaled(bps, QStringLiteral("bps"), 1000.0, QStringLiteral("K"));
    return QLocale().toString(bps, 'f', 2) + QStringLiteral(" bps");
}

inline QString iperfHumanPercent(double value)
{
    return QLocale().toString(value, 'f', 2) + QStringLiteral("%");
}

inline QString trafficModeName(TrafficMode mode)
{
    return mode == TrafficMode::Mixed ? QStringLiteral("Mixed") : QStringLiteral("Single");
}

inline QString trafficTypeName(TrafficType type)
{
    switch (type) {
    case TrafficType::Udp:
        return QStringLiteral("UDP");
    case TrafficType::Icmp:
        return QStringLiteral("ICMP");
    case TrafficType::Http:
        return QStringLiteral("HTTP");
    case TrafficType::Https:
        return QStringLiteral("HTTPS");
    case TrafficType::Dns:
        return QStringLiteral("DNS");
    case TrafficType::Ftp:
        return QStringLiteral("FTP");
    case TrafficType::Tcp:
    default:
        return QStringLiteral("TCP");
    }
}

inline QString packetSizeName(PacketSize size)
{
    switch (size) {
    case PacketSize::B64:
        return QStringLiteral("64 B");
    case PacketSize::B128:
        return QStringLiteral("128 B");
    case PacketSize::B256:
        return QStringLiteral("256 B");
    case PacketSize::B512:
        return QStringLiteral("512 B");
    case PacketSize::B1024:
        return QStringLiteral("1024 B");
    case PacketSize::B1518:
        return QStringLiteral("1518 B");
    case PacketSize::Jumbo:
        return QStringLiteral("9000 B");
    case PacketSize::Custom:
    default:
        return QStringLiteral("Custom");
    }
}

inline QString durationPresetName(DurationPreset preset)
{
    switch (preset) {
    case DurationPreset::Min5:
        return QStringLiteral("5 min");
    case DurationPreset::Min30:
        return QStringLiteral("30 min");
    case DurationPreset::H1:
        return QStringLiteral("1 h");
    case DurationPreset::H6:
        return QStringLiteral("6 h");
    case DurationPreset::H24:
        return QStringLiteral("24 h");
    case DurationPreset::Continuous:
    default:
        return QStringLiteral("Continuous");
    }
}

inline QString directionName(Direction direction)
{
    switch (direction) {
    case Direction::Downlink:
        return QStringLiteral("Downlink");
    case Direction::Bidirectional:
        return QStringLiteral("Bidirectional");
    case Direction::Uplink:
    default:
        return QStringLiteral("Uplink");
    }
}

inline QString iperfFamilyName(IperfGuiConfig::AddressFamily family)
{
    switch (family) {
    case IperfGuiConfig::AddressFamily::IPv4:
        return QStringLiteral("IPv4");
    case IperfGuiConfig::AddressFamily::IPv6:
        return QStringLiteral("IPv6");
    default:
        return QStringLiteral("Any");
    }
}

inline QString trafficMixEntryText(const TrafficMixEntry &entry)
{
    return QStringLiteral("%1 %2 (%3%)")
        .arg(trafficTypeName(entry.trafficType),
             packetSizeName(entry.packetSize))
        .arg(entry.ratioPercent);
}

inline QString trafficMixEntriesText(const QVector<TrafficMixEntry> &entries)
{
    QString text;
    for (const TrafficMixEntry &entry : entries) {
        if (!text.isEmpty()) {
            text += QLatin1Char('\n');
        }
        text += QStringLiteral("  - ");
        text += trafficMixEntryText(entry);
    }
    return text;
}

inline QJsonObject trafficMixEntryToJson(const TrafficMixEntry &entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("traffic_type"), trafficTypeName(entry.trafficType));
    object.insert(QStringLiteral("packet_size"), packetSizeName(entry.packetSize));
    object.insert(QStringLiteral("ratio_percent"), entry.ratioPercent);
    return object;
}

inline QJsonArray trafficMixEntriesToJson(const QVector<TrafficMixEntry> &entries)
{
    QJsonArray array;
    for (const TrafficMixEntry &entry : entries) {
        array.append(trafficMixEntryToJson(entry));
    }
    return array;
}

inline QJsonObject iperfEventToJson(const IperfGuiEvent &event)
{
    QJsonObject object;
    object.insert(QStringLiteral("kind"), static_cast<int>(event.kind));
    object.insert(QStringLiteral("event_name"), event.eventName);
    object.insert(QStringLiteral("message"), event.message);
    object.insert(QStringLiteral("raw_json"), event.rawJson);
    object.insert(QStringLiteral("raw_object"), event.rawObject);
    if (!event.fields.isEmpty()) {
        object.insert(QStringLiteral("fields"), QJsonObject::fromVariantMap(event.fields));
    }
    if (event.receivedAt.isValid()) {
        object.insert(QStringLiteral("received_at"), event.receivedAt.toString(Qt::ISODateWithMs));
    }
    return object;
}

inline QJsonObject iperfConfigToJson(const IperfGuiConfig &config)
{
    QJsonObject object;
    object.insert(QStringLiteral("mode"), static_cast<int>(config.mode));
    object.insert(QStringLiteral("traffic_mode"), trafficModeName(config.trafficMode));
    object.insert(QStringLiteral("traffic_type"), trafficTypeName(config.trafficType));
    object.insert(QStringLiteral("packet_size"), packetSizeName(config.packetSize));
    object.insert(QStringLiteral("duration_preset"), durationPresetName(config.durationPreset));
    object.insert(QStringLiteral("direction"), directionName(config.direction));
    object.insert(QStringLiteral("mix_entries"), trafficMixEntriesToJson(config.mixEntries));
    object.insert(QStringLiteral("server_address"), config.serverAddress);
    object.insert(QStringLiteral("listen_address"), config.listenAddress);
    object.insert(QStringLiteral("custom_port"), config.customPort);
    object.insert(QStringLiteral("force_family"), iperfFamilyName(config.forceFamily));
    object.insert(QStringLiteral("family"), iperfFamilyName(config.family));
    object.insert(QStringLiteral("custom_packet_size_bytes"), config.customPacketSizeBytes);
    object.insert(QStringLiteral("protocol"), static_cast<int>(config.protocol));
    object.insert(QStringLiteral("host"), config.host);
    object.insert(QStringLiteral("bind_address"), config.bindAddress);
    object.insert(QStringLiteral("bind_dev"), config.bindDev);
    object.insert(QStringLiteral("title"), config.title);
    object.insert(QStringLiteral("extra_data"), config.extraData);
    object.insert(QStringLiteral("congestion_control"), config.congestionControl);
    object.insert(QStringLiteral("server_auth_users"), config.serverAuthUsers);
    object.insert(QStringLiteral("client_username"), config.clientUsername);
    object.insert(QStringLiteral("client_password"), config.clientPassword);
    object.insert(QStringLiteral("client_public_key"), config.clientPublicKey);
    object.insert(QStringLiteral("server_private_key"), config.serverPrivateKey);
    object.insert(QStringLiteral("timestamp_format"), config.timestampFormat);
    object.insert(QStringLiteral("port"), config.port);
    object.insert(QStringLiteral("bind_port"), config.bindPort);
    object.insert(QStringLiteral("duration"), config.duration);
    object.insert(QStringLiteral("parallel"), config.parallel);
    object.insert(QStringLiteral("block_size"), config.blockSize);
    object.insert(QStringLiteral("window_size"), config.windowSize);
    object.insert(QStringLiteral("mss"), config.mss);
    object.insert(QStringLiteral("reporter_interval_ms"), config.reporterIntervalMs);
    object.insert(QStringLiteral("stats_interval_ms"), config.statsIntervalMs);
    object.insert(QStringLiteral("pacing_timer_us"), config.pacingTimerUs);
    object.insert(QStringLiteral("connect_timeout_ms"), config.connectTimeoutMs);
    object.insert(QStringLiteral("tos"), config.tos);
    object.insert(QStringLiteral("bitrate_bps"), QString::number(config.bitrateBps));
    object.insert(QStringLiteral("reverse"), config.reverse);
    object.insert(QStringLiteral("bidirectional"), config.bidirectional);
    object.insert(QStringLiteral("one_off"), config.oneOff);
    object.insert(QStringLiteral("no_delay"), config.noDelay);
    object.insert(QStringLiteral("get_server_output"), config.getServerOutput);
    object.insert(QStringLiteral("json_stream"), config.jsonStream);
    object.insert(QStringLiteral("json_stream_full_output"), config.jsonStreamFullOutput);
    object.insert(QStringLiteral("udp_counters_64bit"), config.udpCounters64Bit);
    object.insert(QStringLiteral("zero_copy"), config.zeroCopy);
    object.insert(QStringLiteral("timestamps"), config.timestamps);
    object.insert(QStringLiteral("repeating_payload"), config.repeatingPayload);
    object.insert(QStringLiteral("skip_rx_copy"), config.skipRxCopy);
    object.insert(QStringLiteral("mptcp"), config.mptcp);
    object.insert(QStringLiteral("dont_fragment"), config.dontFragment);
    object.insert(QStringLiteral("force_flush"), config.forceFlush);
    object.insert(QStringLiteral("use_pkcs1_padding"), config.usePkcs1Padding);
    return object;
}

inline QJsonObject iperfSessionRecordToJson(const IperfSessionRecord &record)
{
    QJsonObject object;
    object.insert(QStringLiteral("started_at"),
                  record.startedAt.isValid() ? record.startedAt.toString(Qt::ISODateWithMs) : QString());
    object.insert(QStringLiteral("probe_session"), record.config.probeSession);
    object.insert(QStringLiteral("exit_code"), record.exitCode);
    object.insert(QStringLiteral("status_text"), record.statusText);
    object.insert(QStringLiteral("raw_json"), record.rawJson);
    object.insert(QStringLiteral("peak_bps"), record.peakBps);
    object.insert(QStringLiteral("stable_bps"), record.stableBps);
    object.insert(QStringLiteral("loss_percent"), record.lossPercent);
    object.insert(QStringLiteral("optimal_parallel"), record.optimalParallel);
    object.insert(QStringLiteral("config"), iperfConfigToJson(record.config));
    if (!record.finalFields.isEmpty()) {
        object.insert(QStringLiteral("final_fields"), QJsonObject::fromVariantMap(record.finalFields));
    }
    if (!record.events.isEmpty()) {
        QJsonArray events;
        for (const IperfGuiEvent &event : record.events) {
            events.append(iperfEventToJson(event));
        }
        object.insert(QStringLiteral("events"), events);
    }
    return object;
}

Q_DECLARE_METATYPE(IperfGuiConfig)
Q_DECLARE_METATYPE(IperfGuiEvent)
Q_DECLARE_METATYPE(IperfSessionRecord)
Q_DECLARE_METATYPE(IperfEventKind)
Q_DECLARE_METATYPE(TrafficMode)
Q_DECLARE_METATYPE(TrafficType)
Q_DECLARE_METATYPE(PacketSize)
Q_DECLARE_METATYPE(DurationPreset)
Q_DECLARE_METATYPE(Direction)
Q_DECLARE_METATYPE(IperfGuiConfig::AddressFamily)
