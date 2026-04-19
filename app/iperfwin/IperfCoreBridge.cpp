#include "IperfCoreBridge.h"

#include "IperfJsonParser.h"

#include "iperf.h"
#include "iperf_api.h"
#include "platform/win/socket_compat.h"
#include "platform/win/win_runtime.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QStringList>
#include <QThread>
#include <QtGlobal>
#include <QVariant>

#include <setjmp.h>
#include <cstdio>
#include <vector>

extern "C" {
extern jmp_buf env;
extern int iperf_exit_jump_ready;
}

namespace {

static bool bridgeTraceEnabled();
static void bridgeTrace(const char *step);

class IperfSessionRunner : public QThread
{
public:
    enum class Kind {
        Client,
        Server
    };

    IperfSessionRunner(IperfCoreBridge *bridge, iperf_test *test, Kind kind)
        : m_bridge(bridge)
        , m_test(test)
        , m_kind(kind)
    {
    }

    void run() override
    {
        int rc = 0;
        int jumpResult = 0;

        bridgeTrace("runner(begin)");
        iperf_exit_jump_ready = 1;
        jumpResult = setjmp(env);
        if (jumpResult == 0) {
            if (m_kind == Kind::Client) {
                bridgeTrace("runner(before client)");
                rc = iperf_run_client(m_test);
                bridgeTrace("runner(after client)");
            } else {
                bridgeTrace("runner(before server)");
                rc = iperf_run_server(m_test);
                bridgeTrace("runner(after server)");
            }
        } else {
            bridgeTrace("runner(longjmp)");
            rc = -1;
        }
        iperf_exit_jump_ready = 0;
        bridgeTrace("runner(end)");

        if (m_bridge != nullptr) {
            QMetaObject::invokeMethod(m_bridge, [bridge = m_bridge, rc]() {
                if (bridge != nullptr) {
                    bridge->finishSessionOnGuiThread(rc);
                }
            }, Qt::QueuedConnection);
        }
    }

private:
    IperfCoreBridge *m_bridge = nullptr;
    iperf_test *m_test = nullptr;
    Kind m_kind;
};

static QHash<const iperf_test *, IperfCoreBridge *> s_bridgeByTest;
static QMutex s_bridgeMutex;

static char *dupUtf8(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    if (utf8.isEmpty()) {
        return nullptr;
    }
#ifdef _WIN32
    return _strdup(utf8.constData());
#else
    return strdup(utf8.constData());
#endif
}

static QString jsonKindName(IperfEventKind kind)
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

static int addressFamilyForConfig(IperfGuiConfig::AddressFamily family)
{
    switch (family) {
    case IperfGuiConfig::AddressFamily::IPv4:
        return AF_INET;
    case IperfGuiConfig::AddressFamily::IPv6:
        return AF_INET6;
    case IperfGuiConfig::AddressFamily::Any:
    default:
        return AF_UNSPEC;
    }
}

static bool isServerMode(const IperfGuiConfig &config)
{
    return config.mode == IperfGuiConfig::Mode::Server;
}

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

static bool bridgeTraceEnabled()
{
    return qEnvironmentVariableIsSet("IPERF_TRACE_BRIDGE");
}

static void bridgeTrace(const char *step)
{
    if (!bridgeTraceEnabled()) {
        return;
    }
    fprintf(stderr, "[IperfCoreBridge] %s\n", step);
    fflush(stderr);
}

} // namespace

IperfCoreBridge::IperfCoreBridge(QObject *parent)
    : QObject(parent)
    , m_sink(new IperfJsonSink(this))
    , m_nullOut(openNullDevice())
    , m_networkReady(win_net_acquire() == 0)
{
    connect(m_sink, &IperfJsonSink::eventParsed,
            this, &IperfCoreBridge::handleParsedEvent);

    m_currentSession.startedAt = QDateTime::currentDateTime();
    m_statusText = m_networkReady ? QStringLiteral("Idle") : QStringLiteral("Winsock unavailable");

    if (!m_networkReady) {
        emit errorOccurred(QStringLiteral("Failed to initialize Winsock"));
    }
}

IperfCoreBridge::~IperfCoreBridge()
{
    stop();

    if (m_runner != nullptr) {
        m_runner->wait(5000);
        delete m_runner;
        m_runner = nullptr;
    }

    cleanupTest();

    if (m_nullOut != nullptr && m_nullOut != stdout) {
        fclose(m_nullOut);
        m_nullOut = nullptr;
    }

    if (m_networkReady) {
        win_net_release();
        m_networkReady = false;
    }
}

void
IperfCoreBridge::setConfiguration(const IperfGuiConfig &config)
{
    QMutexLocker locker(&m_mutex);
    m_config = config;
    emit configurationChanged(m_config);
}

IperfGuiConfig
IperfCoreBridge::configuration() const
{
    QMutexLocker locker(&m_mutex);
    return m_config;
}

bool
IperfCoreBridge::isRunning() const
{
    QMutexLocker locker(&m_mutex);
    return m_running;
}

QString
IperfCoreBridge::statusText() const
{
    QMutexLocker locker(&m_mutex);
    return m_statusText;
}

IperfSessionRecord
IperfCoreBridge::currentSession() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentSession;
}

QVector<IperfSessionRecord>
IperfCoreBridge::history() const
{
    QMutexLocker locker(&m_mutex);
    return m_history;
}

void
IperfCoreBridge::clearHistory()
{
    QMutexLocker locker(&m_mutex);
    m_history.clear();
}

void
IperfCoreBridge::start()
{
    bridgeTrace("start(begin)");
    IperfGuiConfig config;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_networkReady) {
            emit errorOccurred(QStringLiteral("Winsock is not available"));
            return;
        }
        if (m_running) {
            emit stateChanged(QStringLiteral("Already running"));
            return;
        }
        config = m_config;
        m_currentSession = IperfSessionRecord();
        m_currentSession.startedAt = QDateTime::currentDateTime();
        m_currentSession.config = config;
        m_currentSession.statusText = QStringLiteral("Starting");
        m_currentSession.exitCode = 0;
        m_statusText = QStringLiteral("Starting");
        m_running = true;
        m_stopRequested = false;
    }

    QString errorMessage;
    bridgeTrace("start(clear sink)");
    m_sink->clear();
    bridgeTrace("start(create test)");
    m_test = createTest(&errorMessage);
    bridgeTrace("start(after create test)");
    if (m_stopRequested) {
        cleanupTest();
        QMutexLocker locker(&m_mutex);
        m_running = false;
        m_statusText = QStringLiteral("Stopped");
        emit runningChanged(false);
        emit stateChanged(m_statusText);
        return;
    }
    if (m_test == nullptr) {
        QMutexLocker locker(&m_mutex);
        m_statusText = errorMessage.isEmpty() ? QStringLiteral("Unable to create iperf test") : errorMessage;
        m_running = false;
        m_stopRequested = false;
        emit errorOccurred(m_statusText);
        emit stateChanged(m_statusText);
        emit runningChanged(false);
        return;
    }

    bridgeTrace("start(register bridge)");
    registerBridge(m_test);
    iperf_set_test_json_callback(m_test, &IperfCoreBridge::jsonCallbackThunk);

    if (isServerMode(config)) {
        m_runner = new IperfSessionRunner(this, m_test, IperfSessionRunner::Kind::Server);
    } else {
        m_runner = new IperfSessionRunner(this, m_test, IperfSessionRunner::Kind::Client);
    }

    bridgeTrace("start(emit running)");
    emit runningChanged(true);
    emit stateChanged(m_statusText);
    bridgeTrace("start(thread start)");
    m_runner->start();
    bridgeTrace("start(end)");
}

void
IperfCoreBridge::stop()
{
    std::vector<int> socketsToClose;
    int ctrlSocket = -1;
    int listenerSocket = -1;
    int protListenerSocket = -1;
    QString statusText;
    bool shouldEmitState = false;

    {
        QMutexLocker locker(&m_mutex);
        if (!m_running) {
            return;
        }

        m_stopRequested = true;
        m_statusText = QStringLiteral("Stopping");
        m_currentSession.statusText = m_statusText;
        statusText = m_statusText;
        shouldEmitState = true;

        if (m_test == nullptr) {
            // Nothing to wake up yet, but the caller still expects the stop state to surface.
        } else {
            m_test->done = 1;

            if (m_test->ctrl_sck >= 0) {
                ctrlSocket = m_test->ctrl_sck;
                m_test->ctrl_sck = -1;
            }

            if (m_test->listener >= 0) {
                listenerSocket = m_test->listener;
                m_test->listener = -1;
            }

            if (m_test->prot_listener >= 0) {
                protListenerSocket = m_test->prot_listener;
                m_test->prot_listener = -1;
            }

            struct iperf_stream *sp;
            SLIST_FOREACH(sp, &m_test->streams, streams) {
                if (sp->socket >= 0) {
                    socketsToClose.push_back(sp->socket);
                    sp->socket = -1;
                }
            }
        }
    }

    auto closeSocket = [](int fd) {
        if (fd >= 0) {
            (void) shutdown(fd, SHUT_RDWR);
            (void) iperf_sock_close(fd);
        }
    };

    closeSocket(ctrlSocket);
    closeSocket(listenerSocket);
    closeSocket(protListenerSocket);
    for (int fd : socketsToClose) {
        closeSocket(fd);
    }

    if (shouldEmitState) {
        emit stateChanged(statusText);
    }
}

struct iperf_test *
IperfCoreBridge::createTest(QString *errorMessage)
{
    bridgeTrace("createTest(begin)");
    struct iperf_test *test = iperf_new_test();
    if (test == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("iperf_new_test() failed");
        }
        bridgeTrace("createTest(iperf_new_test failed)");
        return nullptr;
    }

    bridgeTrace("createTest(defaults)");
    if (iperf_defaults(test) < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("iperf_defaults() failed");
        }
        bridgeTrace("createTest(iperf_defaults failed)");
        iperf_free_test(test);
        return nullptr;
    }
    bridgeTrace("createTest(defaults done)");

    test->outfile = m_nullOut != nullptr ? m_nullOut : stdout;
    iperf_set_verbose(test, 0);
    iperf_set_test_json_output(test, 1);
    iperf_set_test_json_stream(test, 1);
    iperf_set_test_json_stream_full_output(test, 1);

    bridgeTrace("createTest(apply config)");
    applyConfiguration(test);
    bridgeTrace("createTest(end)");

    return test;
}

void
IperfCoreBridge::applyConfiguration(struct iperf_test *test) const
{
    bridgeTrace("applyConfiguration(begin)");
    QMutexLocker locker(&m_mutex);
    const IperfGuiConfig config = m_config;

    bridgeTrace("applyConfiguration(role)");
    test->role = isServerMode(config) ? 's' : 'c';
    if (!test->reverse) {
        if (test->bidirectional)
            test->mode = BIDIRECTIONAL;
        else if (test->role == 'c')
            test->mode = SENDER;
        else if (test->role == 's')
            test->mode = RECEIVER;
    } else {
        if (test->role == 'c')
            test->mode = RECEIVER;
        else if (test->role == 's')
            test->mode = SENDER;
    }
    test->sender_has_retransmits = 0;
    bridgeTrace("applyConfiguration(role done)");
    bridgeTrace("applyConfiguration(protocol)");
    set_protocol(test, config.protocol == IperfGuiConfig::Protocol::Udp ? Pudp : Ptcp);
    bridgeTrace("applyConfiguration(protocol done)");
    bridgeTrace("applyConfiguration(server port)");
    iperf_set_test_server_port(test, config.port);
    bridgeTrace("applyConfiguration(server port done)");
    bridgeTrace("applyConfiguration(bind port)");
    iperf_set_test_bind_port(test, config.bindPort);
    bridgeTrace("applyConfiguration(bind port done)");
    bridgeTrace("applyConfiguration(duration)");
    iperf_set_test_duration(test, qMax(0, config.duration));
    bridgeTrace("applyConfiguration(duration done)");
    bridgeTrace("applyConfiguration(streams)");
    iperf_set_test_num_streams(test, qMax(1, config.parallel));
    bridgeTrace("applyConfiguration(streams done)");
    bridgeTrace("applyConfiguration(blksize)");
    iperf_set_test_blksize(test, config.blockSize > 0 ? config.blockSize : DEFAULT_TCP_BLKSIZE);
    bridgeTrace("applyConfiguration(blksize done)");
    bridgeTrace("applyConfiguration(reporter)");
    iperf_set_test_reporter_interval(test, qMax(0, config.reporterIntervalMs) / 1000.0);
    bridgeTrace("applyConfiguration(reporter done)");
    bridgeTrace("applyConfiguration(stats)");
    iperf_set_test_stats_interval(test, qMax(0, config.statsIntervalMs) / 1000.0);
    bridgeTrace("applyConfiguration(stats done)");
    bridgeTrace("applyConfiguration(pacing)");
    iperf_set_test_pacing_timer(test, qMax(0, config.pacingTimerUs));
    bridgeTrace("applyConfiguration(pacing done)");
    bridgeTrace("applyConfiguration(connect timeout)");
    iperf_set_test_connect_timeout(test, config.connectTimeoutMs);
    bridgeTrace("applyConfiguration(connect timeout done)");
    bridgeTrace("applyConfiguration(rate)");
    iperf_set_test_rate(test, config.bitrateBps);
    bridgeTrace("applyConfiguration(rate done)");
    bridgeTrace("applyConfiguration(reverse)");
    test->reverse = config.reverse ? 1 : 0;
    if (!test->reverse) {
        if (test->role == 'c')
            test->mode = SENDER;
        else if (test->role == 's')
            test->mode = RECEIVER;
    } else {
        if (test->role == 'c')
            test->mode = RECEIVER;
        else if (test->role == 's')
            test->mode = SENDER;
    }
    bridgeTrace("applyConfiguration(reverse done)");
    bridgeTrace("applyConfiguration(bidir)");
    test->bidirectional = config.bidirectional ? 1 : 0;
    if (test->bidirectional)
        test->mode = BIDIRECTIONAL;
    bridgeTrace("applyConfiguration(bidir done)");
    bridgeTrace("applyConfiguration(one off)");
    iperf_set_test_one_off(test, config.oneOff ? 1 : 0);
    bridgeTrace("applyConfiguration(one off done)");
    bridgeTrace("applyConfiguration(no delay)");
    iperf_set_test_no_delay(test, config.noDelay ? 1 : 0);
    bridgeTrace("applyConfiguration(no delay done)");
    bridgeTrace("applyConfiguration(server output)");
    iperf_set_test_get_server_output(test, config.getServerOutput ? 1 : 0);
    bridgeTrace("applyConfiguration(server output done)");
    bridgeTrace("applyConfiguration(udp counters)");
    iperf_set_test_udp_counters_64bit(test, config.udpCounters64Bit ? 1 : 0);
    bridgeTrace("applyConfiguration(udp counters done)");
    bridgeTrace("applyConfiguration(json stream)");
    iperf_set_test_json_stream(test, config.jsonStream ? 1 : 0);
    bridgeTrace("applyConfiguration(json stream done)");
    bridgeTrace("applyConfiguration(json full)");
    iperf_set_test_json_stream_full_output(test, config.jsonStreamFullOutput ? 1 : 0);
    bridgeTrace("applyConfiguration(json full done)");
    bridgeTrace("applyConfiguration(timestamps)");
    iperf_set_test_timestamps(test, config.timestamps ? 1 : 0);
    bridgeTrace("applyConfiguration(timestamps done)");
    bridgeTrace("applyConfiguration(repeating payload)");
    iperf_set_test_repeating_payload(test, config.repeatingPayload ? 1 : 0);
    bridgeTrace("applyConfiguration(repeating payload done)");
    bridgeTrace("applyConfiguration(tos)");
    iperf_set_test_tos(test, config.tos);
    bridgeTrace("applyConfiguration(tos done)");
    bridgeTrace("applyConfiguration(window)");
    iperf_set_test_socket_bufsize(test, config.windowSize > 0 ? config.windowSize : 0);
    bridgeTrace("applyConfiguration(window done)");
    bridgeTrace("applyConfiguration(mss)");
    iperf_set_test_mss(test, config.mss > 0 ? config.mss : 0);
    bridgeTrace("applyConfiguration(mss done)");
    bridgeTrace("applyConfiguration(dont fragment)");
    iperf_set_dont_fragment(test, config.dontFragment ? 1 : 0);
    bridgeTrace("applyConfiguration(dont fragment done)");
    bridgeTrace("applyConfiguration(unit format)");
    iperf_set_test_unit_format(test, 'a');
    bridgeTrace("applyConfiguration(unit format done)");
    bridgeTrace("applyConfiguration(zerocopy)");
    iperf_set_test_zerocopy(test, config.zeroCopy ? 1 : 0);
    bridgeTrace("applyConfiguration(zerocopy done)");
    bridgeTrace("applyConfiguration(forceflush)");
    test->forceflush = config.forceFlush ? 1 : 0;
    bridgeTrace("applyConfiguration(forceflush done)");
    bridgeTrace("applyConfiguration(timestamp format)");
    iperf_set_test_timestamp_format(test, config.timestampFormat.isEmpty() ? TIMESTAMP_FORMAT : config.timestampFormat.toUtf8().constData());
    bridgeTrace("applyConfiguration(timestamp format done)");

    test->settings->domain = addressFamilyForConfig(config.family);
    test->settings->skip_rx_copy = config.skipRxCopy ? 1 : 0;
    test->settings->gso = 0;
    test->settings->gro = 0;

    if (!config.host.isEmpty()) {
        bridgeTrace("applyConfiguration(host)");
        iperf_set_test_server_hostname(test, config.host.toUtf8().constData());
        bridgeTrace("applyConfiguration(host done)");
    }
    if (!config.bindAddress.isEmpty()) {
        bridgeTrace("applyConfiguration(bind address)");
        iperf_set_test_bind_address(test, config.bindAddress.toUtf8().constData());
        bridgeTrace("applyConfiguration(bind address done)");
    }
    if (!config.bindDev.isEmpty()) {
#if CAN_BIND_TO_DEVICE
        iperf_set_test_bind_dev(test, config.bindDev.toUtf8().constData());
#endif
    }
    if (!config.title.isEmpty()) {
        bridgeTrace("applyConfiguration(title)");
        test->title = dupUtf8(config.title);
        bridgeTrace("applyConfiguration(title done)");
    }
    if (!config.extraData.isEmpty()) {
        bridgeTrace("applyConfiguration(extra)");
        iperf_set_test_extra_data(test, config.extraData.toUtf8().constData());
        bridgeTrace("applyConfiguration(extra done)");
    }
    if (!config.congestionControl.isEmpty()) {
        QByteArray congestionControl = config.congestionControl.toUtf8();
        bridgeTrace("applyConfiguration(congestion)");
        iperf_set_test_congestion_control(test, congestionControl.data());
        bridgeTrace("applyConfiguration(congestion done)");
    }

#if defined(HAVE_IPPROTO_MPTCP) && HAVE_IPPROTO_MPTCP
    test->mptcp = config.mptcp ? 1 : 0;
#endif

#if defined(HAVE_SSL) && HAVE_SSL
    iperf_set_test_server_authorized_users(test, config.serverAuthUsers.toUtf8().constData());
    if (!config.clientUsername.isEmpty()) {
        iperf_set_test_client_username(test, config.clientUsername.toUtf8().constData());
    }
    if (!config.clientPassword.isEmpty()) {
        iperf_set_test_client_password(test, config.clientPassword.toUtf8().constData());
    }
    if (!config.clientPublicKey.isEmpty()) {
        iperf_set_test_client_rsa_pubkey_from_file(test, config.clientPublicKey.toUtf8().constData());
    }
    if (!config.serverPrivateKey.isEmpty()) {
        iperf_set_test_server_rsa_privkey_from_file(test, config.serverPrivateKey.toUtf8().constData());
    }
    if (config.usePkcs1Padding) {
        test->use_pkcs1_padding = 1;
    }
#endif
    bridgeTrace("applyConfiguration(end)");
}

void
IperfCoreBridge::registerBridge(struct iperf_test *test)
{
    QMutexLocker locker(&s_bridgeMutex);
    s_bridgeByTest.insert(test, this);
}

void
IperfCoreBridge::unregisterBridge(struct iperf_test *test)
{
    QMutexLocker locker(&s_bridgeMutex);
    s_bridgeByTest.remove(test);
}

IperfCoreBridge *
IperfCoreBridge::bridgeForTest(struct iperf_test *test)
{
    QMutexLocker locker(&s_bridgeMutex);
    return s_bridgeByTest.value(test, nullptr);
}

void
IperfCoreBridge::jsonCallbackThunk(struct iperf_test *test, char *json)
{
    IperfCoreBridge *bridge = bridgeForTest(test);
    if (bridge == nullptr || json == nullptr) {
        return;
    }

    const QString payload = QString::fromUtf8(json);
    QPointer<IperfJsonSink> sink = bridge->m_sink;
    if (sink.isNull()) {
        return;
    }

    QMetaObject::invokeMethod(sink.data(), [sink, payload]() {
        if (!sink.isNull()) {
            sink->enqueueJson(payload);
        }
    }, Qt::QueuedConnection);
}

void
IperfCoreBridge::handleParsedEvent(const IperfGuiEvent &event)
{
    QMutexLocker locker(&m_mutex);
    m_currentSession.events.push_back(event);
    m_currentSession.rawJson = event.rawJson;
    if (event.kind == IperfEventKind::Summary || event.kind == IperfEventKind::Finished) {
        m_currentSession.finalFields = event.fields;
        m_currentSession.statusText = jsonKindName(event.kind);
    } else if (event.kind == IperfEventKind::Error) {
        m_currentSession.statusText = event.message;
    }

    if (event.kind == IperfEventKind::Started) {
        m_statusText = QStringLiteral("Running");
    } else if (event.kind == IperfEventKind::Interval) {
        const QVariantMap summary = summaryFieldsFromEvent(event);
        if (!summary.isEmpty()) {
            const QVariantMap cpu = cpuFieldsFromEvent(event);
            const QString summaryName = event.fields.value(QStringLiteral("summary_key")).toString();
            QString parts = QStringLiteral("Interval");
            if (!summaryName.isEmpty()) {
                parts += QStringLiteral(" ");
                parts += summaryName;
            }
            if (summary.contains(QStringLiteral("bits_per_second"))) {
                parts += QStringLiteral(" ");
                parts += summary.value(QStringLiteral("bits_per_second")).toString();
            }
            if (cpu.contains(QStringLiteral("host_total"))) {
                parts += QStringLiteral(" CPU ");
                parts += cpu.value(QStringLiteral("host_total")).toString();
            }
            m_statusText = parts;
        } else {
            m_statusText = QStringLiteral("Interval");
        }
    } else if (event.kind == IperfEventKind::Summary) {
        m_statusText = QStringLiteral("Summary ready");
    } else if (event.kind == IperfEventKind::Error) {
        m_statusText = event.message;
    } else if (event.kind == IperfEventKind::Finished) {
        m_statusText = QStringLiteral("Finished");
    }

    emit eventReceived(event);
    emit statusMessageChanged(m_statusText);
    emit stateChanged(m_statusText);
    emit sessionUpdated(m_currentSession);
}

void
IperfCoreBridge::finishSessionOnGuiThread(int exitCode)
{
    QMutexLocker locker(&m_mutex);
    if (!m_running) {
        return;
    }

    if (m_stopRequested) {
        exitCode = 0;
        if (m_statusText.isEmpty() || m_statusText == QStringLiteral("Stopping")) {
            m_statusText = QStringLiteral("Stopped");
        }
    } else if (exitCode == 0) {
        m_statusText = QStringLiteral("Completed");
    } else if (m_statusText.isEmpty()) {
        m_statusText = QStringLiteral("Failed");
    }

    m_currentSession.exitCode = exitCode;
    m_currentSession.statusText = m_statusText;
    m_currentSession.rawJson = m_sink->lastRawJson();
    if (!m_sink->lastSummaryEvent().fields.isEmpty()) {
        m_currentSession.finalFields = m_sink->lastSummaryEvent().fields;
    }

    if (m_currentSession.finalFields.isEmpty() && !m_currentSession.events.isEmpty()) {
        const IperfGuiEvent lastEvent = m_currentSession.events.constLast();
        if (!lastEvent.fields.isEmpty()) {
            m_currentSession.finalFields = lastEvent.fields;
        }
    }

    m_history.push_back(m_currentSession);

    unregisterBridge(m_test);
    cleanupTest();

    m_running = false;
    m_stopRequested = false;

    if (m_runner != nullptr) {
        delete m_runner;
        m_runner = nullptr;
    }

    emit runningChanged(false);
    emit sessionCompleted(m_currentSession);
    emit stateChanged(m_statusText);
    emit statusMessageChanged(m_statusText);
}

void
IperfCoreBridge::cleanupTest()
{
    if (m_test == nullptr) {
        return;
    }

    iperf_free_test(m_test);
    m_test = nullptr;
}

FILE *
IperfCoreBridge::openNullDevice()
{
#ifdef _WIN32
    FILE *file = fopen("NUL", "w");
#else
    FILE *file = fopen("/dev/null", "w");
#endif
    return file != nullptr ? file : stdout;
}
