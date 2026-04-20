#pragma once

#include "IperfGuiTypes.h"

#include <QObject>
#include <QString>
#include <QVector>

class IperfCoreBridge;

/**
 * IperfTestOrchestrator runs the probe phase of every client test.
 *
 * It executes a series of short (5 s) iperf sessions to determine the optimal
 * load for the current path, then signals completion so the caller can start a
 * full-duration sustain phase at those parameters.
 *
 * TCP probe: parallel = 1, 2, 4, 8, 16, 32 (up to 6 × 5 s rounds).
 *   Stops early when throughput improvement < 5% over the previous round.
 *
 * UDP probe: two-phase, works for 10 Mbps through 400 Gbps links.
 *   Phase 1 — exponential ramp: start at 100 Mbps/stream, ×4 each step until
 *     loss > 1 % or the per-stream rate exceeds 400 Gbps / parallel.
 *   Phase 2 — binary search: converges between last-good and first-bad rate
 *     until high/low < 1.05.
 *   All rate variables are per-stream (matching iperf3's -b semantics).
 *   m_udpHigh == 0 signals "still in ramp phase".
 *
 * All methods and signals run on the GUI thread.
 */
class IperfTestOrchestrator : public QObject
{
    Q_OBJECT

public:
    explicit IperfTestOrchestrator(IperfCoreBridge *bridge, QObject *parent = nullptr);
    ~IperfTestOrchestrator() override;

    bool isRunning() const { return m_running; }

signals:
    void stepStarted(int step, const QString &description);
    void stepCompleted(int step, double stableBps, double lossPercent);
    void foundMaxThroughput(double stableBps, double peakBps, int optimalParallel, double maxUdpBps);
    void orchestrationFinished(bool aborted);
    void progressChanged(int percent);

public slots:
    void startClimb(const IperfGuiConfig &baseConfig);
    void abort();

private slots:
    void onStepCompleted(const IperfSessionRecord &record);

private:
    void scheduleNextStep();
    void finishClimb(bool aborted);

    // TCP climb state
    static const int s_tcpSteps[];
    static const int s_tcpStepCount;

    // UDP probe state — all values are per-stream rates (iperf3 -b semantics).
    // m_udpHigh == 0  →  ramp phase not yet complete.
    // m_udpHigh  > 0  →  binary-search phase: [m_udpLow, m_udpHigh].
    double m_udpCurrentRate = 100.0e6; // ramp: current probe rate (per stream, starts 100 Mbps)
    double m_udpLow         = 0.0;     // per-stream lower bound (proven good)
    double m_udpHigh        = 0.0;     // per-stream upper bound; 0 = ramp not done
    double m_udpBestBps     = 0.0;     // best per-stream rate with loss < 0.1 %
    double m_udpLastRate    = 0.0;     // per-stream rate used in the last step

    IperfCoreBridge *m_bridge = nullptr;
    IperfGuiConfig m_baseConfig;
    QVector<double> m_stepResults;    // stableBps per step
    QVector<double> m_peakResults;    // peakBps per step

    int m_currentStep = 0;
    bool m_running = false;
    bool m_aborted = false;
    bool m_isTcp = true;
};
