// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Server.h"

#include "SingleInstance.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace echoflow {

namespace {

constexpr std::array<const char*, 5> kAllowedCommands = {"FOCUS", "BLUR", "CTRL_DOWN", "TYPED", "CANCEL"};

std::string trim(const std::string& value)
{
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string upper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

bool isAllowed(const std::string& command)
{
    std::string trimmed = trim(command);
    auto space = trimmed.find(' ');
    std::string verb = upper(space == std::string::npos ? trimmed : trimmed.substr(0, space));
    return std::find(kAllowedCommands.begin(), kAllowedCommands.end(), verb) != kAllowedCommands.end();
}

}  // namespace

Server::Server(Config cfg, VoiceSession& session)
    : cfg_(std::move(cfg))
    , session_(session)
{
}

int Server::run()
{
    auto serverPath = controlSocketPath(cfg_);
    SingleInstanceGuard instance;
    std::string error;
    if (!instance.acquire(serverPath, &error)) {
        log(error);
        return 1;
    }
    int fd = instance.fd();
    log("EchoFlow service listening on " + serverPath.string());

    char buf[4096];
    for (;;) {
        sockaddr_un peer {};
        socklen_t peerLen = sizeof(peer);
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (n <= 0) {
            continue;
        }
        buf[n] = '\0';
        std::string command = trim(std::string(buf, n));
        std::string reply = isAllowed(command) ? session_.handleCommand(command) + "\n"
                                               : "ERR unknown-command\n";
        if (peerLen > 0) {
            sendto(fd, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&peer), peerLen);
        }
    }
}

}  // namespace echoflow
