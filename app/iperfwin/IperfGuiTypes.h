#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QLocale>
#include <QString>
#include <QVariantMap>
#include <QVector>

struct IperfGuiConfig
{
    enum class Mode {
        Client,
        Server
    };

    enum class Protocol {
        Tcp,
        Udp
    };

    enum class AddressFamily {
        Any,
        IPv4,
        IPv6
    };

    Mode mode = Mode::Client;
    Protocol protocol = Protocol::Tcp;
    AddressFamily family = AddressFamily::Any;

    QString host;
    QString bindAddress;
    QString bindDev;
    QString title;
    QString extraData;
    QString congestionControl;
    QString serverAuthUsers;
    QString clientUsername;
    QString clientPassword;
    QString clientPublicKey;
    QString serverPrivateKey;
    QString timestampFormat;

    int port = 5201;
    int bindPort = 0;
    int duration = 10;
    int parallel = 1;
    int blockSize = 0;
    int windowSize = 0;
    int mss = 0;
    int reporterIntervalMs = 1000;
    int statsIntervalMs = 1000;
    int pacingTimerUs = 1000;
    int connectTimeoutMs = -1;
    int tos = 0;

    quint64 bitrateBps = 0;

    bool reverse = false;
    bool bidirectional = false;
    bool oneOff = false;
    bool noDelay = false;
    bool getServerOutput = true;
    bool jsonStream = true;
    bool jsonStreamFullOutput = true;
    bool udpCounters64Bit = true;
    bool zeroCopy = false;
    bool timestamps = false;
    bool repeatingPayload = false;
    bool skipRxCopy = false;
    bool mptcp = false;
    bool dontFragment = false;
    bool forceFlush = true;
    bool usePkcs1Padding = false;
};

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
};

inline QString iperfModeName(IperfGuiConfig::Mode mode)
{
    return mode == IperfGuiConfig::Mode::Server ? QStringLiteral("Server") : QStringLiteral("Client");
}

inline QString iperfProtocolName(IperfGuiConfig::Protocol protocol)
{
    return protocol == IperfGuiConfig::Protocol::Udp ? QStringLiteral("UDP") : QStringLiteral("TCP");
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

inline QString iperfTargetName(const IperfGuiConfig &config)
{
    if (config.mode == IperfGuiConfig::Mode::Server) {
        return QStringLiteral("0.0.0.0");
    }
    return config.host.isEmpty() ? QStringLiteral("localhost") : config.host;
}

inline QString iperfHumanScaled(double value, const QString &unit, double scale, const QString &prefix)
{
    const QLocale locale;
    return locale.toString(value / scale, 'f', 2) + QLatin1Char(' ') + prefix + unit;
}

inline QString iperfHumanBytes(qint64 bytes)
{
    const double absValue = bytes < 0 ? -static_cast<double>(bytes) : static_cast<double>(bytes);
    if (absValue >= 1024.0 * 1024.0 * 1024.0) {
        return iperfHumanScaled(static_cast<double>(bytes), QStringLiteral("B"), 1024.0 * 1024.0 * 1024.0, QStringLiteral("G"));
    }
    if (absValue >= 1024.0 * 1024.0) {
        return iperfHumanScaled(static_cast<double>(bytes), QStringLiteral("B"), 1024.0 * 1024.0, QStringLiteral("M"));
    }
    if (absValue >= 1024.0) {
        return iperfHumanScaled(static_cast<double>(bytes), QStringLiteral("B"), 1024.0, QStringLiteral("K"));
    }
    return QLocale().toString(bytes) + QStringLiteral(" B");
}

inline QString iperfHumanBitsPerSecond(double bps)
{
    const double absValue = bps < 0.0 ? -bps : bps;
    if (absValue >= 1000.0 * 1000.0 * 1000.0) {
        return iperfHumanScaled(bps, QStringLiteral("bps"), 1000.0 * 1000.0 * 1000.0, QStringLiteral("G"));
    }
    if (absValue >= 1000.0 * 1000.0) {
        return iperfHumanScaled(bps, QStringLiteral("bps"), 1000.0 * 1000.0, QStringLiteral("M"));
    }
    if (absValue >= 1000.0) {
        return iperfHumanScaled(bps, QStringLiteral("bps"), 1000.0, QStringLiteral("K"));
    }
    return QLocale().toString(bps, 'f', 2) + QStringLiteral(" bps");
}

inline QString iperfHumanPercent(double value)
{
    return QLocale().toString(value, 'f', 2) + QStringLiteral("%");
}

inline QJsonObject iperfConfigToJson(const IperfGuiConfig &config)
{
    QJsonObject object;
    object.insert(QStringLiteral("mode"), static_cast<int>(config.mode));
    object.insert(QStringLiteral("protocol"), static_cast<int>(config.protocol));
    object.insert(QStringLiteral("family"), static_cast<int>(config.family));
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

inline IperfGuiConfig iperfConfigFromJson(const QJsonObject &object, const IperfGuiConfig &fallback = IperfGuiConfig())
{
    IperfGuiConfig config = fallback;
    config.mode = static_cast<IperfGuiConfig::Mode>(object.value(QStringLiteral("mode")).toInt(static_cast<int>(fallback.mode)));
    config.protocol = static_cast<IperfGuiConfig::Protocol>(object.value(QStringLiteral("protocol")).toInt(static_cast<int>(fallback.protocol)));
    config.family = static_cast<IperfGuiConfig::AddressFamily>(object.value(QStringLiteral("family")).toInt(static_cast<int>(fallback.family)));
    config.host = object.value(QStringLiteral("host")).toString(fallback.host);
    config.bindAddress = object.value(QStringLiteral("bind_address")).toString(fallback.bindAddress);
    config.bindDev = object.value(QStringLiteral("bind_dev")).toString(fallback.bindDev);
    config.title = object.value(QStringLiteral("title")).toString(fallback.title);
    config.extraData = object.value(QStringLiteral("extra_data")).toString(fallback.extraData);
    config.congestionControl = object.value(QStringLiteral("congestion_control")).toString(fallback.congestionControl);
    config.serverAuthUsers = object.value(QStringLiteral("server_auth_users")).toString(fallback.serverAuthUsers);
    config.clientUsername = object.value(QStringLiteral("client_username")).toString(fallback.clientUsername);
    config.clientPassword = object.value(QStringLiteral("client_password")).toString(fallback.clientPassword);
    config.clientPublicKey = object.value(QStringLiteral("client_public_key")).toString(fallback.clientPublicKey);
    config.serverPrivateKey = object.value(QStringLiteral("server_private_key")).toString(fallback.serverPrivateKey);
    config.timestampFormat = object.value(QStringLiteral("timestamp_format")).toString(fallback.timestampFormat);
    config.port = object.value(QStringLiteral("port")).toInt(fallback.port);
    config.bindPort = object.value(QStringLiteral("bind_port")).toInt(fallback.bindPort);
    config.duration = object.value(QStringLiteral("duration")).toInt(fallback.duration);
    config.parallel = object.value(QStringLiteral("parallel")).toInt(fallback.parallel);
    config.blockSize = object.value(QStringLiteral("block_size")).toInt(fallback.blockSize);
    config.windowSize = object.value(QStringLiteral("window_size")).toInt(fallback.windowSize);
    config.mss = object.value(QStringLiteral("mss")).toInt(fallback.mss);
    config.reporterIntervalMs = object.value(QStringLiteral("reporter_interval_ms")).toInt(fallback.reporterIntervalMs);
    config.statsIntervalMs = object.value(QStringLiteral("stats_interval_ms")).toInt(fallback.statsIntervalMs);
    config.pacingTimerUs = object.value(QStringLiteral("pacing_timer_us")).toInt(fallback.pacingTimerUs);
    config.connectTimeoutMs = object.value(QStringLiteral("connect_timeout_ms")).toInt(fallback.connectTimeoutMs);
    config.tos = object.value(QStringLiteral("tos")).toInt(fallback.tos);
    {
        bool ok = false;
        const QString bitrateText = object.value(QStringLiteral("bitrate_bps")).toString(QString::number(fallback.bitrateBps));
        const qulonglong bitrate = bitrateText.toULongLong(&ok);
        config.bitrateBps = ok ? bitrate : fallback.bitrateBps;
    }
    config.reverse = object.value(QStringLiteral("reverse")).toBool(fallback.reverse);
    config.bidirectional = object.value(QStringLiteral("bidirectional")).toBool(fallback.bidirectional);
    config.oneOff = object.value(QStringLiteral("one_off")).toBool(fallback.oneOff);
    config.noDelay = object.value(QStringLiteral("no_delay")).toBool(fallback.noDelay);
    config.getServerOutput = object.value(QStringLiteral("get_server_output")).toBool(fallback.getServerOutput);
    config.jsonStream = object.value(QStringLiteral("json_stream")).toBool(fallback.jsonStream);
    config.jsonStreamFullOutput = object.value(QStringLiteral("json_stream_full_output")).toBool(fallback.jsonStreamFullOutput);
    config.udpCounters64Bit = object.value(QStringLiteral("udp_counters_64bit")).toBool(fallback.udpCounters64Bit);
    config.zeroCopy = object.value(QStringLiteral("zero_copy")).toBool(fallback.zeroCopy);
    config.timestamps = object.value(QStringLiteral("timestamps")).toBool(fallback.timestamps);
    config.repeatingPayload = object.value(QStringLiteral("repeating_payload")).toBool(fallback.repeatingPayload);
    config.skipRxCopy = object.value(QStringLiteral("skip_rx_copy")).toBool(fallback.skipRxCopy);
    config.mptcp = object.value(QStringLiteral("mptcp")).toBool(fallback.mptcp);
    config.dontFragment = object.value(QStringLiteral("dont_fragment")).toBool(fallback.dontFragment);
    config.forceFlush = object.value(QStringLiteral("force_flush")).toBool(fallback.forceFlush);
    config.usePkcs1Padding = object.value(QStringLiteral("use_pkcs1_padding")).toBool(fallback.usePkcs1Padding);
    return config;
}

Q_DECLARE_METATYPE(IperfGuiConfig)
Q_DECLARE_METATYPE(IperfGuiEvent)
Q_DECLARE_METATYPE(IperfSessionRecord)
Q_DECLARE_METATYPE(IperfEventKind)
