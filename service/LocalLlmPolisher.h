// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_LOCAL_LLM_POLISHER_H
#define ECHOFLOW_LOCAL_LLM_POLISHER_H

#include "Interfaces.h"

#include <mutex>
#include <string>
#include <thread>

struct crispasr_chat_session;

namespace echoflow {

// Rewrites transcripts with a small GGUF chat model through CrispASR's
// in-process chat ABI. Keep crispasr_chat.h usage isolated to this class.
class LocalLlmPolisher : public ITextPolisher {
public:
    LocalLlmPolisher(std::string modelPath, int threads);
    ~LocalLlmPolisher() override;

    // Loads the model on a background thread so the first dictation does not
    // pay the load cost. polish() waits for an in-flight load.
    void preload();

    std::string polish(const std::string& text,
        const std::function<bool()>& shouldAbort) override;

private:
    static bool continueGeneration(void* user);
    void ensureLoadedLocked();
    void warmUpLocked();

    std::string modelPath_;
    int threads_;

    std::mutex mutex_;
    crispasr_chat_session* session_ = nullptr;
    bool loadAttempted_ = false;
    std::string loadError_;
    // Only read by continueGeneration() on the thread running polish().
    const std::function<bool()>* shouldAbort_ = nullptr;

    std::thread preloadThread_;
};

}  // namespace echoflow

#endif
