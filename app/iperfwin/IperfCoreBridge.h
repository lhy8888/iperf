#pragma once

#include "IperfGuiTypes.h"
#include "IperfJsonSink.h"

#include <QObject>
#include <QRecursiveMutex>
#include <QString>
#include <QVector>

class QThread;
struct iperf_test;

class IperfCoreBridge : public QObject
{
    Q_OBJECT

public:
    explicit IperfCoreBridge(QObject *parent = nullptr);
    ~IperfCoreBridge() override;

    void setConfiguration(const IperfGuiConfig &config);
    IperfGuiConfig configuration() const;
    void finishSessionOnGuiThread(int exitCode, bool escapedByLongjmp);

    bool isRunning() const;
    QString statusText() const;
    IperfSessionRecord currentSession() const;
    QVector<IperfSessionRecord> history() const;
    void clearHistory();

public slots:
    void start();
    void stop();

signals:
    void configurationChanged(const IperfGuiConfig &config);
    void runningChanged(bool running);
    void stateChanged(const QString &state);
    void statusMessageChanged(const QString &message);
    void eventReceived(const IperfGuiEvent &event);
    void sessionUpdated(const IperfSessionRecord &record);
    void sessionCompleted(const IperfSessionRecord &record);
    void errorOccurred(const QString &message);

private:
    struct iperf_test *createTest(QString *errorMessage);
    void applyConfiguration(struct iperf_test *test) const;
    void registerBridge(struct iperf_test *test);
    void unregisterBridge(struct iperf_test *test);
    static IperfCoreBridge *bridgeForTest(struct iperf_test *test);
    static void jsonCallbackThunk(struct iperf_test *test, char *json);
    void handleParsedEvent(const IperfGuiEvent &event);
    void cleanupTest();
    static FILE *openNullDevice();

    mutable QRecursiveMutex m_mutex;
    IperfGuiConfig m_config;
    IperfJsonSink *m_sink = nullptr;
    IperfSessionRecord m_currentSession;
    // Throughput samples collected per interval tick.  Stored as plain doubles
    // rather than full IperfGuiEvent objects so that a 24-hour Continuous test
    // at 1 s intervals (86 400 ticks) uses ~675 KB instead of potentially
    // hundreds of MB.  Reset at the start of every session.
    QVector<double> m_intervalBps;
    QVector<IperfSessionRecord> m_history;
    struct iperf_test *m_test = nullptr;
    QThread *m_runner = nullptr;
    FILE *m_nullOut = nullptr;
    QString m_statusText;
    bool m_running = false;
    bool m_stopRequested = false;
    bool m_networkReady = false;
};
