// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "Config.h"

#include <stdexcept>
#include <vector>

using namespace echoflow;

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    void defaultConfigHasExpectedFields();
    void crispThreadsFollowPhysicalCores();
    void expandPathResolvesHome();
    void normalizeModelNameMapsVariants();
    void loadDtkConfDerivesModelDirFromName();
    void loadDtkConfNormalizesLegacyModelName();
    void loadDtkConfIgnoresModelDirKey();
    void loadDtkConfReadsLiveDebugAudioFlag();
    void loadDtkConfReadsStreamTranscriptionFlag();
    void loadDtkConfReadsVadSettings();
    void loadDtkConfReadsPolishSettings();
    void loadDtkConfDropsUnknownPolishModel();
    void loadDtkConfReadsLogTranscriptsFlag();
    void loadDtkConfRejectsInvalidVadSettings();
    void loadDtkConfIgnoresUnknownSections();
    void stripPunctuationTrimsWholeCodePoints();
};

void TestConfig::defaultConfigHasExpectedFields() {
    Config c = Config::defaultConfig();
    QCOMPARE(QString::fromStdString(c.modelName), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(c.pwRecord.rate, 16000);
    QCOMPARE(QString::fromStdString(c.pwRecord.format), QStringLiteral("s16"));
    QVERIFY(c.pwRecord.source.empty());
    QCOMPARE(c.fcitxCommit, true);
    QVERIFY(c.language.has_value());
    QCOMPARE(QString::fromStdString(*c.language), QStringLiteral("English"));
    QVERIFY(c.modelDir.empty());
    QVERIFY(!c.skipSilence);
    QVERIFY(c.streamTranscription);
    QVERIFY(!c.saveLiveDebugAudio);
    QCOMPARE(c.openBlasThreads, 4);
    QCOMPARE(c.crispThreads, defaultCrispThreads());
    QVERIFY(c.crispThreads >= 1 && c.crispThreads <= 8);
    QCOMPARE(c.crispMaxNewTokens, 0);
    QCOMPARE(QString::fromStdString(c.vadBackend), QStringLiteral("energy"));
    QVERIFY(c.vadModelPath.empty());
    QCOMPARE(c.vadThreshold, 0.15f);
    QCOMPARE(c.vadEndThreshold, 0.10f);
    QCOMPARE(c.vadMinSpeechMs, 100);
    QCOMPARE(c.vadMinSilenceMs, 300);
    QVERIFY(!c.polishEnabled);
    QCOMPARE(QString::fromStdString(c.polishModelName), QStringLiteral("qwen3-0.6b"));
}

void TestConfig::loadDtkConfReadsPolishSettings() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[basic.polish.enabled]\nvalue=true\n"
            "[basic.polish.model_name]\nvalue=qwen3-1.7b\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QVERIFY(c.polishEnabled);
    QCOMPARE(QString::fromStdString(c.polishModelName), QStringLiteral("qwen3-1.7b"));
    QCOMPARE(QString::fromStdString(c.polishModelPath),
             QString::fromStdString((std::filesystem::path(f.fileName().toStdString()).parent_path()
                                     / "qwen3-1.7b"
                                     / "Qwen3-1.7B-Q4_K_M.gguf").string()));
}

void TestConfig::loadDtkConfReadsLogTranscriptsFlag() {
    QVERIFY(!Config::defaultConfig().logTranscripts);
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[advanced.storage.log_transcripts]\nvalue=true\n");
    f.close();

    QVERIFY(loadDtkConf(f.fileName().toStdString()).logTranscripts);
}

void TestConfig::loadDtkConfDropsUnknownPolishModel() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[basic.polish.model_name]\nvalue=../../etc/passwd\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QVERIFY(c.polishModelPath.empty());
}

void TestConfig::crispThreadsFollowPhysicalCores() {
    QCOMPARE(crispThreadsForPhysicalCores(0), 4);
    QCOMPARE(crispThreadsForPhysicalCores(-1), 4);
    QCOMPARE(crispThreadsForPhysicalCores(1), 1);
    QCOMPARE(crispThreadsForPhysicalCores(4), 4);
    QCOMPARE(crispThreadsForPhysicalCores(8), 8);
    QCOMPARE(crispThreadsForPhysicalCores(16), 8);
}

void TestConfig::loadDtkConfReadsVadSettings() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[advanced.vad.backend]\nvalue=silero\n"
            "[advanced.vad.model]\nvalue=vad/ggml-silero-v6.2.0.bin\n"
            "[advanced.vad.threshold]\nvalue=0.25\n"
            "[advanced.vad.end_threshold]\nvalue=0.18\n"
            "[advanced.vad.min_speech_ms]\nvalue=120\n"
            "[advanced.vad.min_silence_ms]\nvalue=450\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(QString::fromStdString(c.vadBackend), QStringLiteral("silero"));
    QCOMPARE(QString::fromStdString(c.vadModelPath),
             QString::fromStdString((std::filesystem::path(f.fileName().toStdString()).parent_path()
                                     / "vad/ggml-silero-v6.2.0.bin").string()));
    QCOMPARE(c.vadThreshold, 0.25f);
    QCOMPARE(c.vadEndThreshold, 0.18f);
    QCOMPARE(c.vadMinSpeechMs, 120);
    QCOMPARE(c.vadMinSilenceMs, 450);
}

void TestConfig::loadDtkConfRejectsInvalidVadSettings() {
    const std::vector<QByteArray> invalid = {
        "[advanced.vad.backend]\nvalue=unknown\n",
        "[advanced.vad.threshold]\nvalue=0\n",
        "[advanced.vad.threshold]\nvalue=1.1\n",
        "[advanced.vad.end_threshold]\nvalue=0\n",
        "[advanced.vad.end_threshold]\nvalue=1.1\n",
        // end_threshold must not exceed the start threshold.
        "[advanced.vad.threshold]\nvalue=0.2\n"
        "[advanced.vad.end_threshold]\nvalue=0.3\n",
        "[advanced.vad.min_speech_ms]\nvalue=0\n",
        "[advanced.vad.min_silence_ms]\nvalue=-1\n",
    };
    for (const QByteArray& contents : invalid) {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write(contents);
        file.close();
        QVERIFY_EXCEPTION_THROWN(loadDtkConf(file.fileName().toStdString()),
                                 std::invalid_argument);
    }
}

void TestConfig::expandPathResolvesHome() {
    setenv("HOME", "/tmp/fakehome", 1);
    std::filesystem::path base("/home/u/.config/echoflow");
    QCOMPARE(QString::fromStdString(expandPath("$HOME/.local/share/echoflow/recordings", base)),
             QStringLiteral("/tmp/fakehome/.local/share/echoflow/recordings"));
    QCOMPARE(QString::fromStdString(expandPath("recordings", base)),
             QStringLiteral("/home/u/.config/echoflow/recordings"));
}

void TestConfig::normalizeModelNameMapsVariants() {
    QCOMPARE(QString::fromStdString(normalizeModelName("qwen-asr-0.6b")), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(normalizeModelName("0.6b")), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(normalizeModelName("0.6B")), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(normalizeModelName("1.7B")), QStringLiteral("qwen3-asr-1.7b"));
    // Unknown / empty are returned unchanged (no invented default).
    QCOMPARE(QString::fromStdString(normalizeModelName("something-else")), QStringLiteral("something-else"));
    QCOMPARE(QString::fromStdString(normalizeModelName("")), QStringLiteral(""));
}

void TestConfig::loadDtkConfDerivesModelDirFromName() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[basic.model.model_name]\nvalue=qwen3-asr-1.7b\n"
            "[basic.recognition.language]\nvalue=English\n"
            "[basic.recognition.prompt]\nvalue=Preserve spelling: CUDA\n"
            "[basic.recording.rate]\nvalue=22050\n"
            "[basic.recording.source]\nvalue=alsa_input.pci-test.Mic__source\n"
            "[basic.recording.min_record_seconds]\nvalue=0.5\n"
            "[basic.recognition.strip_trailing_punctuation]\nvalue=true\n"
            "[advanced.runtime.openblas_threads]\nvalue=2\n"
            "[advanced.crisp.max_new_tokens]\nvalue=128\n"
            "[advanced.fcitx.fcitx_commit]\nvalue=false\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(QString::fromStdString(c.modelName), QStringLiteral("qwen3-asr-1.7b"));
    QCOMPARE(QString::fromStdString(*c.language), QStringLiteral("English"));
    QCOMPARE(QString::fromStdString(c.prompt), QStringLiteral("Preserve spelling: CUDA"));
    QCOMPARE(c.pwRecord.rate, 22050);
    QCOMPARE(QString::fromStdString(c.pwRecord.source),
             QStringLiteral("alsa_input.pci-test.Mic__source"));
    QCOMPARE(c.minRecordSeconds, 0.5);
    QCOMPARE(c.stripTrailingPunctuation, true);
    QVERIFY(!c.saveLiveDebugAudio);
    QCOMPARE(c.openBlasThreads, 2);
    QCOMPARE(c.crispMaxNewTokens, 128);
    QCOMPARE(c.fcitxCommit, false);
    QCOMPARE(QString::fromStdString(c.modelDir),
             QString::fromStdString((std::filesystem::path(f.fileName().toStdString()).parent_path()
                                     / "qwen3-asr-1.7b").string()));
}

void TestConfig::loadDtkConfReadsLiveDebugAudioFlag() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[advanced.storage.save_live_debug_audio]\nvalue=true\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(c.saveLiveDebugAudio, true);
}

void TestConfig::loadDtkConfReadsStreamTranscriptionFlag() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[basic.recognition.stream_transcription]\nvalue=false\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(c.streamTranscription, false);
}

void TestConfig::loadDtkConfNormalizesLegacyModelName() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[basic.model.model_name]\nvalue=qwen-asr-0.6b\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(QString::fromStdString(c.modelDir),
             QString::fromStdString((std::filesystem::path(f.fileName().toStdString()).parent_path()
                                     / "qwen3-asr-0.6b").string()));
}

void TestConfig::loadDtkConfIgnoresModelDirKey() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[basic.model.model_name]\nvalue=qwen3-asr-0.6b\n"
            "[advanced.runtime.model_dir]\nvalue=$HOME/.local/share/echoflow/should-be-ignored\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    // modelDir is derived purely from model_name + config dir, never from the key.
    QCOMPARE(QString::fromStdString(c.modelDir),
             QString::fromStdString((std::filesystem::path(f.fileName().toStdString()).parent_path()
                                     / "qwen3-asr-0.6b").string()));
    QVERIFY(c.modelDir.find("should-be-ignored") == std::string::npos);
}

void TestConfig::loadDtkConfIgnoresUnknownSections() {
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[some.unknown.thing]\nvalue=ignored\n"
            "[basic.model.model_name]\nvalue=qwen-asr-0.6b\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(QString::fromStdString(c.modelName), QStringLiteral("qwen3-asr-0.6b"));
}

void TestConfig::stripPunctuationTrimsWholeCodePoints() {
    // Regression: 送 (e9 80 81) and 耀 (e8 80 80) share continuation bytes with
    // 、/。; the old byte-wise trim cut into them and produced invalid UTF-8
    // that crashed fcitx5 on commit. Whole code points must survive intact.
    QCOMPARE(QString::fromStdString(stripPunctuation("台式版本号。提交，然后打tag推送。")),
             QStringLiteral("台式版本号。提交，然后打tag推送"));
    QCOMPARE(QString::fromStdString(stripPunctuation("推送。")), QStringLiteral("推送"));
    QCOMPARE(QString::fromStdString(stripPunctuation("耀。")), QStringLiteral("耀"));
    QCOMPARE(QString::fromStdString(stripPunctuation("了。")), QStringLiteral("了"));
    QCOMPARE(QString::fromStdString(stripPunctuation("好。。 ?")), QStringLiteral("好"));
    QCOMPARE(QString::fromStdString(stripPunctuation("hello. ")), QStringLiteral("hello"));
    QCOMPARE(QString::fromStdString(stripPunctuation("推送")), QStringLiteral("推送"));
    QCOMPARE(QString::fromStdString(stripPunctuation("。！？ \t")), QStringLiteral(""));
    QCOMPARE(QString::fromStdString(stripPunctuation("")), QStringLiteral(""));
}

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"
