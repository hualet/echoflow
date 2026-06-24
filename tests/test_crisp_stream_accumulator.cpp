// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "CrispStreamAccumulator.h"

using namespace echoflow;

class TestCrispStreamAccumulator : public QObject {
    Q_OBJECT

private slots:
    void partialEmitsFinalizedPlusPartial();
    void finalAccumulatesAndEmits();
    void chineseDoesNotInsertSpace();
    void malformedLineIsIgnored();
    void finalTextIncludesTrailingPartial();
};

void TestCrispStreamAccumulator::partialEmitsFinalizedPlusPartial()
{
    CrispStreamAccumulator a;
    auto out = a.processEvent(R"({"type":"partial","utterance_id":1,"text":"hello","t0":0,"t1":1})");
    QVERIFY(out.has_value());
    QCOMPARE(QString::fromStdString(*out), QString("hello"));
}

void TestCrispStreamAccumulator::finalAccumulatesAndEmits()
{
    CrispStreamAccumulator a;
    a.processEvent(R"({"type":"partial","utterance_id":1,"text":"hello","t0":0,"t1":1})");
    auto out = a.processEvent(R"({"type":"final","utterance_id":1,"text":"hello world","t0":0,"t1":2})");
    QVERIFY(out.has_value());
    QCOMPARE(QString::fromStdString(*out), QString("hello world"));
    QCOMPARE(QString::fromStdString(a.finalText()), QString("hello world"));
}

void TestCrispStreamAccumulator::chineseDoesNotInsertSpace()
{
    CrispStreamAccumulator a;
    a.processEvent(R"({"type":"final","utterance_id":1,"text":"你好","t0":0,"t1":1})");
    auto out = a.processEvent(R"({"type":"final","utterance_id":2,"text":"世界","t0":1,"t1":2})");
    QCOMPARE(QString::fromStdString(*out), QString::fromUtf8("你好世界"));
}

void TestCrispStreamAccumulator::malformedLineIsIgnored()
{
    CrispStreamAccumulator a;
    QVERIFY(!a.processEvent("not json").has_value());
    QVERIFY(!a.processEvent(R"({"type":"silence","t":1})").has_value());
}

void TestCrispStreamAccumulator::finalTextIncludesTrailingPartial()
{
    CrispStreamAccumulator a;
    a.processEvent(R"({"type":"final","utterance_id":1,"text":"done","t0":0,"t1":1})");
    a.processEvent(R"({"type":"partial","utterance_id":2,"text":"wip","t0":1,"t1":2})");
    QCOMPARE(QString::fromStdString(a.finalText()), QString("done wip"));
}

QTEST_GUILESS_MAIN(TestCrispStreamAccumulator)
#include "test_crisp_stream_accumulator.moc"
