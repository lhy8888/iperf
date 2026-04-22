#include "IperfTestOrchestrator.h"

#include "IperfCoreBridge.h"

#include <QMetaObject>
#include <QtMath>

static QVector<int> buildParallelLadderImpl(int maxParallel, int startParallel)
{
    QVector<int> ladder;
    const int ceiling = qMax(1, maxParallel);
    int parallel = qBound(1, startParallel, ceiling);

    while (true) {
        if (ladder.isEmpty() || ladder.last() != parallel) {
            ladder.push_back(parallel);
        }
        if (parallel >= ceiling) {
            break;
        }
        const int next = qMin(ceiling, parallel * 2);
        if (next <= parallel) {
            break;
        }
        parallel = next;
    }

    if (ladder.isEmpty()) {
        ladder.push_back(ceiling);
    }
    if (ladder.last() != ceiling) {
        ladder.push_back(ceiling);
    }

    return ladder;
}

static int tcpProbeDurationSecondsForParallel(int parallel)
{
    if (parallel <= 2) {
        return 8;
    }
    if (parallel <= 8) {
        return 6;
    }
    if (parallel <= 32) {
        return 5;
    }
    return 6;
}

// TCP parallel stream steps: dynamic doubling ladder up to the ceiling.
QVector<int> IperfTestOrchestrator::buildParallelLadder(int maxParallel, int startParallel)
{
    return buildParallelLadderImpl(maxParallel, startParallel);
}

int IperfTestOrchestrator::tcpProbeDurationSeconds(int parallel)
{
    return tcpProbeDurationSecondsForParallel(parallel);
}

void IperfTestOrchestrator::resetUdpProbeState()
{
    m_stepResults.clear();
    m_peakResults.clear();
    m_currentStep = 0;
    m_udpCurrentRate = 100.0e6;
    m_udpLow = 0.0;
    m_udpHigh = 0.0;
    m_udpBestBps = 0.0;
    m_udpLastRate = 0.0;
}

void IperfTestOrchestrator::recordUdpCandidateResult()
{
    if (m_stepResults.isEmpty()) {
        return;
    }

    double bestStable = 0.0;
    double bestPeak = 0.0;
    for (int i = 0; i < m_stepResults.size(); ++i) {
        const double stable = m_stepResults.at(i);
        if (stable > bestStable) {
            bestStable = stable;
            bestPeak = m_peakResults.size() > i ? m_peakResults.at(i) : 0.0;
        }
    }

    const int parallel = (m_udpCandidateIndex >= 0 && m_udpCandidateIndex < m_parallelLadder.size())
        ? m_parallelLadder.at(m_udpCandidateIndex)
        : qMax(1, m_baseConfig.parallel);

    m_udpCandidateParallels.push_back(parallel);
    m_udpCandidateStableResults.push_back(bestStable);
    m_udpCandidatePeakResults.push_back(bestPeak);
    m_udpCandidateRateResults.push_back(qMax(m_udpBestBps, m_udpLow));
}

bool IperfTestOrchestrator::advanceUdpCandidate(bool finishedCurrentCandidate)
{
    if (finishedCurrentCandidate) {
        recordUdpCandidateResult();
    }

    if (!m_udpAutoParallelProbe || m_udpCandidateIndex + 1 >= m_parallelLadder.size()) {
        return false;
    }

    ++m_udpCandidateIndex;
    resetUdpProbeState();
    return true;
}

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
    m_udpCandidateParallels.clear();
    m_udpCandidateStableResults.clear();
    m_udpCandidatePeakResults.clear();
    m_udpCandidateRateResults.clear();
    m_currentStep = 0;
    m_aborted = false;
    m_tcpPlateauStreak = 0;
    m_tcpValidationPending = false;
    m_running = true;
    m_isTcp = (baseConfig.trafficType == TrafficType::Tcp ||
               baseConfig.trafficType == TrafficType::Icmp || // fallback
               (baseConfig.trafficType != TrafficType::Udp));
    m_probeMaxParallel = qBound(1, baseConfig.probeMaxParallel, 256);
    m_udpAutoParallelProbe = baseConfig.udpAutoParallelProbe;

    if (!m_isTcp) {
        const int startParallel = qMax(1, baseConfig.parallel);
        m_parallelLadder = m_udpAutoParallelProbe
            ? buildParallelLadder(m_probeMaxParallel, startParallel)
            : QVector<int>{startParallel};
        m_udpCandidateIndex = 0;
        resetUdpProbeState();
    } else {
        m_parallelLadder = buildParallelLadder(m_probeMaxParallel, 1);
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
        if (m_currentStep < 0 || m_currentStep >= m_parallelLadder.size()) {
            finishClimb(false);
            return;
        }

        const int parallel = m_parallelLadder.at(m_currentStep);
        stepConfig.parallel = parallel;
        stepConfig.duration = tcpProbeDurationSeconds(parallel);
        stepConfig.trafficType = TrafficType::Tcp;
        stepConfig.probeSession = true;

        const int percent = qBound(0,
                                   qRound((double(m_currentStep) / qMax(1, m_parallelLadder.size())) * 100.0),
                                   99);
        emit progressChanged(percent);
        emit stepStarted(m_currentStep,
                         QStringLiteral("TCP streams=%1%2")
                             .arg(parallel)
                             .arg(m_tcpValidationPending ? QStringLiteral(" · validate") : QString()));
    } else {
        if (m_udpCandidateIndex < 0 || m_udpCandidateIndex >= m_parallelLadder.size()) {
            finishClimb(false);
            return;
        }

        const int parallel = m_parallelLadder.at(m_udpCandidateIndex);
        const int percent = qBound(0,
                                   qRound((double(m_udpCandidateIndex) / qMax(1, m_parallelLadder.size())) * 100.0),
                                   99);
        emit progressChanged(percent);

        const double kMaxPerStream = 400.0e9 / static_cast<double>(parallel);

        double perStreamRate = 0.0;
        if (m_udpHigh <= 0.0) {
            if (m_udpCurrentRate > kMaxPerStream) {
                m_udpBestBps = qMax(m_udpBestBps, kMaxPerStream);
                if (advanceUdpCandidate(true)) {
                    scheduleNextStep();
                } else {
                    finishClimb(false);
                }
                return;
            }
            perStreamRate = m_udpCurrentRate;
        } else {
            if (m_udpHigh / qMax(m_udpLow, 1.0) < 1.05) {
                if (advanceUdpCandidate(true)) {
                    scheduleNextStep();
                } else {
                    finishClimb(false);
                }
                return;
            }
            perStreamRate = (m_udpLow + m_udpHigh) / 2.0;
        }

        m_udpLastRate = perStreamRate;
        stepConfig.parallel = parallel;
        stepConfig.bitrateBps = static_cast<quint64>(perStreamRate);
        stepConfig.duration = 5;
        stepConfig.trafficType = TrafficType::Udp;
        stepConfig.probeSession = true;

        const double perStreamGbps = perStreamRate / 1.0e9;
        const double totalGbps = perStreamGbps * parallel;
        emit stepStarted(m_currentStep,
                         QStringLiteral("UDP %1 Gbps total  (%2 streams × %3 Gbps)")
                             .arg(totalGbps, 0, 'f', 1)
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
        if (m_currentStep > 0) {
            const double prev = m_stepResults.at(m_currentStep - 1);
            const double prevPeak = m_peakResults.at(m_currentStep - 1);
            const bool stablePlateau = (prev > 0.0) ? ((stable - prev) / prev < 0.05) : false;
            const bool peakPlateau = (prevPeak > 0.0) ? ((peak - prevPeak) / prevPeak < 0.03) : false;
            const bool plateau = stablePlateau && peakPlateau;

            if (plateau) {
                ++m_tcpPlateauStreak;
                if (!m_tcpValidationPending && m_currentStep + 1 < m_parallelLadder.size()) {
                    m_tcpValidationPending = true;
                } else {
                    finishClimb(false);
                    return;
                }
            } else {
                m_tcpPlateauStreak = 0;
                m_tcpValidationPending = false;
            }
        }

        ++m_currentStep;
        if (m_currentStep >= m_parallelLadder.size()) {
            finishClimb(false);
            return;
        }
        scheduleNextStep();
        return;
    }

    const double usedRate = m_udpLastRate;

    if (m_udpHigh <= 0.0) {
        if (loss < 0.001) {
            m_udpLow = usedRate;
            m_udpBestBps = qMax(m_udpBestBps, usedRate);
            m_udpCurrentRate = usedRate * 4.0;
        } else if (loss > 0.01) {
            m_udpHigh = usedRate;
            if (m_udpLow <= 0.0) {
                m_udpLow = 1.0e6;
                m_udpBestBps = qMax(m_udpBestBps, m_udpLow);
            }
        } else {
            m_udpBestBps = qMax(m_udpBestBps, usedRate);
            if (advanceUdpCandidate(true)) {
                scheduleNextStep();
            } else {
                finishClimb(false);
            }
            return;
        }
    } else {
        if (loss < 0.001) {
            m_udpLow = usedRate;
            m_udpBestBps = qMax(m_udpBestBps, usedRate);
        } else if (loss > 0.01) {
            m_udpHigh = usedRate;
        } else {
            m_udpBestBps = qMax(m_udpBestBps, usedRate);
            if (advanceUdpCandidate(true)) {
                scheduleNextStep();
            } else {
                finishClimb(false);
            }
            return;
        }

        if (m_udpHigh > 0.0 && m_udpHigh / qMax(m_udpLow, 1.0) < 1.05) {
            if (advanceUdpCandidate(true)) {
                scheduleNextStep();
            } else {
                finishClimb(false);
            }
            return;
        }
    }

    ++m_currentStep;
    scheduleNextStep();
}

void
IperfTestOrchestrator::finishClimb(bool aborted)
{
    disconnect(m_bridge, &IperfCoreBridge::sessionCompleted,
               this, &IperfTestOrchestrator::onStepCompleted);
    m_running = false;

    if (!aborted) {
        if (m_isTcp && !m_stepResults.isEmpty()) {
            double bestStable = 0.0;
            double bestPeak = 0.0;
            int bestParallel = 1;
            for (int i = 0; i < m_stepResults.size(); ++i) {
                if (m_stepResults.at(i) > bestStable) {
                    bestStable = m_stepResults.at(i);
                    bestPeak = m_peakResults.size() > i ? m_peakResults.at(i) : 0.0;
                    bestParallel = m_parallelLadder.value(i, m_parallelLadder.isEmpty()
                                                             ? qMax(1, m_baseConfig.parallel)
                                                             : m_parallelLadder.constLast());
                }
            }
            emit progressChanged(100);
            emit foundMaxThroughput(bestStable, bestPeak, bestParallel, 0.0);
        } else if (!m_isTcp) {
            if (!m_udpCandidateStableResults.isEmpty()) {
                int bestIndex = 0;
                for (int i = 1; i < m_udpCandidateStableResults.size(); ++i) {
                    const double stable = m_udpCandidateStableResults.at(i);
                    const double bestStable = m_udpCandidateStableResults.at(bestIndex);
                    const double peak = m_udpCandidatePeakResults.value(i, 0.0);
                    const double bestPeak = m_udpCandidatePeakResults.value(bestIndex, 0.0);
                    const double rate = m_udpCandidateRateResults.value(i, 0.0);
                    const double bestRate = m_udpCandidateRateResults.value(bestIndex, 0.0);
                    if (stable > bestStable
                        || (qFuzzyCompare(stable + 1.0, bestStable + 1.0) && peak > bestPeak)
                        || (qFuzzyCompare(stable + 1.0, bestStable + 1.0)
                            && qFuzzyCompare(peak + 1.0, bestPeak + 1.0)
                            && rate > bestRate)) {
                        bestIndex = i;
                    }
                }
                const double bestStable = m_udpCandidateStableResults.at(bestIndex);
                const double bestPeak = m_udpCandidatePeakResults.value(bestIndex, 0.0);
                const int bestParallel = m_udpCandidateParallels.value(bestIndex, qMax(1, m_baseConfig.parallel));
                const double bestRate = qMax(0.0, m_udpCandidateRateResults.value(bestIndex, 0.0));
                emit progressChanged(100);
                emit foundMaxThroughput(bestStable, bestPeak, bestParallel, bestRate);
            } else if (!m_stepResults.isEmpty()) {
                double bestStable = 0.0;
                double bestPeak = 0.0;
                for (int i = 0; i < m_stepResults.size(); ++i) {
                    if (m_stepResults.at(i) > bestStable) {
                        bestStable = m_stepResults.at(i);
                        bestPeak = m_peakResults.size() > i ? m_peakResults.at(i) : 0.0;
                    }
                }
                emit progressChanged(100);
                emit foundMaxThroughput(bestStable, bestPeak, qMax(1, m_baseConfig.parallel), qMax(m_udpBestBps, m_udpLow));
            }
        }
    }

    emit orchestrationFinished(aborted);
}

