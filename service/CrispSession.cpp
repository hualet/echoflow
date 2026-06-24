// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CrispSession.h"

extern "C" {
#include "crispasr_session.h"
}

#include "log.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace echoflow {

// ---------------------------------------------------------------------------
// WAV reader
// ---------------------------------------------------------------------------

static uint32_t readU32LE(std::istream& in)
{
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    return static_cast<uint32_t>(b[0])
        | (static_cast<uint32_t>(b[1]) << 8)
        | (static_cast<uint32_t>(b[2]) << 16)
        | (static_cast<uint32_t>(b[3]) << 24);
}

static uint16_t readU16LE(std::istream& in)
{
    unsigned char b[2];
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
}

std::vector<float> CrispSession::readWavF32(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        log("failed to open wav: " + path);
        return {};
    }

    char riff[5] = {};
    in.read(riff, 4);
    if (std::memcmp(riff, "RIFF", 4) != 0) {
        log("not a WAV file: " + path);
        return {};
    }
    readU32LE(in);  // file size - 8

    char wave[5] = {};
    in.read(wave, 4);
    if (std::memcmp(wave, "WAVE", 4) != 0) {
        log("not a WAV file: " + path);
        return {};
    }

    uint16_t channels = 1;
    uint16_t bitsPerSample = 16;
    uint32_t dataSize = 0;

    while (in) {
        char chunkId[5] = {};
        in.read(chunkId, 4);
        uint32_t chunkSize = readU32LE(in);
        if (!in) break;

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            if (chunkSize < 16) break;
            uint16_t audioFormat = readU16LE(in);
            if (audioFormat != 1) {
                log("unsupported WAV format (not PCM): " + path);
                return {};
            }
            channels = readU16LE(in);
            /*uint32_t sampleRate =*/ readU32LE(in);
            /*uint32_t byteRate =*/ readU32LE(in);
            /*uint16_t blockAlign =*/ readU16LE(in);
            bitsPerSample = readU16LE(in);
            if (chunkSize > 16) {
                in.seekg(chunkSize - 16, std::ios::cur);
            }
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataSize = chunkSize;
            break;
        } else {
            in.seekg(chunkSize, std::ios::cur);
        }
    }

    if (dataSize == 0) {
        log("no data chunk in WAV: " + path);
        return {};
    }

    const size_t nSamples = dataSize / (channels * (bitsPerSample / 8));
    std::vector<float> pcm;
    pcm.reserve(nSamples / channels);

    std::vector<int16_t> raw(nSamples);
    in.read(reinterpret_cast<char*>(raw.data()), dataSize);
    size_t n = in.gcount() / sizeof(int16_t);

    for (size_t i = 0; i < n; i += channels) {
        int32_t sum = 0;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            sum += raw[i + ch];
        }
        float sample = static_cast<float>(sum / channels) / 32768.0f;
        pcm.push_back(sample);
    }

    return pcm;
}

// ---------------------------------------------------------------------------
// CrispSession
// ---------------------------------------------------------------------------

CrispSession::CrispSession(const std::string& modelPath, const std::string& backend,
                           int nThreads)
{
    session_ = crispasr_session_open_explicit(modelPath.c_str(), backend.c_str(),
                                               nThreads);
    if (!session_) {
        log("crispasr_session_open_explicit failed for: " + modelPath);
    }
}

CrispSession::~CrispSession()
{
    if (session_) {
        crispasr_session_close(session_);
    }
}

bool CrispSession::isLoaded() const
{
    return session_ != nullptr;
}

void CrispSession::setLanguage(const std::string& lang)
{
    if (session_) {
        crispasr_session_set_source_language(session_, lang.c_str());
    }
}

void CrispSession::setMaxNewTokens(int n)
{
    if (session_ && n > 0) {
        crispasr_session_set_max_new_tokens(session_, n);
    }
}

CrispSession::Result CrispSession::transcribeResult(const float* pcm, int nSamples)
{
    Result res;
    res.r = crispasr_session_transcribe(session_, pcm, nSamples);
    if (res.r) {
        res.nSegments = crispasr_session_result_n_segments(res.r);
    }
    return res;
}

std::string CrispSession::transcribe(const float* pcm, int nSamples)
{
    return transcribeResult(pcm, nSamples).text();
}

// ---------------------------------------------------------------------------
// CrispSession::Result
// ---------------------------------------------------------------------------

CrispSession::Result::~Result()
{
    if (r) {
        crispasr_session_result_free(r);
    }
}

std::string CrispSession::Result::text() const
{
    std::string out;
    for (int i = 0; i < nSegments; ++i) {
        const char* seg = crispasr_session_result_segment_text(r, i);
        if (seg) {
            if (i > 0 && !out.empty() && out.back() != '\n') {
                out += " ";
            }
            out += seg;
        }
    }
    return out;
}

}  // namespace echoflow
