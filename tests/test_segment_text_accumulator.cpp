// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "SegmentTextAccumulator.h"

using namespace echoflow;

class TestSegmentTextAccumulator : public QObject {
    Q_OBJECT

private slots:
    void joinsChineseWithoutSpaces();
    void joinsAsciiWordsWithSpace();
    void joinsAsciiAfterPunctuationWithSpace();
    void ignoresEmptySegments();
    void clearDropsText();
};

void TestSegmentTextAccumulator::joinsChineseWithoutSpaces() {
    SegmentTextAccumulator accumulator;

    accumulator.append(0, "你好");
    accumulator.append(1, "世界");

    QCOMPARE(accumulator.text(), std::string("你好世界"));
}

void TestSegmentTextAccumulator::joinsAsciiWordsWithSpace() {
    SegmentTextAccumulator accumulator;

    accumulator.append(0, "hello");
    accumulator.append(1, "world");

    QCOMPARE(accumulator.text(), std::string("hello world"));
}

void TestSegmentTextAccumulator::joinsAsciiAfterPunctuationWithSpace() {
    SegmentTextAccumulator accumulator;

    accumulator.append(0, "Hello,");
    accumulator.append(1, "world");

    QCOMPARE(accumulator.text(), std::string("Hello, world"));
}

void TestSegmentTextAccumulator::ignoresEmptySegments() {
    SegmentTextAccumulator accumulator;

    accumulator.append(0, "你好");
    accumulator.append(1, " ");
    accumulator.append(2, "世界");

    QCOMPARE(accumulator.text(), std::string("你好世界"));
}

void TestSegmentTextAccumulator::clearDropsText() {
    SegmentTextAccumulator accumulator;

    accumulator.append(0, "hello");
    accumulator.clear();

    QCOMPARE(accumulator.text(), std::string());
}

QTEST_MAIN(TestSegmentTextAccumulator)
#include "test_segment_text_accumulator.moc"
