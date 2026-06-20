// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SEGMENT_TEXT_ACCUMULATOR_H
#define ECHOFLOW_SEGMENT_TEXT_ACCUMULATOR_H

#include <map>
#include <string>

namespace echoflow {

class SegmentTextAccumulator {
public:
    void append(int sequence, const std::string& text);
    std::string text() const;
    void clear();

private:
    static std::string trim(const std::string& text);
    static bool needsSpace(const std::string& previous, const std::string& next);
    static bool isAsciiWordByte(unsigned char byte);
    static bool isAsciiTrailingPunctuation(unsigned char byte);

    std::map<int, std::string> segments_;
};

}  // namespace echoflow

#endif
