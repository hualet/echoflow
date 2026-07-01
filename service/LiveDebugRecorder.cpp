// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LiveDebugRecorder.h"

#include <chrono>
#include <fstream>
#include <stdexcept>

namespace echoflow {

namespace {

void writeU16(std::ostream& out, uint16_t value)
{
    const char bytes[] = {static_cast<char>(value & 0xff),
                          static_cast<char>((value >> 8) & 0xff)};
    out.write(bytes, sizeof(bytes));
}

void writeU32(std::ostream& out, uint32_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xff), static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff), static_cast<char>((value >> 24) & 0xff)};
    out.write(bytes, sizeof(bytes));
}

std::string jsonEscape(const std::string& value)
{
    std::string escaped;
    for (const char c : value) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

std::string joinText(const std::vector<std::string>& results)
{
    std::string joined;
    for (const auto& result : results) {
        if (result.empty()) continue;
        if (!joined.empty()) joined += ' ';
        joined += result;
    }
    return joined;
}

}  // namespace

LiveDebugRecorder::LiveDebugRecorder(std::filesystem::path directory)
    : directory_(std::move(directory))
{
}

void LiveDebugRecorder::start()
{
    std::filesystem::create_directories(directory_);
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const std::string stem = "live-debug-" + std::to_string(stamp);
    wavPath_ = directory_ / (stem + ".wav");
    metadataPath_ = directory_ / (stem + ".json");
    samples_.clear();
    active_ = true;
}

void LiveDebugRecorder::append(const int16_t* samples, size_t count)
{
    if (!active_ || !samples || count == 0) return;
    samples_.insert(samples_.end(), samples, samples + count);
}

void LiveDebugRecorder::finish(const LivePipelineMetrics& metrics,
                               const std::vector<std::string>& results)
{
    if (!active_) return;
    const uint64_t dataBytes64 = samples_.size() * sizeof(int16_t);
    if (dataBytes64 > UINT32_MAX - 36) {
        throw std::runtime_error("live debug WAV is too large");
    }
    const uint32_t dataBytes = static_cast<uint32_t>(dataBytes64);

    std::ofstream wav(wavPath_, std::ios::binary);
    if (!wav) throw std::runtime_error("cannot create live debug WAV");
    wav.write("RIFF", 4);
    writeU32(wav, 36 + dataBytes);
    wav.write("WAVEfmt ", 8);
    writeU32(wav, 16);
    writeU16(wav, 1);
    writeU16(wav, 1);
    writeU32(wav, 16000);
    writeU32(wav, 16000 * sizeof(int16_t));
    writeU16(wav, sizeof(int16_t));
    writeU16(wav, 16);
    wav.write("data", 4);
    writeU32(wav, dataBytes);
    wav.write(reinterpret_cast<const char*>(samples_.data()), dataBytes);
    if (!wav) throw std::runtime_error("cannot write live debug WAV");

    std::ofstream metadata(metadataPath_);
    if (!metadata) throw std::runtime_error("cannot create live debug metadata");
    metadata << "{\"captured_samples\":" << metrics.inputSamples
             << ",\"segment_samples\":" << metrics.segmentSamples
             << ",\"enqueued_segments\":" << metrics.enqueuedSegments
             << ",\"asr_queue_high_water_mark\":" << metrics.asrQueueHighWaterMark
             << ",\"audio_dropped\":" << (metrics.audioDropped ? "true" : "false")
             << ",\"final_text\":\"" << jsonEscape(joinText(results)) << "\"}\n";
    active_ = false;
}

const std::filesystem::path& LiveDebugRecorder::wavPath() const
{
    return wavPath_;
}

const std::filesystem::path& LiveDebugRecorder::metadataPath() const
{
    return metadataPath_;
}

}  // namespace echoflow
