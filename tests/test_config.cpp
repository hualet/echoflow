// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "Config.h"

using namespace echoflow;

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    void defaultConfigHasExpectedFields();
    void expandPathResolvesHome();
    void normalizeModelNameMapsVariants();
    void loadDtkConfDerivesModelDirFromName();
    void loadDtkConfNormalizesLegacyModelName();
    void loadDtkConfIgnoresModelDirKey();
    void loadDtkConfReadsLiveDebugAudioFlag();
    void loadDtkConfReadsStreamTranscriptionFlag();
    void loadDtkConfIgnoresUnknownSections();
};

void TestConfig::defaultConfigHasExpectedFields() {
    Config c = Config::defaultConfig();
    QCOMPARE(QString::fromStdString(c.modelName), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(c.pwRecord.rate, 16000);
    QCOMPARE(QString::fromStdString(c.pwRecord.format), QStringLiteral("s16"));
    QVERIFY(c.pwRecord.source.empty());
    QCOMPARE(c.fcitxCommit, true);
    QVERIFY(c.language.has_value());
    QCOMPARE(QString::fromStdString(*c.language), QStringLiteral("Chinese"));
    QVERIFY(c.modelDir.empty());
    QVERIFY(!c.skipSilence);
    QVERIFY(c.streamTranscription);
    QVERIFY(!c.saveLiveDebugAudio);
    QCOMPARE(c.openBlasThreads, 4);
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

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"
