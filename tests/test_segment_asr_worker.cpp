// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SegmentAsrWorker.h"

#include <QTest>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace echoflow;

class TestSegmentAsrWorker : public QObject {
    Q_OBJECT

private slots:
    void slowTranscriberPreservesOrderAndEverySegment();
};

void TestSegmentAsrWorker::slowTranscriberPreservesOrderAndEverySegment()
{
    SegmentAsrWorker worker([](const AudioSegment& segment) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return std::to_string(segment.samples.front());
    });

    worker.start();
    for (int i = 0; i < 12; ++i) {
        AudioSegment segment;
        segment.samples = {static_cast<int16_t>(i)};
        QVERIFY(worker.enqueue(std::move(segment)));
    }

    const std::vector<std::string> expected = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
    QCOMPARE(worker.finish(), expected);
    QCOMPARE(worker.enqueuedCount(), size_t(12));
    QCOMPARE(worker.completedCount(), size_t(12));
    QVERIFY(worker.highWaterMark() > 1);
}

QTEST_GUILESS_MAIN(TestSegmentAsrWorker)
#include "test_segment_asr_worker.moc"
