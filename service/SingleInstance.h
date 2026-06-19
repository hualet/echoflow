// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_SINGLEINSTANCE_H
#define ECHOFLOW_SINGLEINSTANCE_H

#include <filesystem>
#include <string>

namespace echoflow {

class SingleInstanceGuard {
public:
    SingleInstanceGuard() = default;
    ~SingleInstanceGuard();

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    SingleInstanceGuard(SingleInstanceGuard&& other) noexcept;
    SingleInstanceGuard& operator=(SingleInstanceGuard&& other) noexcept;

    bool acquire(const std::filesystem::path& socketPath, std::string* error);
    bool isAcquired() const;
    int fd() const;

private:
    void reset();
    bool bindSocket(const std::filesystem::path& socketPath, std::string* error);

    int fd_ = -1;
    std::filesystem::path socketPath_;
};

} // namespace echoflow

#endif
