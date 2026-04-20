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
        // UDP: initialize binary search boundaries
        m_udpLow = 10.0e6;   // 10 Mbps
        m_udpHigh = 1.0e9;   // 1 Gbps
        m_udpBestBps = 0.0;
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
        // UDP: check convergence first
        if (m_currentStep > 0 && m_udpHigh / m_udpLow < 1.05) {
            finishClimb(false);
            return;
        }
        if (m_currentStep >= 20) {
            // Safety limit: at most 20 UDP rounds
            finishClimb(false);
            return;
        }
        const double rate = (m_udpLow + m_udpHigh) / 2.0;
        stepConfig.bitrateBps = static_cast<quint64>(rate);
        stepConfig.duration = 5; // 5 s per UDP probe round
        stepConfig.trafficType = TrafficType::Udp;

        emit stepStarted(m_currentStep,
                         QStringLiteral("UDP rate=%1 Mbps").arg(rate / 1.0e6, 0, 'f', 1));
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
        // UDP binary search
        if (loss < 0.001) {
            // Less than 0.1% loss — can go higher
            m_udpLow = static_cast<double>(m_bridge->configuration().bitrateBps);
            m_udpBestBps = qMax(m_udpBestBps, stable);
        } else if (loss > 0.01) {
            // More than 1% loss — too high
            m_udpHigh = static_cast<double>(m_bridge->configuration().bitrateBps);
        } else {
            // Sweet spot: 0.1% ≤ loss ≤ 1%
            m_udpBestBps = qMax(m_udpBestBps, stable);
            finishClimb(false);
            return;
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
        // Find best step
        double bestStable = 0.0;
        double bestPeak = 0.0;
        int bestParallel = 1;
        int bestIdx = 0;
        for (int i = 0; i < m_stepResults.size(); ++i) {
            if (m_stepResults.at(i) > bestStable) {
                bestStable = m_stepResults.at(i);
                bestPeak = m_peakResults.size() > i ? m_peakResults.at(i) : 0.0;
                bestParallel = m_isTcp ? s_tcpSteps[qMin(i, s_tcpStepCount - 1)] : 1;
                bestIdx = i;
            }
        }
        const double maxUdpBps = m_isTcp ? 0.0 : m_udpBestBps;
        emit progressChanged(100);
        emit foundMaxThroughput(bestStable, bestPeak, bestParallel, maxUdpBps);
    }

    emit orchestrationFinished(aborted);
}
