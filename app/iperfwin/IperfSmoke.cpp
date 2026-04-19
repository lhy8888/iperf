#include "IperfCoreBridge.h"
#include "IperfJsonParser.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
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

static IperfGuiConfig makeServerConfig(int port)
{
    IperfGuiConfig config;
    config.mode = IperfGuiConfig::Mode::Server;
    config.protocol = IperfGuiConfig::Protocol::Tcp;
    config.family = IperfGuiConfig::AddressFamily::IPv4;
    config.port = port;
    config.oneOff = true;
    config.jsonStream = true;
    config.jsonStreamFullOutput = true;
    config.getServerOutput = true;
    config.parallel = 1;
    config.duration = 0;
    return config;
}

static IperfGuiConfig makeClientConfig(int port, int durationSeconds)
{
    IperfGuiConfig config;
    config.mode = IperfGuiConfig::Mode::Client;
    config.protocol = IperfGuiConfig::Protocol::Tcp;
    config.family = IperfGuiConfig::AddressFamily::IPv4;
    config.host = QStringLiteral("127.0.0.1");
    config.port = port;
    config.duration = durationSeconds;
    config.parallel = 1;
    config.jsonStream = true;
    config.jsonStreamFullOutput = true;
    config.getServerOutput = true;
    return config;
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

static bool startServerChild(int port, int readyTimeoutMs, int warmupMs, QProcess *process, QString *error)
{
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProgram(QCoreApplication::applicationFilePath());
    process->setArguments({
        QStringLiteral("--server-child"),
        QStringLiteral("--port"),
        QString::number(port),
    });
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

static bool startClientChild(int port, int durationSeconds, int stopAfterMs, int readyTimeoutMs, int warmupMs, QProcess *process, QString *error)
{
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProgram(QCoreApplication::applicationFilePath());
    QStringList args = {
        QStringLiteral("--client-child"),
        QStringLiteral("--port"),
        QString::number(port),
        QStringLiteral("--duration"),
        QString::number(durationSeconds),
    };
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
        writeLine(QStringLiteral("CLIENT_STOP cycle %1: start server").arg(index + 1));
        QProcess server;
        QString serverDiagnostics;
        const int cyclePort = port + index;
        if (!startServerChild(cyclePort, timeoutMs, warmupMs, &server, &serverDiagnostics)) {
            if (error != nullptr) {
                *error = QStringLiteral("client stop cycle %1 could not start server: %2")
                             .arg(index + 1)
                             .arg(serverDiagnostics.isEmpty() ? QStringLiteral("unknown error") : serverDiagnostics);
            }
            return false;
        }

        writeLine(QStringLiteral("CLIENT_STOP cycle %1: start client").arg(index + 1));
        QProcess client;
        QString clientDiagnostics;
        if (!startClientChild(cyclePort, 60, stopDelayMs, timeoutMs, warmupMs, &client, &clientDiagnostics)) {
            if (error != nullptr) {
                *error = QStringLiteral("client stop cycle %1 could not start client: %2")
                             .arg(index + 1)
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
                *error = QStringLiteral("client stop cycle %1 timed out").arg(index + 1);
            }
            if (server.state() != QProcess::NotRunning) {
                server.kill();
                server.waitForFinished(5000);
            }
            return false;
        }
        const QString clientOutput = QString::fromUtf8(client.readAllStandardOutput());
        if (client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0) {
            if (error != nullptr) {
                *error = QStringLiteral("client stop cycle %1 failed: %2")
                             .arg(index + 1)
                             .arg(clientOutput.isEmpty() ? QStringLiteral("unknown client error") : clientOutput.trimmed());
            }
            if (server.state() != QProcess::NotRunning) {
                server.kill();
                server.waitForFinished(5000);
            }
            return false;
        }

        if (server.state() != QProcess::NotRunning && !server.waitForFinished(qMin(timeoutMs, 5000))) {
            server.terminate();
            if (!server.waitForFinished(5000)) {
                server.kill();
                server.waitForFinished(5000);
            }
        }

        const QString serverOutput = QString::fromUtf8(server.readAllStandardOutput());
        if (server.exitStatus() != QProcess::NormalExit) {
            writeLine(QStringLiteral("CLIENT_STOP cycle %1: server exited abnormally%2")
                      .arg(index + 1)
                      .arg(serverOutput.isEmpty() ? QString() : QStringLiteral(": %1").arg(serverOutput.trimmed())));
        } else if (server.exitCode() != 0) {
            writeLine(QStringLiteral("CLIENT_STOP cycle %1: server exited with code %2")
                      .arg(index + 1)
                      .arg(server.exitCode()));
        }

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
    parser.addOption(serverStopOption);
    parser.addOption(clientStopOption);
    parser.addOption(portOption);
    parser.addOption(timeoutOption);
    parser.addOption(warmupOption);
    parser.addOption(durationOption);
    parser.addOption(stopAfterOption);
    parser.addOption(iterationsOption);
    parser.addOption(stopDelayOption);
    parser.process(app);

    const int port = parser.value(portOption).toInt();
    const int timeoutMs = parser.value(timeoutOption).toInt();
    const int warmupMs = parser.value(warmupOption).toInt();
    const int durationSeconds = parser.value(durationOption).toInt();
    const int stopAfterMs = parser.value(stopAfterOption).toInt();
    const int iterations = qMax(1, parser.value(iterationsOption).toInt());
    const int stopDelayMs = qMax(0, parser.value(stopDelayOption).toInt());

    if (parser.isSet(serverChildOption)) {
        IperfCoreBridge bridge;
        bridge.setConfiguration(makeServerConfig(port));

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
        bridge.setConfiguration(makeClientConfig(port, durationSeconds));

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
