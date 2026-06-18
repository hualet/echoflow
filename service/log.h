// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_LOG_H
#define ECHOFLOW_LOG_H

#include <cstdio>
#include <ctime>
#include <string>

namespace echoflow {

inline void log(const std::string& message)
{
    std::time_t t = std::time(nullptr);
    char ts[20];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::printf("[%s] %s\n", ts, message.c_str());
    std::fflush(stdout);
}

}  // namespace echoflow

#endif
