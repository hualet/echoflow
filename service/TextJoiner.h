// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_TEXT_JOINER_H
#define ECHOFLOW_TEXT_JOINER_H

#include <string>

namespace echoflow {

std::string joinOverlappingText(const std::string& stable, const std::string& next);

}  // namespace echoflow

#endif
