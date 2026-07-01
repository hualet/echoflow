// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VadMetrics.h"

#include <algorithm>
#include <cstddef>

namespace echoflow {

namespace {

std::vector<TimeInterval> normalized(std::vector<TimeInterval> intervals)
{
    intervals.erase(std::remove_if(intervals.begin(), intervals.end(), [](const auto& value) {
        return value.endSeconds <= value.beginSeconds;
    }), intervals.end());
    std::sort(intervals.begin(), intervals.end(), [](const auto& left, const auto& right) {
        return left.beginSeconds < right.beginSeconds;
    });
    std::vector<TimeInterval> merged;
    for (const auto& value : intervals) {
        if (merged.empty() || value.beginSeconds > merged.back().endSeconds) {
            merged.push_back(value);
        } else {
            merged.back().endSeconds = std::max(merged.back().endSeconds, value.endSeconds);
        }
    }
    return merged;
}

double duration(const std::vector<TimeInterval>& intervals)
{
    double total = 0.0;
    for (const auto& value : intervals) total += value.endSeconds - value.beginSeconds;
    return total;
}

double intersectionDuration(const std::vector<TimeInterval>& left,
                            const std::vector<TimeInterval>& right)
{
    size_t i = 0;
    size_t j = 0;
    double total = 0.0;
    while (i < left.size() && j < right.size()) {
        const double begin = std::max(left[i].beginSeconds, right[j].beginSeconds);
        const double end = std::min(left[i].endSeconds, right[j].endSeconds);
        if (end > begin) total += end - begin;
        if (left[i].endSeconds < right[j].endSeconds) ++i;
        else ++j;
    }
    return total;
}

}  // namespace

VadMetrics evaluateVadIntervals(const std::vector<TimeInterval>& reference,
                                const std::vector<TimeInterval>& predicted)
{
    const auto truth = normalized(reference);
    const auto detected = normalized(predicted);
    const double overlap = intersectionDuration(truth, detected);

    VadMetrics metrics;
    metrics.speechSeconds = duration(truth);
    metrics.missedSpeechSeconds = metrics.speechSeconds - overlap;
    metrics.falseActivationSeconds = duration(detected) - overlap;

    std::vector<double> endpointDelays;
    for (const auto& expected : truth) {
        double bestOverlap = 0.0;
        double bestDelay = 0.0;
        for (const auto& actual : detected) {
            const double shared = std::min(expected.endSeconds, actual.endSeconds)
                - std::max(expected.beginSeconds, actual.beginSeconds);
            if (shared > bestOverlap) {
                bestOverlap = shared;
                bestDelay = actual.endSeconds - expected.endSeconds;
            }
        }
        if (bestOverlap > 0.0) endpointDelays.push_back(bestDelay);
    }
    if (!endpointDelays.empty()) {
        std::sort(endpointDelays.begin(), endpointDelays.end());
        const size_t middle = endpointDelays.size() / 2;
        metrics.medianEndpointDelaySeconds = endpointDelays.size() % 2
            ? endpointDelays[middle]
            : (endpointDelays[middle - 1] + endpointDelays[middle]) / 2.0;
    }
    return metrics;
}

StreamingLatencyMetrics simulateStreamingLatency(
    const std::vector<double>& segmentEndSeconds,
    const std::vector<double>& decodeMilliseconds,
    double recordingDurationSeconds)
{
    StreamingLatencyMetrics metrics;
    const size_t count = std::min(segmentEndSeconds.size(), decodeMilliseconds.size());
    double completion = 0.0;
    for (size_t i = 0; i < count; ++i) {
        completion = std::max(completion, segmentEndSeconds[i] * 1000.0)
            + decodeMilliseconds[i];
        if (i == 0) metrics.firstStableTextMs = completion;
    }
    metrics.lastCompletionMs = completion;
    metrics.stopLatencyMs = std::max(0.0, completion - recordingDurationSeconds * 1000.0);
    return metrics;
}

}  // namespace echoflow
