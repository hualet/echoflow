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
    void rejectsAnswersAndDroppedContent();
    void acceptsTypicalCleanups();
};

void TestPolishPrompt::rejectsAnswersAndDroppedContent()
{
    // Seen live: Qwen3-0.6B answered the transcript.
    QVERIFY(!isPlausiblePolish("到底是不是弄润色啊？ 是。", "是的。"));
    QVERIFY(!isPlausiblePolish("你知道明天北京的天气怎么样吗", "明天北京晴，最高气温二十度左右。"));
    QVERIFY(!isPlausiblePolish("OK，那个，let me check一下，我们的deadline是不是下周三。",
        "好的，让我查一下，我们的截止日期是不是在下周三。"));
    QVERIFY(!isPlausiblePolish("第一句话说完了。然后我又说了第二句话，还有第三句话。", "第一句话说完了。"));
}

void TestPolishPrompt::acceptsTypicalCleanups()
{
    QVERIFY(isPlausiblePolish("嗯，那个，我想问一下，就是，这个周五的会议是几点开始的？",
        "这个周五的会议是几点开始的？"));
    QVERIFY(isPlausiblePolish("我们下周一，啊不对，下周二去上海出差，然后住两个晚上。",
        "我们下周二去上海出差，住两个晚上。"));
    QVERIFY(isPlausiblePolish("你是不是能忍色啊？能不能忍色？", "你是不是能润色啊？"));
    QVERIFY(isPlausiblePolish("今天要做的事情有，第一个是修那个登录的bug，第二个是写周报，然后还要跟产品开个会。",
        "今天要做的事情有：\n1. 修登录的bug\n2. 写周报\n3. 跟产品开个会"));
    QVERIFY(isPlausiblePolish("OK，那个，let me check一下", "OK，let me check一下"));
    QVERIFY(isPlausiblePolish("好的", "好的。"));
}

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
    QVERIFY(first.back().content.size() > std::string("第一句").size());
    QVERIFY(first.back().content.compare(first.back().content.size() - 9, 9, "第一句") == 0);
    QVERIFY(second.back().content.compare(second.back().content.size() - 9, 9, "第二句") == 0);
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
