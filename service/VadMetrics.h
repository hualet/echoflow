// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_VAD_METRICS_H
#define ECHOFLOW_VAD_METRICS_H

#include <vector>

namespace echoflow {

struct TimeInterval {
    double beginSeconds = 0.0;
    double endSeconds = 0.0;
};

struct VadMetrics {
    double speechSeconds = 0.0;
    double missedSpeechSeconds = 0.0;
    double falseActivationSeconds = 0.0;
    double medianEndpointDelaySeconds = 0.0;
};

VadMetrics evaluateVadIntervals(const std::vector<TimeInterval>& reference,
                                const std::vector<TimeInterval>& predicted);

}  // namespace echoflow

#endif
