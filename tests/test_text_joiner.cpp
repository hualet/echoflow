// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TextJoiner.h"

#include <QTest>

using namespace echoflow;

class TestTextJoiner : public QObject {
    Q_OBJECT

private slots:
    void removesExactChineseOverlap();
    void preservesNearMatch();
    void preservesRepeatedCompleteUtterance();
};

void TestTextJoiner::removesExactChineseOverlap()
{
    QCOMPARE(joinOverlappingText("今天我们继续讨论语音输入", "讨论语音输入的完整性"),
             std::string("今天我们继续讨论语音输入的完整性"));
}

void TestTextJoiner::preservesNearMatch()
{
    QCOMPARE(joinOverlappingText("输入可能丢字", "输入不应该丢字"),
             std::string("输入可能丢字 输入不应该丢字"));
}

void TestTextJoiner::preservesRepeatedCompleteUtterance()
{
    QCOMPARE(joinOverlappingText("重复测试", "重复测试"),
             std::string("重复测试 重复测试"));
}

QTEST_GUILESS_MAIN(TestTextJoiner)
#include "test_text_joiner.moc"
