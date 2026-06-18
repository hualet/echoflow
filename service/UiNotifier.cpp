// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UiNotifier.h"

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace echoflow {

UnixDatagramUiNotifier::UnixDatagramUiNotifier(std::filesystem::path socket)
    : socket_(std::move(socket))
{
}

void UnixDatagramUiNotifier::send(const std::string& message)
{
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        return;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_.c_str(), sizeof(addr.sun_path) - 1);
    sendto(fd, message.data(), message.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    close(fd);
}

}  // namespace echoflow
