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
    void keepsShortTranscriptWhole();
    void splitsAtSentenceBoundsAndKeepsSpaces();
    void hardSplitsRunOnText();
    void splitsOversizedSentenceAtClauseMarks();
    void roundTripsRealTranscriptInChunks();
    void joinsChunksVerbatim();
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
    QVERIFY(isPlausiblePolish("这个脚本是用派森写的，跑在林纳克斯上面。",
        "这个脚本是用Python写的，跑在Linux上面。"));
    QVERIFY(isPlausiblePolish("我们大概有百分之三十的用户是在晚上八点到十点之间用的。",
        "我们大概有30%的用户是在晚上8点到10点之间用的。"));
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

void TestPolishPrompt::keepsShortTranscriptWhole()
{
    const auto punctuated = splitTranscriptForPolish("你好。", 100);
    QCOMPARE(punctuated.size(), std::size_t(1));
    QCOMPARE(punctuated.front(), std::string("你好。"));
    const auto plain = splitTranscriptForPolish("你好", 100);
    QCOMPARE(plain.size(), std::size_t(1));
    QCOMPARE(plain.front(), std::string("你好"));
    QVERIFY(splitTranscriptForPolish("", 100).empty());
}

void TestPolishPrompt::splitsAtSentenceBoundsAndKeepsSpaces()
{
    // The space after 。 belongs to the sentence that precedes it, so a
    // chunk boundary never starts with a stray space.
    const auto chunks = splitTranscriptForPolish("AAAA。 BBBB。 CCCC。", 12);
    QCOMPARE(chunks.size(), std::size_t(2));
    QCOMPARE(chunks[0], std::string("AAAA。 BBBB。 "));
    QCOMPARE(chunks[1], std::string("CCCC。"));
}

void TestPolishPrompt::hardSplitsRunOnText()
{
    const auto repeated = [](int times) {
        std::string out;
        for (int i = 0; i < times; ++i) {
            out += "好";
        }
        return out;
    };
    const auto chunks = splitTranscriptForPolish(repeated(25), 10);
    QCOMPARE(chunks.size(), std::size_t(3));
    QCOMPARE(chunks[0], repeated(10));
    QCOMPARE(chunks[1], repeated(10));
    QCOMPARE(chunks[2], repeated(5));
}

void TestPolishPrompt::splitsOversizedSentenceAtClauseMarks()
{
    const auto chunks = splitTranscriptForPolish("AAAAAAAAAA，BBBBBBBBBB，CCCC", 11);
    QCOMPARE(chunks.size(), std::size_t(3));
    QCOMPARE(chunks[0], std::string("AAAAAAAAAA，"));
    QCOMPARE(chunks[1], std::string("BBBBBBBBBB，"));
    QCOMPARE(chunks[2], std::string("CCCC"));
}

void TestPolishPrompt::roundTripsRealTranscriptInChunks()
{
    // A real failing dictation from 2026-10-17: 179 code points that the
    // model used to copy verbatim when polished in one piece.
    const std::string transcript =
        "能够满足他们行业的数据安全合规要求的一个部署方案，那这个时候其实都是要解决他们"
        "的这个安全。 的这个安全顾虑的一个需求，那同时的话呢，就是真正的进入到这些企业的"
        "时候，我觉得他们可能，呃，因为。 呃，因为对他们来讲，他们这个场景中的这个编程场"
        "景，不占他们企业中的这个很大的一个占比。因为他们本质上还是。 还是一些，要么就是"
        "一些业务流，要么就是一些啊，资金流。所以说";
    const auto chunks = splitTranscriptForPolish(transcript, kPolishChunkMaxChars);
    QCOMPARE(chunks.size(), std::size_t(4));
    for (const auto& chunk : chunks) {
        QVERIFY(utf8CodePointCount(chunk) <= kPolishChunkMaxChars);
    }
    QCOMPARE(joinPolishedChunks(chunks), transcript);
}

void TestPolishPrompt::joinsChunksVerbatim()
{
    QCOMPARE(joinPolishedChunks({}), std::string());
    QCOMPARE(joinPolishedChunks({"A。 ", "B。"}), std::string("A。 B。"));
}

QTEST_APPLESS_MAIN(TestPolishPrompt)
#include "test_polish_prompt.moc"
