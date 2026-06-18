// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Committer.h"

#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace echoflow {

Committer::Committer(const Config& cfg, std::filesystem::path fcitxSocket)
    : cfg_(cfg)
    , fcitxSocket_(std::move(fcitxSocket))
{
}

std::pair<bool, std::string> Committer::commitText(const std::string& text)
{
    if (!cfg_.fcitxCommit) {
        return {false, "fcitx disabled"};
    }

    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        return {false, "socket"};
    }

    timeval timeout {0, 500000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_un clientAddr {};
    clientAddr.sun_family = AF_UNIX;
    std::string clientPath = "/tmp/echoflow-client-" + std::to_string(getpid()) + "-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sock";
    unlink(clientPath.c_str());
    strncpy(clientAddr.sun_path, clientPath.c_str(), sizeof(clientAddr.sun_path) - 1);
    if (bind(fd, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr)) < 0) {
        close(fd);
        unlink(clientPath.c_str());
        return {false, "bind"};
    }

    sockaddr_un serverAddr {};
    serverAddr.sun_family = AF_UNIX;
    strncpy(serverAddr.sun_path, fcitxSocket_.c_str(), sizeof(serverAddr.sun_path) - 1);
    std::string payload = "COMMIT\n" + text;
    if (sendto(fd, payload.data(), payload.size(), 0,
               reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr))
        < 0) {
        close(fd);
        unlink(clientPath.c_str());
        return {false, "sendto"};
    }

    char buf[256] = {};
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    close(fd);
    unlink(clientPath.c_str());
    if (n <= 0) {
        return {false, "timeout"};
    }

    std::string reply(buf, n);
    auto end = reply.find_last_not_of(" \t\r\n");
    reply = end == std::string::npos ? std::string() : reply.substr(0, end + 1);
    return {reply == "OK", reply};
}

}  // namespace echoflow
