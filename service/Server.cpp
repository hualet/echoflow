// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Server.h"

#include "log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace echoflow {
namespace fs = std::filesystem;

namespace {

constexpr std::array<const char*, 4> kAllowedCommands = {"FOCUS", "BLUR", "CTRL_DOWN", "TYPED"};

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
    fs::create_directories(serverPath.parent_path());
    unlink(serverPath.c_str());

    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        log("control socket creation failed");
        return 1;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, serverPath.c_str(), sizeof(addr.sun_path) - 1);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log("control socket bind failed: " + serverPath.string());
        close(fd);
        return 1;
    }
    chmod(serverPath.c_str(), 0600);
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
