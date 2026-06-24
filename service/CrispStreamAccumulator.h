// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CRISP_STREAM_ACCUMULATOR_H
#define ECHOFLOW_CRISP_STREAM_ACCUMULATOR_H

#include <optional>
#include <string>
#include <string_view>

namespace echoflow {

// Parses CrispASR --stream-json lines and accumulates finalized utterance text
// plus the current evolving partial. Pure logic: no I/O, fully unit-testable.
class CrispStreamAccumulator {
public:
    // Parse one --stream-json line. Returns text to push to the partial
    // callback, or nullopt for silence/malformed/no-change.
    std::optional<std::string> processEvent(std::string_view jsonLine);
    std::string finalText() const;
    void clear();

private:
    static bool needsSpace(const std::string& prev, const std::string& next);
    std::string finalized_;
    std::string currentPartial_;
};

}  // namespace echoflow

#endif
