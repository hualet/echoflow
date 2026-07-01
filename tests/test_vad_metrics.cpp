// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VadMetrics.h"

#include <QTest>

using namespace echoflow;

class TestVadMetrics : public QObject {
    Q_OBJECT

private slots:
    void computesMissFalseActivationAndEndpointDelay();
    void simulatesQueuedStreamingLatency();
};

void TestVadMetrics::computesMissFalseActivationAndEndpointDelay()
{
    const std::vector<TimeInterval> reference = {{1.0, 3.0}, {5.0, 6.0}};
    const std::vector<TimeInterval> predicted = {{0.5, 3.4}, {5.2, 6.5}};

    const VadMetrics metrics = evaluateVadIntervals(reference, predicted);

    QVERIFY(qAbs(metrics.speechSeconds - 3.0) < 0.0001);
    QVERIFY(qAbs(metrics.missedSpeechSeconds - 0.2) < 0.0001);
    QVERIFY(qAbs(metrics.falseActivationSeconds - 1.4) < 0.0001);
    QVERIFY(qAbs(metrics.medianEndpointDelaySeconds - 0.45) < 0.0001);
}

void TestVadMetrics::simulatesQueuedStreamingLatency()
{
    const StreamingLatencyMetrics metrics = simulateStreamingLatency(
        {2.0, 5.0}, {1500.0, 2000.0}, 6.0);
    QCOMPARE(metrics.firstStableTextMs, 3500.0);
    QCOMPARE(metrics.stopLatencyMs, 1000.0);
    QCOMPARE(metrics.lastCompletionMs, 7000.0);
}

QTEST_GUILESS_MAIN(TestVadMetrics)
#include "test_vad_metrics.moc"
