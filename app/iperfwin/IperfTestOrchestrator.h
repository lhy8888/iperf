#pragma once

#include "IperfGuiTypes.h"

#include <QObject>
#include <QString>
#include <QVector>

class IperfCoreBridge;

/**
 * IperfTestOrchestrator runs the probe phase of every client test.
 *
 * It executes a series of short iperf sessions to determine the optimal load
 * for the current path, then signals completion so the caller can start a
 * full-duration sustain phase at those parameters.
 *
 * TCP probe: dynamic ladder 1, 2, 4, 8, ... up to the internal ceiling.
 *   Uses slightly longer warm-up rounds at low parallel counts, then does a
 *   higher-ceiling validation step before stopping on a plateau.
 *
 * UDP probe: two-phase, works for 10 Mbps through 400 Gbps links.
 *   Phase 1 — exponential ramp: start at 100 Mbps/stream, ×4 each step until
 *     loss > 1 % or the per-stream rate exceeds 400 Gbps / parallel.
 *   Phase 2 — binary search: converges between last-good and first-bad rate
 *     until high/low < 1.05.
 *   All rate variables are per-stream (matching iperf3's -b semantics).
 *   m_udpHigh == 0 signals "still in ramp phase".
 *   If the internal auto-parallel probe is enabled, the same UDP rate search
 *   is repeated on a parallel ladder and the best total throughput wins.
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
    static QVector<int> buildParallelLadder(int maxParallel, int startParallel = 1);
    static int tcpProbeDurationSeconds(int parallel);
    void resetUdpProbeState();
    void recordUdpCandidateResult();
    bool advanceUdpCandidate(bool finishedCurrentCandidate);

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
    QVector<int> m_parallelLadder;
    QVector<double> m_stepResults;    // stableBps per step
    QVector<double> m_peakResults;    // peakBps per step
    QVector<int> m_udpCandidateParallels;
    QVector<double> m_udpCandidateStableResults;
    QVector<double> m_udpCandidatePeakResults;
    QVector<double> m_udpCandidateRateResults;
    int m_probeMaxParallel = 128;
    int m_udpCandidateIndex = 0;
    int m_tcpPlateauStreak = 0;
    bool m_tcpValidationPending = false;

    int m_currentStep = 0;
    bool m_running = false;
    bool m_aborted = false;
    bool m_isTcp = true;
    bool m_udpAutoParallelProbe = true;
};
