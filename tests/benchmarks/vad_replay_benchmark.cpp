// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AudioSegmenter.h"
#include "Config.h"
#include "CrispSession.h"
#include "VadMetrics.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

struct ManifestEntry {
    fs::path audio;
    QString reference;
    QString condition;
    std::vector<echoflow::TimeInterval> speech;
};

std::vector<ManifestEntry> readManifest(const fs::path& path)
{
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("cannot open manifest: " + path.string());
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        throw std::runtime_error("manifest must be a JSON array: " + error.errorString().toStdString());
    }

    std::vector<ManifestEntry> entries;
    for (const auto& value : document.array()) {
        const QJsonObject object = value.toObject();
        ManifestEntry entry;
        entry.audio = object.value("audio").toString().toStdString();
        if (entry.audio.is_relative()) entry.audio = path.parent_path() / entry.audio;
        entry.reference = object.value("reference").toString();
        entry.condition = object.value("condition").toString();
        for (const auto& intervalValue : object.value("speech").toArray()) {
            const QJsonArray interval = intervalValue.toArray();
            if (interval.size() == 2) {
                entry.speech.push_back({interval[0].toDouble(), interval[1].toDouble()});
            }
        }
        if (entry.audio.empty()) throw std::runtime_error("manifest entry has no audio path");
        entries.push_back(std::move(entry));
    }
    return entries;
}

int editDistance(const QString& left, const QString& right)
{
    std::vector<int> previous(static_cast<size_t>(right.size() + 1));
    std::vector<int> current(previous.size());
    for (int j = 0; j <= right.size(); ++j) previous[static_cast<size_t>(j)] = j;
    for (int i = 1; i <= left.size(); ++i) {
        current[0] = i;
        for (int j = 1; j <= right.size(); ++j) {
            const int substitution = previous[static_cast<size_t>(j - 1)]
                + (left[i - 1] == right[j - 1] ? 0 : 1);
            current[static_cast<size_t>(j)] = std::min({
                previous[static_cast<size_t>(j)] + 1,
                current[static_cast<size_t>(j - 1)] + 1,
                substitution});
        }
        std::swap(previous, current);
    }
    return previous.back();
}

QJsonArray intervalsToJson(const std::vector<echoflow::TimeInterval>& intervals)
{
    QJsonArray result;
    for (const auto& interval : intervals) {
        result.append(QJsonArray({interval.beginSeconds, interval.endSeconds}));
    }
    return result;
}

void writeJsonLine(const QJsonObject& object)
{
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    std::fputc('\n', stdout);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    std::optional<fs::path> manifestPath;
    std::optional<fs::path> configPath;
    std::string backend = "energy";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--manifest" && i + 1 < argc) manifestPath = fs::path(argv[++i]);
        else if (arg == "--config" && i + 1 < argc) configPath = fs::path(argv[++i]);
        else if (arg == "--backend" && i + 1 < argc) backend = argv[++i];
        else {
            std::fprintf(stderr, "Usage: vad_replay_benchmark --manifest FILE [--backend energy] [--config FILE]\n");
            return 2;
        }
    }
    if (!manifestPath || backend != "energy") {
        std::fprintf(stderr, "the current baseline supports --backend energy\n");
        return 2;
    }

    try {
        std::unique_ptr<echoflow::CrispSession> session;
        if (configPath) {
            const echoflow::Config config = echoflow::loadDtkConf(*configPath);
            session = std::make_unique<echoflow::CrispSession>(
                config.crispModelPath, config.crispBackend, config.crispThreads);
            if (!session->isLoaded()) throw std::runtime_error("cannot load ASR model");
            const std::string language = config.language.value_or("");
            if (!language.empty()) session->setLanguage(language == "Chinese" ? "zh" : language);
        }

        double totalSpeech = 0.0;
        double totalMissed = 0.0;
        double totalFalse = 0.0;
        int totalReferenceChars = 0;
        int totalErrors = 0;
        int clips = 0;
        int labelledClips = 0;
        for (const ManifestEntry& entry : readManifest(*manifestPath)) {
            const std::vector<float> f32 = echoflow::CrispSession::readWavF32(entry.audio.string());
            if (f32.empty()) throw std::runtime_error("empty or unreadable WAV: " + entry.audio.string());
            std::vector<int16_t> pcm;
            pcm.reserve(f32.size());
            for (float sample : f32) {
                pcm.push_back(static_cast<int16_t>(std::lround(
                    std::clamp(sample, -1.0f, 0.999969f) * 32768.0f)));
            }

            echoflow::AudioSegmenter segmenter(echoflow::AudioSegmenterConfig{});
            std::vector<echoflow::AudioSegment> segments = segmenter.append(pcm.data(), pcm.size());
            if (auto tail = segmenter.flush()) segments.push_back(std::move(*tail));
            std::vector<echoflow::TimeInterval> predicted;
            QString transcript;
            double decodeMs = 0.0;
            for (const auto& segment : segments) {
                predicted.push_back({segment.beginSample / 16000.0, segment.endSample / 16000.0});
                if (session) {
                    std::vector<float> audio;
                    audio.reserve(segment.samples.size());
                    for (int16_t sample : segment.samples) audio.push_back(sample / 32768.0f);
                    const auto started = Clock::now();
                    const QString text = QString::fromStdString(
                        session->transcribe(audio.data(), static_cast<int>(audio.size())));
                    decodeMs += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
                    if (!transcript.isEmpty() && !text.isEmpty()) transcript += ' ';
                    transcript += text;
                }
            }

            const echoflow::VadMetrics metrics =
                echoflow::evaluateVadIntervals(entry.speech, predicted);
            const int errors = entry.reference.isEmpty() ? 0 : editDistance(entry.reference, transcript);
            const double cer = entry.reference.isEmpty()
                ? 0.0 : static_cast<double>(errors) / entry.reference.size();
            QJsonObject output {
                {"type", "clip"}, {"backend", QString::fromStdString(backend)},
                {"audio", QString::fromStdString(entry.audio.string())},
                {"condition", entry.condition}, {"duration_s", pcm.size() / 16000.0},
                {"segments", intervalsToJson(predicted)},
                {"decode_ms", decodeMs}, {"transcript", transcript}, {"cer", cer}
            };
            if (!entry.speech.empty()) {
                output.insert("speech_s", metrics.speechSeconds);
                output.insert("missed_speech_s", metrics.missedSpeechSeconds);
                output.insert("false_activation_s", metrics.falseActivationSeconds);
                output.insert("endpoint_delay_median_s", metrics.medianEndpointDelaySeconds);
                totalSpeech += metrics.speechSeconds;
                totalMissed += metrics.missedSpeechSeconds;
                totalFalse += metrics.falseActivationSeconds;
                ++labelledClips;
            }
            writeJsonLine(output);
            totalReferenceChars += entry.reference.size();
            totalErrors += errors;
            ++clips;
        }

        writeJsonLine(QJsonObject {
            {"type", "summary"}, {"backend", QString::fromStdString(backend)},
            {"clips", clips},
            {"labelled_clips", labelledClips},
            {"miss_rate", totalSpeech > 0.0 ? totalMissed / totalSpeech : 0.0},
            {"false_activation_s", totalFalse},
            {"cer", totalReferenceChars > 0
                ? static_cast<double>(totalErrors) / totalReferenceChars : 0.0}
        });
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
