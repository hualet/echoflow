// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UiNotifier.h"

#include "log.h"

#include <cerrno>
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
    const size_t separator = message.find(' ');
    const std::string type = message.substr(0, separator);
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        log("UI message send failed: type=" + type
            + ", bytes=" + std::to_string(message.size())
            + ", error=" + std::strerror(errno));
        return;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_.c_str(), sizeof(addr.sun_path) - 1);
    const ssize_t sent = sendto(fd, message.data(), message.size(), 0,
                                reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    const int sendError = errno;
    close(fd);
    if (sent < 0) {
        log("UI message send failed: type=" + type
            + ", bytes=" + std::to_string(message.size())
            + ", error=" + std::strerror(sendError));
        return;
    }
    // Focus traffic follows every keystroke; only log session messages.
    if (type == "FOCUS" || type == "REFOCUS" || type == "CARET" || type == "TYPED"
        || type == "BLUR") {
        return;
    }
    log("UI message sent: type=" + type
        + ", bytes=" + std::to_string(static_cast<size_t>(sent)));
}

}  // namespace echoflow
