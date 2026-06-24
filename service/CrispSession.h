// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_CRISP_SESSION_H
#define ECHOFLOW_CRISP_SESSION_H

#include <memory>
#include <string>
#include <vector>

struct crispasr_session;
struct crispasr_session_result;

namespace echoflow {

class CrispSession {
public:
    CrispSession(const std::string& modelPath, const std::string& backend,
                 int nThreads);
    ~CrispSession();

    CrispSession(const CrispSession&) = delete;
    CrispSession& operator=(const CrispSession&) = delete;

    bool isLoaded() const;
    void setLanguage(const std::string& lang);
    void setMaxNewTokens(int n);
    std::string transcribe(const float* pcm, int nSamples);

    static std::vector<float> readWavF32(const std::string& path);

private:
    struct Result {
        ~Result();
        crispasr_session_result* r = nullptr;
        int nSegments = 0;
        std::string text() const;
    };
    Result transcribeResult(const float* pcm, int nSamples);

    crispasr_session* session_ = nullptr;
};

}  // namespace echoflow

#endif
