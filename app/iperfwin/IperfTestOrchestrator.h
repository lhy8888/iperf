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
 * UDP probe: binary-search on bitrate (low=10 Mbps, high=1 Gbps).
 *   loss < 0.1% → rate up; loss > 1.0% → rate down; converges when high/low < 1.05.
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

    // UDP binary-search state
    double m_udpLow = 10.0e6;
    double m_udpHigh = 1.0e9;
    double m_udpBestBps = 0.0;

    IperfCoreBridge *m_bridge = nullptr;
    IperfGuiConfig m_baseConfig;
    QVector<double> m_stepResults;    // stableBps per step
    QVector<double> m_peakResults;    // peakBps per step

    int m_currentStep = 0;
    bool m_running = false;
    bool m_aborted = false;
    bool m_isTcp = true;
};
