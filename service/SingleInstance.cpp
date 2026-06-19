// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SingleInstance.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace echoflow {
namespace fs = std::filesystem;

namespace {

bool pathFitsUnixSocket(const fs::path& path, std::string* error)
{
    sockaddr_un addr {};
    const std::string value = path.string();
    if (value.size() >= sizeof(addr.sun_path)) {
        if (error) {
            *error = "socket path is too long: " + value;
        }
        return false;
    }
    return true;
}

bool socketHasLivePeer(const fs::path& path)
{
    if (!pathFitsUnixSocket(path, nullptr)) {
        return false;
    }

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    const std::string value = path.string();
    std::strncpy(addr.sun_path, value.c_str(), sizeof(addr.sun_path) - 1);
    const char probe[] = "PING";
    const bool live = sendto(fd, probe, sizeof(probe) - 1, 0,
                             reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) >= 0;
    close(fd);
    return live;
}

} // namespace

SingleInstanceGuard::~SingleInstanceGuard()
{
    reset();
}

SingleInstanceGuard::SingleInstanceGuard(SingleInstanceGuard&& other) noexcept
    : fd_(other.fd_)
    , socketPath_(std::move(other.socketPath_))
{
    other.fd_ = -1;
}

SingleInstanceGuard& SingleInstanceGuard::operator=(SingleInstanceGuard&& other) noexcept
{
    if (this != &other) {
        reset();
        fd_ = other.fd_;
        socketPath_ = std::move(other.socketPath_);
        other.fd_ = -1;
    }
    return *this;
}

bool SingleInstanceGuard::acquire(const fs::path& socketPath, std::string* error)
{
    reset();
    fs::create_directories(socketPath.parent_path());
    if (bindSocket(socketPath, error)) {
        return true;
    }

    if (errno != EADDRINUSE) {
        return false;
    }

    if (socketHasLivePeer(socketPath)) {
        if (error) {
            *error = "echoflow-service is already running";
        }
        return false;
    }

    unlink(socketPath.c_str());
    return bindSocket(socketPath, error);
}

bool SingleInstanceGuard::isAcquired() const
{
    return fd_ >= 0;
}

int SingleInstanceGuard::fd() const
{
    return fd_;
}

void SingleInstanceGuard::reset()
{
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    if (!socketPath_.empty()) {
        unlink(socketPath_.c_str());
        socketPath_.clear();
    }
}

bool SingleInstanceGuard::bindSocket(const fs::path& socketPath, std::string* error)
{
    if (!pathFitsUnixSocket(socketPath, error)) {
        errno = ENAMETOOLONG;
        return false;
    }

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        if (error) {
            *error = std::string("control socket creation failed: ") + std::strerror(errno);
        }
        return false;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    const std::string value = socketPath.string();
    std::strncpy(addr.sun_path, value.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int savedErrno = errno;
        if (error) {
            *error = "control socket bind failed: " + value + ": " + std::strerror(savedErrno);
        }
        close(fd);
        errno = savedErrno;
        return false;
    }

    chmod(value.c_str(), 0600);
    fd_ = fd;
    socketPath_ = socketPath;
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace echoflow
