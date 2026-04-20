#pragma once

#include <QDateTime>
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

Q_DECLARE_METATYPE(IperfGuiConfig)
Q_DECLARE_METATYPE(IperfGuiEvent)
Q_DECLARE_METATYPE(IperfSessionRecord)
Q_DECLARE_METATYPE(IperfEventKind)
Q_DECLARE_METATYPE(TrafficType)
Q_DECLARE_METATYPE(PacketSize)
