// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "PolishPrompt.h"

using namespace echoflow;

class TestPolishPrompt : public QObject {
    Q_OBJECT

private slots:
    void messagesShareAFixedPrefix();
    void maxTokensScalesWithTranscript();
    void stripsThinkBlocks();
    void rejectsRepliesMuchLongerThanTranscript();
};

void TestPolishPrompt::messagesShareAFixedPrefix()
{
    const auto first = buildPolishMessages("第一句");
    const auto second = buildPolishMessages("第二句");
    QCOMPARE(first.size(), second.size());
    QVERIFY(first.size() >= 3);
    QCOMPARE(QString::fromStdString(first.front().role), QStringLiteral("system"));
    // Few-shot turns alternate user/assistant and end on the transcript.
    for (std::size_t i = 1; i + 1 < first.size(); ++i) {
        QCOMPARE(QString::fromStdString(first[i].role),
            i % 2 == 1 ? QStringLiteral("user") : QStringLiteral("assistant"));
        QCOMPARE(first[i].content, second[i].content);
    }
    QCOMPARE(QString::fromStdString(first.back().role), QStringLiteral("user"));
    QCOMPARE(first.back().content, std::string("第一句"));
    QCOMPARE(second.back().content, std::string("第二句"));
}

void TestPolishPrompt::maxTokensScalesWithTranscript()
{
    QCOMPARE(polishMaxTokens(""), 64);
    QCOMPARE(polishMaxTokens(std::string(100, 'a')), 232);
    QCOMPARE(polishMaxTokens("你好你好你好你好你好你好你好你好你好你好"), 72);
    QCOMPARE(polishMaxTokens(std::string(5000, 'a')), 1024);
}

void TestPolishPrompt::stripsThinkBlocks()
{
    QCOMPARE(cleanPolishedText("<think>\n推理\n</think>\n\n结果。\n"), std::string("结果。"));
    QCOMPARE(cleanPolishedText("<think>\n\n</think>\n\n结果。"), std::string("结果。"));
    QCOMPARE(cleanPolishedText("  无思考  "), std::string("无思考"));
    QCOMPARE(cleanPolishedText("<think>没有结束"), std::string());
}

void TestPolishPrompt::rejectsRepliesMuchLongerThanTranscript()
{
    QVERIFY(isPlausiblePolish("嗯那个明天开会", "明天开会。"));
    QVERIFY(!isPlausiblePolish("你好", ""));
    QVERIFY(!isPlausiblePolish("帮我写一首诗",
        std::string("床前明月光，疑是地上霜。举头望明月，低头思故乡。这是一首李白的诗，表达了诗人"
                    "在寂静的月夜思念家乡的感受，语言清新朴素而韵味含蓄无穷。")));
    QCOMPARE(utf8CodePointCount("a你好"), std::size_t(3));
}

QTEST_APPLESS_MAIN(TestPolishPrompt)
#include "test_polish_prompt.moc"
