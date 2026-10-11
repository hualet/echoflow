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
