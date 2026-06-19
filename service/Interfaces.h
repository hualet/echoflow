// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_INTERFACES_H
#define ECHOFLOW_INTERFACES_H

#include <filesystem>
#include <string>
#include <utility>

namespace echoflow {

class IRecorder {
public:
    virtual ~IRecorder() = default;
    virtual void start() = 0;
    virtual std::filesystem::path stop() = 0;
};

class IAsrEngine {
public:
    virtual ~IAsrEngine() = default;
    virtual std::string transcribe(const std::filesystem::path& audio) = 0;
};

class ILiveVoicePipeline {
public:
    virtual ~ILiveVoicePipeline() = default;
    virtual void start() = 0;
    virtual std::string finish() = 0;
    virtual void cancel() = 0;
};

class ICommitter {
public:
    virtual ~ICommitter() = default;
    virtual std::pair<bool, std::string> commitText(const std::string& text) = 0;
};

class IUiNotifier {
public:
    virtual ~IUiNotifier() = default;
    virtual void send(const std::string& message) = 0;
};

}  // namespace echoflow

#endif
