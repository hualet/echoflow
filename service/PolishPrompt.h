// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_POLISH_PROMPT_H
#define ECHOFLOW_POLISH_PROMPT_H

#include <cstddef>
#include <string>
#include <vector>

namespace echoflow {

struct PolishMessage {
    std::string role;
    std::string content;
};

// System prompt plus few-shot turns, followed by the transcript as the final
// user turn. The prefix is identical on every call so the chat session can
// reuse its KV cache and only prefill the transcript.
std::vector<PolishMessage> buildPolishMessages(const std::string& transcript);

// Chunk target for long transcripts. Greedy decoding degrades to a verbatim
// copy well below this length on the 1.7B model (see tests/data/
// polish_eval_zh.txt), so longer dictations are polished chunk by chunk.
constexpr std::size_t kPolishChunkMaxChars = 64;

// Splits a transcript into chunks of at most maxChars code points, cutting
// only at sentence or clause punctuation where the text offers it. Splitting
// and joining are lossless: joining the chunks reproduces the input exactly.
std::vector<std::string> splitTranscriptForPolish(const std::string& transcript,
    std::size_t maxChars);

// Chunks are polished independently and concatenated; each carries its own
// punctuation, and the trailing mark of the last chunk is handled by the
// commit path's punctuation stripping.
std::string joinPolishedChunks(const std::vector<std::string>& chunks);

// Generation budget for a transcript: enough to copy it with punctuation and
// light list formatting, small enough to cut off a runaway reply.
int polishMaxTokens(const std::string& transcript);

// Removes <think>...</think> blocks and surrounding whitespace.
std::string cleanPolishedText(const std::string& text);

// Rejects outputs that look like the model answered, translated, or dropped
// most of the transcript instead of cleaning it up, so the raw transcript is
// committed instead.
bool isPlausiblePolish(const std::string& raw, const std::string& polished);

std::size_t utf8CodePointCount(const std::string& text);

}  // namespace echoflow

#endif
