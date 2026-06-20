// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SegmentTextAccumulator.h"

#include <cctype>

namespace echoflow {

void SegmentTextAccumulator::append(int sequence, const std::string& text) {
    const std::string trimmed = trim(text);
    if (trimmed.empty()) {
        return;
    }

    segments_[sequence] = trimmed;
}

std::string SegmentTextAccumulator::text() const {
    std::string result;
    for (const auto& [sequence, segment] : segments_) {
        (void)sequence;
        if (result.empty()) {
            result = segment;
            continue;
        }

        if (needsSpace(result, segment)) {
            result += ' ';
        }
        result += segment;
    }

    return result;
}

void SegmentTextAccumulator::clear() {
    segments_.clear();
}

std::string SegmentTextAccumulator::trim(const std::string& text) {
    size_t start = 0;
    while (start < text.size()
           && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    size_t end = text.size();
    while (end > start
           && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(start, end - start);
}

bool SegmentTextAccumulator::needsSpace(const std::string& previous,
                                        const std::string& next) {
    if (previous.empty() || next.empty()) {
        return false;
    }

    const unsigned char previousByte = static_cast<unsigned char>(previous.back());
    const unsigned char nextByte = static_cast<unsigned char>(next.front());
    if (!isAsciiWordByte(nextByte)) {
        return false;
    }

    return isAsciiWordByte(previousByte) || isAsciiTrailingPunctuation(previousByte);
}

bool SegmentTextAccumulator::isAsciiWordByte(unsigned char byte) {
    return byte < 128 && std::isalnum(byte) != 0;
}

bool SegmentTextAccumulator::isAsciiTrailingPunctuation(unsigned char byte) {
    return byte == ',' || byte == '.' || byte == ':' || byte == ';'
        || byte == '!' || byte == '?';
}

}  // namespace echoflow
