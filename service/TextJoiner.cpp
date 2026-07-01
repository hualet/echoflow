// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TextJoiner.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace echoflow {

namespace {

std::vector<size_t> codePointOffsets(const std::string& value)
{
    std::vector<size_t> offsets = {0};
    for (size_t i = 0; i < value.size();) {
        const unsigned char lead = static_cast<unsigned char>(value[i]);
        size_t width = 1;
        if ((lead & 0xe0) == 0xc0) width = 2;
        else if ((lead & 0xf0) == 0xe0) width = 3;
        else if ((lead & 0xf8) == 0xf0) width = 4;
        i = std::min(value.size(), i + width);
        offsets.push_back(i);
    }
    return offsets;
}

}  // namespace

std::string joinOverlappingText(const std::string& stable, const std::string& next)
{
    if (stable.empty()) return next;
    if (next.empty()) return stable;

    const auto leftOffsets = codePointOffsets(stable);
    const auto rightOffsets = codePointOffsets(next);
    const size_t maxCodePoints = std::min(leftOffsets.size(), rightOffsets.size()) - 1;
    size_t overlapBytes = 0;
    for (size_t count = maxCodePoints; count >= 2; --count) {
        const size_t leftBegin = leftOffsets[leftOffsets.size() - 1 - count];
        const size_t rightEnd = rightOffsets[count];
        if (stable.compare(leftBegin, stable.size() - leftBegin, next, 0, rightEnd) == 0) {
            overlapBytes = rightEnd;
            break;
        }
        if (count == 2) break;
    }
    if (overlapBytes > 0) return stable + next.substr(overlapBytes);
    return stable + " " + next;
}

}  // namespace echoflow
