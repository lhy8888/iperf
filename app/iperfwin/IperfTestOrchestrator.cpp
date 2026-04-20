#include "IperfTestOrchestrator.h"

#include "IperfCoreBridge.h"

#include <QMetaObject>
#include <QtMath>

// TCP parallel stream steps: 1, 2, 4, 8, 16, 32
const int IperfTestOrchestrator::s_tcpSteps[] = { 1, 2, 4, 8, 16, 32 };
const int IperfTestOrchestrator::s_tcpStepCount = static_cast<int>(sizeof(s_tcpSteps) / sizeof(s_tcpSteps[0]));

IperfTestOrchestrator::IperfTestOrchestrator(IperfCoreBridge *bridge, QObject *parent)
    : QObject(parent)
    , m_bridge(bridge)
{
}

IperfTestOrchestrator::~IperfTestOrchestrator()
{
    // If still connected, disconnect to avoid dangling callback
    if (m_bridge != nullptr && m_running) {
        disconnect(m_bridge, &IperfCoreBridge::sessionCompleted,
                   this, &IperfTestOrchestrator::onStepCompleted);
        m_bridge->stop();
    }
}

void
IperfTestOrchestrator::startClimb(const IperfGuiConfig &baseConfig)
{
    if (m_running || m_bridge == nullptr) {
        return;
    }

    m_baseConfig = baseConfig;
    m_stepResults.clear();
    m_peakResults.clear();
    m_currentStep = 0;
    m_aborted = false;
    m_running = true;
    m_isTcp = (baseConfig.trafficType == TrafficType::Tcp ||
               baseConfig.trafficType == TrafficType::Icmp || // fallback
               (baseConfig.trafficType != TrafficType::Udp));

    if (!m_isTcp) {
        // UDP: two-phase probe — exponential ramp then binary search.
        // m_udpHigh == 0 marks "ramp phase not yet complete".
        m_udpCurrentRate = 100.0e6; // 100 Mbps/stream starting point
        m_udpLow         = 0.0;
        m_udpHigh        = 0.0;
        m_udpBestBps     = 0.0;
        m_udpLastRate    = 0.0;
    }

    connect(m_bridge, &IperfCoreBridge::sessionCompleted,
            this, &IperfTestOrchestrator::onStepCompleted,
            Qt::QueuedConnection);

    scheduleNextStep();
}

void
IperfTestOrchestrator::abort()
{
    if (!m_running) {
        return;
    }
    m_aborted = true;
    if (m_bridge != nullptr && m_bridge->isRunning()) {
        m_bridge->stop();
        // finishClimb will be called from onStepCompleted after bridge stops
    } else {
        finishClimb(true);
    }
}

void
IperfTestOrchestrator::scheduleNextStep()
{
    if (m_bridge == nullptr || m_aborted) {
        finishClimb(m_aborted);
        return;
    }

    IperfGuiConfig stepConfig = m_baseConfig;

    if (m_isTcp) {
        if (m_currentStep >= s_tcpStepCount) {
            finishClimb(false);
            return;
        }
        const int parallel = s_tcpSteps[m_currentStep];
        stepConfig.parallel = parallel;
        // 5 s per probe step keeps total climb time under ~30 s
        stepConfig.duration = 5;
        stepConfig.trafficType = TrafficType::Tcp;

        const int percent = (m_currentStep * 100) / s_tcpStepCount;
        emit progressChanged(percent);
        emit stepStarted(m_currentStep, QStringLiteral("TCP streams=%1").arg(parallel));
    } else {
        // UDP: exponential ramp-up (×4/step) then binary search.
        // All rates are per-stream (iperf3 -b semantics).
        // m_udpHigh == 0  →  still in ramp phase.

        if (m_currentStep >= 30) {        // hard safety limit
            finishClimb(false);
            return;
        }

        const int    parallel      = qMax(1, m_baseConfig.parallel);
        // Hard cap: 400 Gbps total. Per-stream cap = 400 Gbps / parallel.
        const double kMaxPerStream = 400.0e9 / static_cast<double>(parallel);

        double perStreamRate;
        if (m_udpHigh <= 0.0) {
            // ── Ramp phase ──────────────────────────────────────────────────
            if (m_udpCurrentRate > kMaxPerStream) {
                // Exceeded 400 Gbps total without any loss → path is extremely
                // fast (or rate-unlimited); accept the cap as best.
                m_udpBestBps = qMax(m_udpBestBps, kMaxPerStream);
                finishClimb(false);
                return;
            }
            perStreamRate = m_udpCurrentRate;
        } else {
            // ── Binary-search phase ──────────────────────────────────────────
            if (m_udpHigh / qMax(m_udpLow, 1.0) < 1.05) {
                finishClimb(false);
                return;
            }
            perStreamRate = (m_udpLow + m_udpHigh) / 2.0;
        }

        m_udpLastRate         = perStreamRate;
        stepConfig.bitrateBps = static_cast<quint64>(perStreamRate);
        stepConfig.duration   = 5;
        stepConfig.trafficType = TrafficType::Udp;

        const double perStreamGbps = perStreamRate / 1.0e9;
        const double totalGbps     = perStreamGbps * parallel;
        emit stepStarted(m_currentStep,
                         QStringLiteral("UDP %1 Gbps total  (%2 streams × %3 Gbps)")
                         .arg(totalGbps,     0, 'f', 1)
                         .arg(parallel)
                         .arg(perStreamGbps, 0, 'f', 2));
    }

    m_bridge->setConfiguration(stepConfig);
    m_bridge->start();
}

void
IperfTestOrchestrator::onStepCompleted(const IperfSessionRecord &record)
{
    if (!m_running) {
        return;
    }
    if (m_aborted) {
        finishClimb(true);
        return;
    }

    const double stable = record.stableBps;
    const double peak = record.peakBps;
    const double loss = record.lossPercent;

    m_stepResults.push_back(stable);
    m_peakResults.push_back(peak);

    emit stepCompleted(m_currentStep, stable, loss);

    if (m_isTcp) {
        // Check convergence: less than 5% improvement over previous round
        if (m_currentStep > 0) {
            const double prev = m_stepResults.at(m_currentStep - 1);
            if (prev > 0.0 && (stable - prev) / prev < 0.05) {
                // Saturated — best is the previous step
                finishClimb(false);
                return;
            }
        }
        ++m_currentStep;
        scheduleNextStep();
    } else {
        // UDP: exponential ramp + binary search (all rates are per-stream).
        // m_udpLastRate holds the per-stream rate we just tested.
        const double usedRate = m_udpLastRate; // per-stream

        if (m_udpHigh <= 0.0) {
            // ── Ramp phase ──────────────────────────────────────────────────
            if (loss < 0.001) {
                // < 0.1 % loss: path can take more — raise low bound and
                // quadruple the rate for the next step.
                m_udpLow     = usedRate;
                m_udpBestBps = qMax(m_udpBestBps, usedRate);
                m_udpCurrentRate = usedRate * 4.0;
            } else if (loss > 0.01) {
                // > 1 % loss: found the ceiling — transition to binary search.
                m_udpHigh = usedRate;
                if (m_udpLow <= 0.0) {
                    // First ramp step already saturated (very slow / shaped path).
                    m_udpLow = 1.0e6; // 1 Mbps fallback lower bound
                }
            } else {
                // 0.1 %–1 % loss during ramp: sweet spot, accept immediately.
                m_udpBestBps = qMax(m_udpBestBps, usedRate);
                finishClimb(false);
                return;
            }
        } else {
            // ── Binary-search phase ──────────────────────────────────────────
            if (loss < 0.001) {
                m_udpLow     = usedRate;
                m_udpBestBps = qMax(m_udpBestBps, usedRate);
            } else if (loss > 0.01) {
                m_udpHigh = usedRate;
            } else {
                // Sweet spot (0.1 %–1 %)
                m_udpBestBps = qMax(m_udpBestBps, usedRate);
                finishClimb(false);
                return;
            }
        }
        ++m_currentStep;
        scheduleNextStep();
    }
}

void
IperfTestOrchestrator::finishClimb(bool aborted)
{
    disconnect(m_bridge, &IperfCoreBridge::sessionCompleted,
               this, &IperfTestOrchestrator::onStepCompleted);
    m_running = false;

    if (!aborted && !m_stepResults.isEmpty()) {
        double bestStable   = 0.0;
        double bestPeak     = 0.0;
        int    bestParallel = 1;
        for (int i = 0; i < m_stepResults.size(); ++i) {
            if (m_stepResults.at(i) > bestStable) {
                bestStable   = m_stepResults.at(i);
                bestPeak     = m_peakResults.size() > i ? m_peakResults.at(i) : 0.0;
                // TCP: bestParallel is the streams count for that step.
                // UDP: keep the same parallel the user chose; do not override.
                if (m_isTcp) {
                    bestParallel = s_tcpSteps[qMin(i, s_tcpStepCount - 1)];
                }
            }
        }
        if (!m_isTcp) {
            // UDP: use the user's original parallel so the sustained phase
            // replicates the probe exactly.  m_udpBestBps is the per-stream
            // rate that proved stable; the sustained phase sets cfg.bitrateBps
            // to this value and cfg.parallel = m_baseConfig.parallel.
            bestParallel = qMax(1, m_baseConfig.parallel);
        }
        const double maxUdpBps = m_isTcp ? 0.0 : m_udpBestBps; // per-stream rate
        emit progressChanged(100);
        emit foundMaxThroughput(bestStable, bestPeak, bestParallel, maxUdpBps);
    }

    emit orchestrationFinished(aborted);
}
