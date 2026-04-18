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

Q_DECLARE_METATYPE(IperfGuiConfig)
Q_DECLARE_METATYPE(IperfGuiEvent)
Q_DECLARE_METATYPE(IperfSessionRecord)
Q_DECLARE_METATYPE(IperfEventKind)

