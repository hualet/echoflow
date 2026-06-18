// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SERVER_H
#define ECHOFLOW_SERVER_H

#include "Config.h"
#include "VoiceSession.h"

namespace echoflow {

class Server {
public:
    Server(Config cfg, VoiceSession& session);
    int run();

private:
    Config cfg_;
    VoiceSession& session_;
};

}  // namespace echoflow

#endif
