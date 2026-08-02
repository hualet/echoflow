// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_LIVE_SEGMENTER_FACTORY_H
#define ECHOFLOW_LIVE_SEGMENTER_FACTORY_H

#include "Config.h"
#include "LiveSegmenter.h"

#include <memory>

namespace echoflow {

std::unique_ptr<ILiveSegmenter> makeLiveSegmenter(const Config& config);

}  // namespace echoflow

#endif
