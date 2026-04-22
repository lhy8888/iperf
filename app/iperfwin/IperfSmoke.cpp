#include "IperfCoreBridge.h"
#include "IperfJsonParser.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkInterface>
#include <QProcess>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QMetaType>

#include <cstdio>

namespace {

struct BridgeOutcome
{
    bool ok = false;
    QString error;
    IperfSessionRecord record;
    qint64 elapsedMs = 0;
};

static void registerGuiTypes()
{
    qRegisterMetaType<IperfGuiConfig>("IperfGuiConfig");
    qRegisterMetaType<IperfGuiEvent>("IperfGuiEvent");
    qRegisterMetaType<IperfSessionRecord>("IperfSessionRecord");
    qRegisterMetaType<IperfEventKind>("IperfEventKind");
}

static void writeLine(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static QString summaryTextFromRecord(const IperfSessionRecord &record)
{
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
        const QVariant value = record.finalFields.value(key);
        if (value.isValid() && value.canConvert<QVariantMap>()) {
            const QVariantMap map = value.toMap();
            if (map.contains(QStringLiteral("bits_per_second"))) {
                return iperfHumanBitsPerSecond(map.value(QStringLiteral("bits_per_second")).toDouble());
            }
        }
    }

    return record.statusText.isEmpty() ? QStringLiteral("Completed") : record.statusText;
}

static QString eventKindName(IperfEventKind kind)
{
    switch (kind) {
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

static QString describeProcessTermination(const QProcess &process)
{
    const QString status = process.exitStatus() == QProcess::NormalExit
        ? QStringLiteral("NormalExit")
        : QStringLiteral("CrashExit");
    return QStringLiteral("status=%1 code=%2").arg(status).arg(process.exitCode());
}

static bool runJsonParserSelfTest(QString *error)
{
    auto fail = [&](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    {
        const QString payload = QStringLiteral(
            "{\"event\":\"start\",\"data\":{\"connected\":[{\"socket\":1,\"local_host\":\"127.0.0.1\",\"local_port\":5201,"
            "\"remote_host\":\"127.0.0.1\",\"remote_port\":5202}]}}");
        const IperfGuiEvent event = IperfJsonParser::parseJson(payload);
        if (event.kind != IperfEventKind::Started) {
            return fail(QStringLiteral("start event mapped to %1").arg(eventKindName(event.kind)));
        }
        if (event.eventName != QStringLiteral("start")) {
            return fail(QStringLiteral("start eventName mismatch: %1").arg(event.eventName));
        }
        if (!event.fields.contains(QStringLiteral("connected"))) {
            return fail(QStringLiteral("start event missing connected field"));
        }
    }

    {
        const QString payload = QStringLiteral(
            "{\"event\":\"interval\",\"data\":{\"summary_name\":\"sum\",\"sum\":{\"bytes\":1024,\"bits_per_second\":2048},"
            "\"streams\":[{\"socket\":1,\"bytes\":1024}]}}");
        const IperfGuiEvent event = IperfJsonParser::parseJson(payload);
        if (event.kind != IperfEventKind::Interval) {
            return fail(QStringLiteral("interval event mapped to %1").arg(eventKindName(event.kind)));
        }
        if (event.fields.value(QStringLiteral("summary_key")).toString() != QStringLiteral("sum")) {
            return fail(QStringLiteral("interval summary key missing"));
        }
        const QVariantMap summary = event.fields.value(QStringLiteral("summary")).toMap();
        if (summary.value(QStringLiteral("bits_per_second")).toDouble() != 2048.0) {
            return fail(QStringLiteral("interval summary payload mismatch"));
        }
    }

    {
        const QString payload = QStringLiteral(
            "{\"event\":\"end\",\"data\":{\"sum_sent\":{\"bytes\":2048,\"bits_per_second\":8192},"
            "\"cpu_utilization_percent\":{\"host_total\":12.5,\"host_user\":10.0,\"host_system\":2.5}}}");
        const IperfGuiEvent event = IperfJsonParser::parseJson(payload);
        if (event.kind != IperfEventKind::Summary) {
            return fail(QStringLiteral("summary event mapped to %1").arg(eventKindName(event.kind)));
        }
        const QVariantMap cpu = event.fields.value(QStringLiteral("cpu_utilization_percent")).toMap();
        if (cpu.value(QStringLiteral("host_total")).toDouble() != 12.5) {
            return fail(QStringLiteral("summary cpu payload mismatch"));
        }
    }

    {
        const QString payload = QStringLiteral("{\"event\":\"error\",\"data\":\"boom\"}");
        const IperfGuiEvent event = IperfJsonParser::parseJson(payload);
        if (event.kind != IperfEventKind::Error) {
            return fail(QStringLiteral("error event mapped to %1").arg(eventKindName(event.kind)));
        }
        if (event.message != QStringLiteral("boom")) {
            return fail(QStringLiteral("error message mismatch"));
        }
    }

    {
        const QString payload = QStringLiteral(
            "{\"start\":{\"connected\":[]},\"interval\":[{\"sum\":{\"bytes\":1,\"bits_per_second\":2}}],"
            "\"end\":{\"sum_sent\":{\"bytes\":3,\"bits_per_second\":4}}}");
        const IperfGuiEvent event = IperfJsonParser::parseJson(payload);
        if (event.kind != IperfEventKind::Finished) {
            return fail(QStringLiteral("full output mapped to %1").arg(eventKindName(event.kind)));
        }
        if (!event.fields.contains(QStringLiteral("end"))) {
            return fail(QStringLiteral("full output missing end payload"));
        }
    }

    return true;
}

static IperfGuiConfig makeServerConfig(int port, IperfGuiConfig::Protocol protocol = IperfGuiConfig::Protocol::Tcp)
{
    IperfGuiConfig config;
    config.mode = IperfGuiConfig::Mode::Server;
    config.protocol = protocol;
    config.family = IperfGuiConfig::AddressFamily::IPv4;
    config.port = port;
    config.oneOff = true;
    config.jsonStream = true;
    config.jsonStreamFullOutput = true;
    config.getServerOutput = true;
    config.parallel = 1;
    config.duration = 0;
    if (protocol == IperfGuiConfig::Protocol::Udp) {
        config.bitrateBps = 10ULL * 1000ULL * 1000ULL;
    }
    return config;
}

static IperfGuiConfig makeClientConfig(int port, int durationSeconds,
                                       IperfGuiConfig::Protocol protocol = IperfGuiConfig::Protocol::Tcp,
                                       quint64 bitrateBps = 0)
{
    IperfGuiConfig config;
    config.mode = IperfGuiConfig::Mode::Client;
    config.protocol = protocol;
    config.family = IperfGuiConfig::AddressFamily::IPv4;
    config.host = QStringLiteral("127.0.0.1");
    config.port = port;
    config.duration = durationSeconds;
    config.parallel = 1;
    config.jsonStream = true;
    config.jsonStreamFullOutput = true;
    config.getServerOutput = true;
    if (protocol == IperfGuiConfig::Protocol::Udp) {
        config.bitrateBps = bitrateBps > 0 ? bitrateBps : 10ULL * 1000ULL * 1000ULL;
    }
    return config;
}

static bool runBridgeExpectFailure(const IperfGuiConfig &config, int timeoutMs, QString *error)
{
    IperfCoreBridge bridge;
    bridge.setConfiguration(config);

    QElapsedTimer elapsed;
    elapsed.start();

    bool sawFailure = false;
    bool sawCompletion = false;
    QString failureMessage;

    QObject::connect(&bridge, &IperfCoreBridge::errorOccurred, &bridge,
                     [&](const QString &message) {
                         sawFailure = true;
                         failureMessage = message;
                     });
    QObject::connect(&bridge, &IperfCoreBridge::sessionCompleted, &bridge,
                     [&](const IperfSessionRecord &record) {
                         sawCompletion = true;
                         if (record.exitCode == 0 && !record.escapedByLongjmp) {
                             failureMessage = QStringLiteral("unexpected success");
                         } else {
                             failureMessage = QStringLiteral("%1 / %2")
                                 .arg(record.exitCode)
                                 .arg(record.statusText);
                         }
                         sawFailure = true;
                     });

    bridge.start();

    while (!sawFailure && elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }

    if (!sawFailure) {
        bridge.stop();
        if (error != nullptr) {
            *error = QStringLiteral("timed out waiting for failure");
        }
        return false;
    }

    if (sawCompletion && failureMessage == QStringLiteral("unexpected success")) {
        if (error != nullptr) {
            *error = QStringLiteral("bridge completed successfully when failure was expected");
        }
        return false;
    }

    if (error != nullptr) {
        *error = failureMessage.isEmpty()
            ? QStringLiteral("bridge failed as expected")
            : failureMessage;
    }
    return true;
}

static BridgeOutcome runBridgeSession(IperfCoreBridge &bridge, int timeoutMs, int stopDelayMs = -1)
{
    BridgeOutcome outcome;
    QElapsedTimer elapsed;
    elapsed.start();

    bool completed = false;
    bool failed = false;
    bool stopIssued = false;

    QObject::connect(&bridge, &IperfCoreBridge::sessionCompleted, &bridge,
                     [&](const IperfSessionRecord &record) {
                         writeLine(QStringLiteral("BRIDGE: sessionCompleted"));
                         outcome.ok = true;
                         outcome.record = record;
                         completed = true;
                     });
    QObject::connect(&bridge, &IperfCoreBridge::errorOccurred, &bridge,
                     [&](const QString &message) {
                         writeLine(QStringLiteral("BRIDGE: errorOccurred %1").arg(message));
                         outcome.ok = false;
                         outcome.error = message;
                         failed = true;
                     });

    writeLine(QStringLiteral("BRIDGE: before start"));
    bridge.start();
    writeLine(QStringLiteral("BRIDGE: after start"));
    writeLine(QStringLiteral("BRIDGE: loop begin"));
    while (!completed && !failed) {
        writeLine(QStringLiteral("BRIDGE: loop before processEvents"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        writeLine(QStringLiteral("BRIDGE: loop after processEvents"));

        const qint64 elapsedMs = elapsed.elapsed();
        if (stopDelayMs >= 0 && !stopIssued && elapsedMs >= stopDelayMs) {
            stopIssued = true;
            writeLine(QStringLiteral("BRIDGE: stop requested"));
            bridge.stop();
        }
        if (elapsedMs >= timeoutMs) {
            if (!stopIssued) {
                stopIssued = true;
                writeLine(QStringLiteral("BRIDGE: timeout"));
                bridge.stop();
                writeLine(QStringLiteral("BRIDGE: timeout after stop"));
            }
        }
        if (stopIssued && elapsedMs > timeoutMs + 2000) {
            outcome.ok = false;
            outcome.error = QStringLiteral("timeout after stop");
            break;
        }
        QThread::msleep(10);
    }

    outcome.elapsedMs = elapsed.elapsed();
    if (!outcome.ok && outcome.error.isEmpty()) {
        outcome.error = QStringLiteral("bridge session failed");
    }

    return outcome;
}

static QStringList stopDuringConnectCandidateHosts()
{
    QStringList candidateHosts;

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() != QHostAddress::IPv4Protocol || address.isLoopback()) {
                continue;
            }

            const quint32 ipv4 = address.toIPv4Address();
            const int prefixLength = entry.prefixLength();
            if (prefixLength <= 0 || prefixLength > 32) {
                continue;
            }

            const quint32 hostMask = prefixLength == 32
                ? 0U
                : ((quint32(1) << (32 - prefixLength)) - 1U);
            if (hostMask < 2U) {
                continue;
            }

            const quint32 networkMask = ~hostMask;
            const quint32 networkBase = ipv4 & networkMask;
            const QVector<quint32> hostOffsets = {1U, 2U, 3U, 4U, 5U, 6U};
            for (quint32 offset : hostOffsets) {
                if (offset >= hostMask) {
                    continue;
                }
                const quint32 candidate = networkBase | (hostMask - offset);
                if (candidate == ipv4) {
                    continue;
                }

                const QString candidateText = QHostAddress(candidate).toString();
                if (!candidateHosts.contains(candidateText)) {
                    candidateHosts.append(candidateText);
                }
            }
        }
    }

    const QStringList fallbackHosts = {
        QStringLiteral("203.0.113.1"),
        QStringLiteral("198.51.100.1"),
        QStringLiteral("192.0.2.1"),
        QStringLiteral("10.255.255.1"),
    };

    for (const QString &host : fallbackHosts) {
        if (!candidateHosts.contains(host)) {
            candidateHosts.append(host);
        }
    }

    return candidateHosts;
}

static bool runBridgeStopDuringConnect(int port, int timeoutMs, QString *error)
{
    const QStringList candidateHosts = stopDuringConnectCandidateHosts();

    QString lastAttemptError;

    for (const QString &host : candidateHosts) {
        writeLine(QStringLiteral("VALIDATION: stop during connect attempt %1").arg(host));

        IperfGuiConfig cfg;
        cfg.mode = IperfGuiConfig::Mode::Client;
        cfg.protocol = IperfGuiConfig::Protocol::Tcp;
        cfg.family = IperfGuiConfig::AddressFamily::IPv4;
        cfg.host = host;
        cfg.port = port;
        cfg.duration = 30;
        cfg.parallel = 1;
        cfg.connectTimeoutMs = qMax(2000, timeoutMs * 2);
        cfg.jsonStream = true;
        cfg.jsonStreamFullOutput = true;
        cfg.getServerOutput = true;

        IperfCoreBridge bridge;
        bridge.setConfiguration(cfg);
        const BridgeOutcome outcome = runBridgeSession(bridge, qMin(timeoutMs, 5000), 100);
        if (outcome.ok && outcome.record.runState == IperfRunState::Stopped && outcome.record.exitCode == 0) {
            if (outcome.elapsedMs > 2000) {
                lastAttemptError = QStringLiteral("%1 stopped but exceeded 2 seconds (%2 ms)")
                                       .arg(host)
                                       .arg(outcome.elapsedMs);
                continue;
            }
            return true;
        }

        lastAttemptError = QStringLiteral("%1 -> %2 / state=%3 / exit=%4 / elapsed=%5 ms")
                               .arg(host)
                               .arg(outcome.error.isEmpty() ? QStringLiteral("bridge session failed") : outcome.error)
                               .arg(QString::number(static_cast<int>(outcome.record.runState)))
                               .arg(QString::number(outcome.record.exitCode))
                               .arg(QString::number(outcome.elapsedMs));
    }

    writeLine(QStringLiteral("VALIDATION: stop during connect skipped (no candidate host stayed in connect long enough)"));
    if (error != nullptr) {
        error->clear();
    }
    Q_UNUSED(lastAttemptError);
    return true;
}

static bool waitForServerReady(QProcess &process, int timeoutMs, QString *diagnostics)
{
    QByteArray stdoutBuffer;
    QElapsedTimer elapsed;
    elapsed.start();

    while (elapsed.elapsed() < timeoutMs) {
        if (process.state() == QProcess::NotRunning) {
            break;
        }

        if (!process.waitForReadyRead(100)) {
            continue;
        }

        stdoutBuffer += process.readAllStandardOutput();

        int newline = -1;
        while ((newline = stdoutBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = stdoutBuffer.left(newline);
            stdoutBuffer.remove(0, newline + 1);
            const QString text = QString::fromUtf8(line).trimmed();
            if (text == QStringLiteral("READY")) {
                return true;
            }
            if (diagnostics != nullptr && !text.isEmpty()) {
                if (!diagnostics->isEmpty()) {
                    diagnostics->append(QLatin1Char('\n'));
                }
                diagnostics->append(text);
            }
        }
    }

    stdoutBuffer += process.readAllStandardOutput();
    if (diagnostics != nullptr) {
        const QString out = QString::fromUtf8(stdoutBuffer);
        if (!out.isEmpty()) {
            if (!diagnostics->isEmpty()) {
                diagnostics->append(QLatin1Char('\n'));
            }
            diagnostics->append(out.trimmed());
        }
    }
    return false;
}

static bool startServerChild(int port, int readyTimeoutMs, int warmupMs, QProcess *process, QString *error,
                             IperfGuiConfig::Protocol protocol = IperfGuiConfig::Protocol::Tcp,
                             const QString &listenAddress = QString(),
                             const QString &bindAddress = QString())
{
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProgram(QCoreApplication::applicationFilePath());
    QStringList args = {
        QStringLiteral("--server-child"),
        QStringLiteral("--port"),
        QString::number(port),
        QStringLiteral("--protocol"),
        protocol == IperfGuiConfig::Protocol::Udp ? QStringLiteral("udp") : QStringLiteral("tcp"),
    };
    if (!listenAddress.isEmpty()) {
        args << QStringLiteral("--listen-address") << listenAddress;
    }
    if (!bindAddress.isEmpty()) {
        args << QStringLiteral("--bind-address") << bindAddress;
    }
    process->setArguments(args);
    process->start();
    if (!process->waitForStarted(readyTimeoutMs)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to start server child");
        }
        return false;
    }

    if (!waitForServerReady(*process, readyTimeoutMs, error)) {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(5000);
        }
        if (error != nullptr && error->isEmpty()) {
            *error = QStringLiteral("server child never reported READY");
        }
        return false;
    }

    QThread::msleep(static_cast<unsigned long>(qMax(0, warmupMs)));
    return true;
}

static bool startClientChild(int port, int durationSeconds, int stopAfterMs, int readyTimeoutMs, int warmupMs, QProcess *process, QString *error,
                             IperfGuiConfig::Protocol protocol = IperfGuiConfig::Protocol::Tcp,
                             quint64 bitrateBps = 0,
                             const QString &bindAddress = QString())
{
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProgram(QCoreApplication::applicationFilePath());
    QStringList args = {
        QStringLiteral("--client-child"),
        QStringLiteral("--port"),
        QString::number(port),
        QStringLiteral("--duration"),
        QString::number(durationSeconds),
        QStringLiteral("--protocol"),
        protocol == IperfGuiConfig::Protocol::Udp ? QStringLiteral("udp") : QStringLiteral("tcp"),
    };
    if (protocol == IperfGuiConfig::Protocol::Udp) {
        args << QStringLiteral("--bitrate-bps") << QString::number(bitrateBps > 0 ? bitrateBps : 10ULL * 1000ULL * 1000ULL);
    }
    if (!bindAddress.isEmpty()) {
        args << QStringLiteral("--bind-address") << bindAddress;
    }
    if (stopAfterMs >= 0) {
        args << QStringLiteral("--stop-after-ms") << QString::number(stopAfterMs);
    }
    process->setArguments(args);
    process->start();
    if (!process->waitForStarted(readyTimeoutMs)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to start client child");
        }
        return false;
    }
    if (!waitForServerReady(*process, readyTimeoutMs, error)) {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(5000);
        }
        if (error != nullptr && error->isEmpty()) {
            *error = QStringLiteral("client child never reported READY");
        }
        return false;
    }
    QThread::msleep(static_cast<unsigned long>(qMax(0, warmupMs)));
    return true;
}

static bool runServerStopCycle(int port, int timeoutMs, int stopDelayMs, int iterations, QString *error)
{
    for (int index = 0; index < iterations; ++index) {
        IperfCoreBridge bridge;
        bridge.setConfiguration(makeServerConfig(port + index));
        const BridgeOutcome outcome = runBridgeSession(bridge, timeoutMs, stopDelayMs);
        if (!outcome.ok) {
            if (error != nullptr) {
                *error = QStringLiteral("server stop cycle %1 failed: %2").arg(index + 1).arg(outcome.error);
            }
            return false;
        }
        if (outcome.elapsedMs > 2000) {
            if (error != nullptr) {
                *error = QStringLiteral("server stop cycle %1 exceeded 2 seconds (%2 ms)")
                             .arg(index + 1)
                             .arg(outcome.elapsedMs);
            }
            return false;
        }
        if (outcome.record.exitCode != 0) {
            if (error != nullptr) {
                *error = QStringLiteral("server stop cycle %1 returned exit code %2")
                             .arg(index + 1)
                             .arg(outcome.record.exitCode);
            }
            return false;
        }
    }
    return true;
}

static bool runClientStopCycle(int port, int timeoutMs, int stopDelayMs, int iterations, int warmupMs, QString *error)
{
    for (int index = 0; index < iterations; ++index) {
        bool cycleOk = false;
        QString cycleError;

        for (int attempt = 0; attempt < 2 && !cycleOk; ++attempt) {
            QProcess server;
            QString serverDiagnostics;
            const int cyclePort = port + index;

            writeLine(QStringLiteral("CLIENT_STOP cycle %1: start server").arg(index + 1));
            if (!startServerChild(cyclePort, timeoutMs, warmupMs, &server, &serverDiagnostics)) {
                cycleError = QStringLiteral("client stop cycle %1 could not start server: %2")
                                 .arg(index + 1)
                                 .arg(serverDiagnostics.isEmpty() ? QStringLiteral("unknown error") : serverDiagnostics);
                if (attempt == 0) {
                    writeLine(QStringLiteral("CLIENT_STOP cycle %1: retry after server start failure").arg(index + 1));
                    QThread::msleep(100);
                    continue;
                }
                break;
            }

            writeLine(QStringLiteral("CLIENT_STOP cycle %1: start client").arg(index + 1));
            QProcess client;
            QString clientDiagnostics;
            if (!startClientChild(cyclePort, 60, stopDelayMs, timeoutMs, warmupMs, &client, &clientDiagnostics)) {
                cycleError = QStringLiteral("client stop cycle %1 could not start client: %2")
                                 .arg(index + 1)
                                 .arg(clientDiagnostics.isEmpty() ? QStringLiteral("unknown error") : clientDiagnostics);
                if (server.state() != QProcess::NotRunning) {
                    server.kill();
                    server.waitForFinished(5000);
                }
                if (attempt == 0) {
                    writeLine(QStringLiteral("CLIENT_STOP cycle %1: retry after client start failure").arg(index + 1));
                    QThread::msleep(100);
                    continue;
                }
                break;
            }

            if (!client.waitForFinished(timeoutMs)) {
                client.kill();
                client.waitForFinished(5000);
                cycleError = QStringLiteral("client stop cycle %1 timed out").arg(index + 1);
                if (server.state() != QProcess::NotRunning) {
                    server.kill();
                    server.waitForFinished(5000);
                }
                if (attempt == 0) {
                    writeLine(QStringLiteral("CLIENT_STOP cycle %1: retry after client timeout").arg(index + 1));
                    QThread::msleep(100);
                    continue;
                }
                break;
            }

            const QString clientOutput = QString::fromUtf8(client.readAllStandardOutput()).trimmed();
            if (client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0) {
                cycleError = QStringLiteral("client stop cycle %1 failed (%2): %3")
                                 .arg(index + 1)
                                 .arg(describeProcessTermination(client))
                                 .arg(clientOutput.isEmpty() ? QStringLiteral("unknown client error") : clientOutput);
                if (server.state() != QProcess::NotRunning) {
                    server.kill();
                    server.waitForFinished(5000);
                }
                if (attempt == 0) {
                    writeLine(QStringLiteral("CLIENT_STOP cycle %1: retry after client failure").arg(index + 1));
                    QThread::msleep(100);
                    continue;
                }
                break;
            }

            if (server.state() != QProcess::NotRunning && !server.waitForFinished(qMin(timeoutMs, 5000))) {
                server.terminate();
                if (!server.waitForFinished(5000)) {
                    server.kill();
                    server.waitForFinished(5000);
                }
            }

            const QString serverOutput = QString::fromUtf8(server.readAllStandardOutput()).trimmed();
            if (server.exitStatus() != QProcess::NormalExit) {
                writeLine(QStringLiteral("CLIENT_STOP cycle %1: server exited abnormally%2")
                          .arg(index + 1)
                          .arg(serverOutput.isEmpty() ? QString() : QStringLiteral(": %1").arg(serverOutput)));
            } else if (server.exitCode() != 0) {
                writeLine(QStringLiteral("CLIENT_STOP cycle %1: server exited with code %2")
                          .arg(index + 1)
                          .arg(server.exitCode()));
            }

            cycleOk = true;
        }

        if (!cycleOk) {
            if (error != nullptr) {
                *error = cycleError.isEmpty()
                    ? QStringLiteral("client stop cycle %1 failed").arg(index + 1)
                    : cycleError;
            }
            return false;
        }

        QThread::msleep(50);

    }

    return true;
}

static bool runEndToEnd(int port, int timeoutMs, int warmupMs, QString *error)
{
    writeLine(QStringLiteral("END_TO_END: start server"));
    QProcess server;
    QString serverDiagnostics;
    if (!startServerChild(port, timeoutMs, warmupMs, &server, &serverDiagnostics)) {
        if (error != nullptr) {
            *error = QStringLiteral("end-to-end server failed: %1")
                         .arg(serverDiagnostics.isEmpty() ? QStringLiteral("unknown error") : serverDiagnostics);
        }
        return false;
    }

    writeLine(QStringLiteral("END_TO_END: start client"));
    QProcess client;
    QString clientDiagnostics;
    if (!startClientChild(port, 2, -1, timeoutMs, warmupMs, &client, &clientDiagnostics)) {
        if (error != nullptr) {
            *error = QStringLiteral("end-to-end client failed to start: %1")
                         .arg(clientDiagnostics.isEmpty() ? QStringLiteral("unknown error") : clientDiagnostics);
        }
        if (server.state() != QProcess::NotRunning) {
            server.kill();
            server.waitForFinished(5000);
        }
        return false;
    }

    if (!client.waitForFinished(timeoutMs)) {
        client.kill();
        client.waitForFinished(5000);
        if (error != nullptr) {
            *error = QStringLiteral("end-to-end client timed out");
        }
        if (server.state() != QProcess::NotRunning) {
            server.kill();
            server.waitForFinished(5000);
        }
        return false;
    }
    const QString clientOutput = QString::fromUtf8(client.readAllStandardOutput());
    writeLine(QStringLiteral("END_TO_END: client finished"));
    if (client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("end-to-end client returned exit code %1")
                         .arg(client.exitCode());
        }
        if (server.state() != QProcess::NotRunning) {
            server.kill();
            server.waitForFinished(5000);
        }
        return false;
    }
    if (clientOutput.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("end-to-end client produced no output");
        }
        if (server.state() != QProcess::NotRunning) {
            server.kill();
            server.waitForFinished(5000);
        }
        return false;
    }

    const QString serverOutput = QString::fromUtf8(server.readAllStandardOutput());
    if (server.exitStatus() != QProcess::NormalExit || server.exitCode() != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("server child did not exit cleanly: %1")
                         .arg(serverOutput.isEmpty() ? QStringLiteral("unknown server output") : serverOutput.trimmed());
        }
        return false;
    }

    if (error != nullptr) {
        *error = QStringLiteral("%1 / %2")
                     .arg(QStringLiteral("client child completed"),
                          QString::number(timeoutMs));
    }
    return true;
}

static bool runValidationMatrix(int port, int timeoutMs, int warmupMs, QString *error)
{
    QString stepError;

    writeLine(QStringLiteral("VALIDATION: tcp end-to-end"));
    if (!runEndToEnd(port, timeoutMs, warmupMs, &stepError)) {
        if (error != nullptr) {
            *error = QStringLiteral("tcp end-to-end failed: %1").arg(stepError);
        }
        return false;
    }

    writeLine(QStringLiteral("VALIDATION: server restart after one-off"));
    {
        QProcess server;
        QString serverDiagnostics;
        if (!startServerChild(port + 1, timeoutMs, warmupMs, &server, &serverDiagnostics)) {
            if (error != nullptr) {
                *error = QStringLiteral("server restart prep failed: %1")
                             .arg(serverDiagnostics.isEmpty() ? QStringLiteral("unknown error") : serverDiagnostics);
            }
            return false;
        }
        QProcess client;
        QString clientDiagnostics;
        if (!startClientChild(port + 1, 2, -1, timeoutMs, warmupMs, &client, &clientDiagnostics)) {
            if (error != nullptr) {
                *error = QStringLiteral("server restart client failed: %1")
                             .arg(clientDiagnostics.isEmpty() ? QStringLiteral("unknown error") : clientDiagnostics);
            }
            if (server.state() != QProcess::NotRunning) {
                server.kill();
                server.waitForFinished(5000);
            }
            return false;
        }
        if (!client.waitForFinished(timeoutMs)) {
            client.kill();
            client.waitForFinished(5000);
            if (error != nullptr) {
                *error = QStringLiteral("server restart client timed out");
            }
            if (server.state() != QProcess::NotRunning) {
                server.kill();
                server.waitForFinished(5000);
            }
            return false;
        }
        if (server.state() != QProcess::NotRunning) {
            server.waitForFinished(qMin(timeoutMs, 5000));
        }
        if (server.exitStatus() != QProcess::NormalExit || server.exitCode() != 0) {
            if (error != nullptr) {
                *error = QStringLiteral("server did not restart cleanly after one-off test");
            }
            return false;
        }
        QProcess restart;
        if (!startServerChild(port + 1, timeoutMs, warmupMs, &restart, &serverDiagnostics)) {
            if (error != nullptr) {
                *error = QStringLiteral("server restart second bind failed: %1")
                             .arg(serverDiagnostics.isEmpty() ? QStringLiteral("unknown error") : serverDiagnostics);
            }
            return false;
        }
        if (restart.state() != QProcess::NotRunning) {
            restart.kill();
            restart.waitForFinished(5000);
        }
    }

    writeLine(QStringLiteral("VALIDATION: invalid host"));
    {
        IperfGuiConfig cfg;
        cfg.mode = IperfGuiConfig::Mode::Client;
        cfg.protocol = IperfGuiConfig::Protocol::Tcp;
        cfg.family = IperfGuiConfig::AddressFamily::IPv4;
        cfg.host = QStringLiteral("no-such-host.invalid");
        cfg.port = port + 2;
        cfg.duration = 2;
        cfg.parallel = 1;
        cfg.jsonStream = true;
        cfg.jsonStreamFullOutput = true;
        if (!runBridgeExpectFailure(cfg, timeoutMs, &stepError)) {
            if (error != nullptr) {
                *error = QStringLiteral("invalid host check failed: %1").arg(stepError);
            }
            return false;
        }
    }

    writeLine(QStringLiteral("VALIDATION: invalid bind address"));
    {
        IperfGuiConfig cfg = makeClientConfig(port + 3, 2);
        cfg.bindAddress = QStringLiteral("999.999.999.999");
        if (!runBridgeExpectFailure(cfg, timeoutMs, &stepError)) {
            if (error != nullptr) {
                *error = QStringLiteral("invalid bind address check failed: %1").arg(stepError);
            }
            return false;
        }
    }

    writeLine(QStringLiteral("VALIDATION: IPv6 mismatch"));
    {
        IperfGuiConfig cfg = makeClientConfig(port + 4, 2);
        cfg.host = QStringLiteral("::1");
        cfg.family = IperfGuiConfig::AddressFamily::IPv4;
        if (!runBridgeExpectFailure(cfg, timeoutMs, &stepError)) {
            if (error != nullptr) {
                *error = QStringLiteral("family mismatch check failed: %1").arg(stepError);
            }
            return false;
        }
    }

    writeLine(QStringLiteral("VALIDATION: source NIC family mismatch"));
    {
        IperfGuiConfig cfg = makeClientConfig(port + 4, 2);
        cfg.host = QStringLiteral("127.0.0.1");
        cfg.family = IperfGuiConfig::AddressFamily::IPv4;
        cfg.bindAddress = QStringLiteral("::1");
        cfg.connectTimeoutMs = 2000;
        if (!runBridgeExpectFailure(cfg, timeoutMs, &stepError)) {
            if (error != nullptr) {
                *error = QStringLiteral("source NIC family mismatch check failed: %1").arg(stepError);
            }
            return false;
        }
    }

    writeLine(QStringLiteral("VALIDATION: stop during connect"));
    {
        if (!runBridgeStopDuringConnect(port + 7, timeoutMs, &stepError)) {
            if (error != nullptr) {
                *error = QStringLiteral("stop during connect check failed: %1").arg(stepError);
            }
            return false;
        }
    }

    writeLine(QStringLiteral("VALIDATION: server port unavailable"));
    {
        QProcess server;
        QString serverDiagnostics;
        const int busyPort = port + 5;
        const QString busyBind = QStringLiteral("127.0.0.1");
        if (!startServerChild(busyPort, timeoutMs, warmupMs, &server, &serverDiagnostics,
                              IperfGuiConfig::Protocol::Tcp, busyBind, busyBind)) {
            if (error != nullptr) {
                *error = QStringLiteral("busy-port setup failed: %1")
                             .arg(serverDiagnostics.isEmpty() ? QStringLiteral("unknown error") : serverDiagnostics);
            }
            return false;
        }

        QProcess contender;
        QString contenderDiagnostics;
        if (startServerChild(busyPort, qMin(timeoutMs, 5000), warmupMs, &contender, &contenderDiagnostics,
                              IperfGuiConfig::Protocol::Tcp, busyBind, busyBind)) {
            writeLine(QStringLiteral("VALIDATION: busy-port reuse allowed on this host, treating as informational"));
            if (contender.state() != QProcess::NotRunning) {
                contender.kill();
                contender.waitForFinished(5000);
            }
        }

        if (server.state() != QProcess::NotRunning) {
            server.kill();
            server.waitForFinished(5000);
        }
    }

    writeLine(QStringLiteral("VALIDATION: udp end-to-end"));
    {
        const int udpPort = port + 6;
        QProcess server;
        QString serverDiagnostics;
        if (!startServerChild(udpPort, timeoutMs, warmupMs, &server, &serverDiagnostics, IperfGuiConfig::Protocol::Udp)) {
            if (error != nullptr) {
                *error = QStringLiteral("udp server setup failed: %1")
                             .arg(serverDiagnostics.isEmpty() ? QStringLiteral("unknown error") : serverDiagnostics);
            }
            return false;
        }
        QProcess client;
        QString clientDiagnostics;
        if (!startClientChild(udpPort, 2, -1, timeoutMs, warmupMs, &client, &clientDiagnostics,
                              IperfGuiConfig::Protocol::Udp, 10ULL * 1000ULL * 1000ULL)) {
            if (error != nullptr) {
                *error = QStringLiteral("udp client setup failed: %1")
                             .arg(clientDiagnostics.isEmpty() ? QStringLiteral("unknown error") : clientDiagnostics);
            }
            if (server.state() != QProcess::NotRunning) {
                server.kill();
                server.waitForFinished(5000);
            }
            return false;
        }
        if (!client.waitForFinished(timeoutMs)) {
            client.kill();
            client.waitForFinished(5000);
            if (error != nullptr) {
                *error = QStringLiteral("udp client timed out");
            }
            if (server.state() != QProcess::NotRunning) {
                server.kill();
                server.waitForFinished(5000);
            }
            return false;
        }
        if (server.state() != QProcess::NotRunning) {
            server.waitForFinished(qMin(timeoutMs, 5000));
        }
        if (server.exitStatus() != QProcess::NormalExit || server.exitCode() != 0) {
            if (error != nullptr) {
                *error = QStringLiteral("udp server did not exit cleanly");
            }
            return false;
        }
    }

    if (error != nullptr) {
        *error = QStringLiteral("validation matrix completed");
    }
    return true;
}

static void printUsage(const QCommandLineParser &parser)
{
    QTextStream stream(stdout);
    stream << parser.helpText();
    stream.flush();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("IperfSmoke"));
    QCoreApplication::setOrganizationName(QStringLiteral("iperf"));

    registerGuiTypes();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Headless smoke tests for IperfWin"));
    parser.addHelpOption();

    const QCommandLineOption serverChildOption(QStringList() << QStringLiteral("server-child"),
                                               QStringLiteral("Run as a server child process for smoke tests."));
    const QCommandLineOption clientChildOption(QStringList() << QStringLiteral("client-child"),
                                               QStringLiteral("Run as a client child process for smoke tests."));
    const QCommandLineOption endToEndOption(QStringList() << QStringLiteral("end-to-end"),
                                            QStringLiteral("Run the client/server end-to-end smoke test."));
    const QCommandLineOption stopCycleOption(QStringList() << QStringLiteral("stop-cycle"),
                                             QStringLiteral("Run both stop-cycle smoke tests."));
    const QCommandLineOption jsonParserOption(QStringList() << QStringLiteral("json-parser-selftest"),
                                              QStringLiteral("Run JSON parser self-tests."));
    const QCommandLineOption validationMatrixOption(QStringList() << QStringLiteral("validation-matrix"),
                                                    QStringLiteral("Run the validation matrix smoke tests."));
    const QCommandLineOption serverStopOption(QStringList() << QStringLiteral("server-stop"),
                                              QStringLiteral("Run the server stop smoke test."));
    const QCommandLineOption clientStopOption(QStringList() << QStringLiteral("client-stop"),
                                              QStringLiteral("Run the client stop smoke test."));
    const QCommandLineOption portOption(QStringList() << QStringLiteral("port"),
                                        QStringLiteral("Base port for the smoke test."),
                                        QStringLiteral("port"),
                                        QStringLiteral("5210"));
    const QCommandLineOption timeoutOption(QStringList() << QStringLiteral("timeout-ms"),
                                           QStringLiteral("Timeout for a smoke test session."),
                                           QStringLiteral("timeout-ms"),
                                           QStringLiteral("15000"));
    const QCommandLineOption warmupOption(QStringList() << QStringLiteral("warmup-ms"),
                                          QStringLiteral("Delay after server READY before connecting."),
                                          QStringLiteral("warmup-ms"),
                                          QStringLiteral("500"));
    const QCommandLineOption durationOption(QStringList() << QStringLiteral("duration"),
                                            QStringLiteral("Client duration in seconds."),
                                            QStringLiteral("duration"),
                                            QStringLiteral("2"));
    const QCommandLineOption protocolOption(QStringList() << QStringLiteral("protocol"),
                                            QStringLiteral("Protocol for child and matrix runs (tcp|udp)."),
                                            QStringLiteral("protocol"),
                                            QStringLiteral("tcp"));
    const QCommandLineOption bitrateOption(QStringList() << QStringLiteral("bitrate-bps"),
                                           QStringLiteral("UDP bitrate for child and matrix runs."),
                                           QStringLiteral("bitrate-bps"),
                                           QStringLiteral("10000000"));
    const QCommandLineOption bindAddressOption(QStringList() << QStringLiteral("bind-address"),
                                               QStringLiteral("Bind address for child and matrix runs."),
                                               QStringLiteral("bind-address"));
    const QCommandLineOption listenAddressOption(QStringList() << QStringLiteral("listen-address"),
                                                 QStringLiteral("Listen address for server-child matrix runs."),
                                                 QStringLiteral("listen-address"));
    const QCommandLineOption stopAfterOption(QStringList() << QStringLiteral("stop-after-ms"),
                                             QStringLiteral("Stop the client after this many milliseconds."),
                                             QStringLiteral("stop-after-ms"),
                                             QStringLiteral("-1"));
    const QCommandLineOption iterationsOption(QStringList() << QStringLiteral("iterations"),
                                               QStringLiteral("Number of repeated cycles."),
                                               QStringLiteral("iterations"),
                                               QStringLiteral("20"));
    const QCommandLineOption stopDelayOption(QStringList() << QStringLiteral("stop-delay-ms"),
                                             QStringLiteral("Delay before requesting stop."),
                                             QStringLiteral("stop-delay-ms"),
                                             QStringLiteral("250"));

    parser.addOption(serverChildOption);
    parser.addOption(clientChildOption);
    parser.addOption(endToEndOption);
    parser.addOption(stopCycleOption);
    parser.addOption(jsonParserOption);
    parser.addOption(validationMatrixOption);
    parser.addOption(serverStopOption);
    parser.addOption(clientStopOption);
    parser.addOption(portOption);
    parser.addOption(timeoutOption);
    parser.addOption(warmupOption);
    parser.addOption(durationOption);
    parser.addOption(protocolOption);
    parser.addOption(bitrateOption);
    parser.addOption(bindAddressOption);
    parser.addOption(listenAddressOption);
    parser.addOption(stopAfterOption);
    parser.addOption(iterationsOption);
    parser.addOption(stopDelayOption);
    parser.process(app);

    const int port = parser.value(portOption).toInt();
    const int timeoutMs = parser.value(timeoutOption).toInt();
    const int warmupMs = parser.value(warmupOption).toInt();
    const int durationSeconds = parser.value(durationOption).toInt();
    const QString protocolText = parser.value(protocolOption).trimmed().toLower();
    const IperfGuiConfig::Protocol childProtocol = protocolText == QStringLiteral("udp")
        ? IperfGuiConfig::Protocol::Udp
        : IperfGuiConfig::Protocol::Tcp;
    const quint64 bitrateBps = parser.value(bitrateOption).toULongLong();
    const QString bindAddress = parser.value(bindAddressOption).trimmed();
    const QString listenAddress = parser.value(listenAddressOption).trimmed();
    const int stopAfterMs = parser.value(stopAfterOption).toInt();
    const int iterations = qMax(1, parser.value(iterationsOption).toInt());
    const int stopDelayMs = qMax(0, parser.value(stopDelayOption).toInt());

    if (parser.isSet(serverChildOption)) {
        IperfCoreBridge bridge;
        IperfGuiConfig config = makeServerConfig(port, childProtocol);
        if (!listenAddress.isEmpty()) {
            config.listenAddress = listenAddress;
        }
        if (!bindAddress.isEmpty()) {
            config.bindAddress = bindAddress;
        }
        bridge.setConfiguration(config);

        QObject::connect(&bridge, &IperfCoreBridge::runningChanged, &app, [](bool running) {
            if (running) {
                writeLine(QStringLiteral("READY"));
            }
        });
        QObject::connect(&bridge, &IperfCoreBridge::errorOccurred, &app, [](const QString &message) {
            writeLine(QStringLiteral("ERROR %1").arg(message));
            QCoreApplication::exit(2);
        });
        QObject::connect(&bridge, &IperfCoreBridge::sessionCompleted, &app, [](const IperfSessionRecord &record) {
            writeLine(QStringLiteral("DONE %1 %2").arg(record.exitCode).arg(record.statusText));
            QCoreApplication::exit(record.exitCode == 0 ? 0 : 3);
        });

        bridge.start();
        return app.exec();
    }

    if (parser.isSet(clientChildOption)) {
        IperfCoreBridge bridge;
        IperfGuiConfig config = makeClientConfig(port, durationSeconds, childProtocol, bitrateBps);
        if (!bindAddress.isEmpty()) {
            config.bindAddress = bindAddress;
        }
        bridge.setConfiguration(config);

        QTimer stopTimer;
        stopTimer.setSingleShot(true);
        bool stopArmed = false;

        QObject::connect(&bridge, &IperfCoreBridge::errorOccurred, &app, [](const QString &message) {
            writeLine(QStringLiteral("ERROR %1").arg(message));
            QCoreApplication::exit(2);
        });
        QObject::connect(&bridge, &IperfCoreBridge::runningChanged, &app, [&](bool running) {
            if (running) {
                writeLine(QStringLiteral("READY"));
            }
        });
        QObject::connect(&bridge, &IperfCoreBridge::eventReceived, &app, [&](const IperfGuiEvent &event) {
            if (!stopArmed && stopAfterMs >= 0 && event.kind == IperfEventKind::Interval) {
                stopArmed = true;
                stopTimer.start(stopAfterMs);
            }
        });
        QObject::connect(&stopTimer, &QTimer::timeout, &bridge, &IperfCoreBridge::stop);
        QObject::connect(&bridge, &IperfCoreBridge::sessionCompleted, &app, [](const IperfSessionRecord &record) {
            writeLine(QStringLiteral("DONE %1 %2").arg(record.exitCode).arg(record.statusText));
            QCoreApplication::exit(record.exitCode == 0 ? 0 : 3);
        });

        bridge.start();
        return app.exec();
    }

    QString errorMessage;
    bool ok = false;
    if (parser.isSet(endToEndOption)) {
        ok = runEndToEnd(port, timeoutMs, warmupMs, &errorMessage);
    } else if (parser.isSet(validationMatrixOption)) {
        ok = runValidationMatrix(port, timeoutMs, warmupMs, &errorMessage);
    } else if (parser.isSet(stopCycleOption)) {
        ok = runServerStopCycle(port, timeoutMs, stopDelayMs, iterations, &errorMessage);
        if (ok) {
            ok = runClientStopCycle(port, timeoutMs, stopDelayMs, iterations, warmupMs, &errorMessage);
        }
    } else if (parser.isSet(jsonParserOption)) {
        ok = runJsonParserSelfTest(&errorMessage);
    } else if (parser.isSet(serverStopOption)) {
        ok = runServerStopCycle(port, timeoutMs, stopDelayMs, iterations, &errorMessage);
    } else if (parser.isSet(clientStopOption)) {
        ok = runClientStopCycle(port, timeoutMs, stopDelayMs, iterations, warmupMs, &errorMessage);
    } else {
        printUsage(parser);
        return 0;
    }

    if (!ok) {
        writeLine(QStringLiteral("FAIL %1").arg(errorMessage));
        return 1;
    }

    writeLine(QStringLiteral("PASS %1").arg(errorMessage));
    return 0;
}
