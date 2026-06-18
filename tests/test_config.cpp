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
    void loadDtkConfParsesValues();
    void loadDtkConfMigratesLegacyDefaultModelDir();
    void loadDtkConfIgnoresUnknownSections();
};

void TestConfig::defaultConfigHasExpectedFields()
{
    Config c = Config::defaultConfig();
    QCOMPARE(QString::fromStdString(c.modelName), QStringLiteral("qwen-asr-0.6b"));
    QCOMPARE(c.pwRecord.rate, 16000);
    QCOMPARE(QString::fromStdString(c.pwRecord.format), QStringLiteral("s16"));
    QCOMPARE(c.fcitxCommit, true);
    QVERIFY(c.language.has_value());
    QCOMPARE(QString::fromStdString(*c.language), QStringLiteral("Chinese"));
}

void TestConfig::expandPathResolvesHome()
{
    setenv("HOME", "/tmp/fakehome", 1);
    std::filesystem::path base("/home/u/.config/echoflow");
    QCOMPARE(QString::fromStdString(expandPath("$HOME/AI/Model/qwen3-asr-0.6b", base)),
             QStringLiteral("/tmp/fakehome/AI/Model/qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(expandPath("recordings", base)),
             QStringLiteral("/home/u/.config/echoflow/recordings"));
}

void TestConfig::loadDtkConfParsesValues()
{
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[basic.model.model_name]\nvalue=qwen-asr-0.6b\n"
            "[basic.recognition.language]\nvalue=English\n"
            "[basic.recognition.prompt]\nvalue=Preserve spelling: CUDA\n"
            "[basic.recording.rate]\nvalue=22050\n"
            "[basic.recording.min_record_seconds]\nvalue=0.5\n"
            "[basic.recognition.strip_trailing_punctuation]\nvalue=true\n"
            "[advanced.fcitx.fcitx_commit]\nvalue=false\n"
            "[advanced.runtime.model_dir]\nvalue=$HOME/AI/Model/qwen3-asr-0.6b\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(QString::fromStdString(c.modelName), QStringLiteral("qwen-asr-0.6b"));
    QCOMPARE(QString::fromStdString(*c.language), QStringLiteral("English"));
    QCOMPARE(QString::fromStdString(c.prompt), QStringLiteral("Preserve spelling: CUDA"));
    QCOMPARE(c.pwRecord.rate, 22050);
    QCOMPARE(c.minRecordSeconds, 0.5);
    QCOMPARE(c.stripTrailingPunctuation, true);
    QCOMPARE(c.fcitxCommit, false);
    QVERIFY(c.modelDir.find("qwen3-asr-0.6b") != std::string::npos);
}

void TestConfig::loadDtkConfMigratesLegacyDefaultModelDir()
{
    setenv("HOME", "/tmp/echoflow-home", 1);

    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[advanced.runtime.model_dir]\n"
            "value=$HOME/AI/Model/Qwen3-ASR-GGUF/model-0.6B\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(QString::fromStdString(c.modelDir),
             QStringLiteral("/tmp/echoflow-home/AI/Model/qwen3-asr-0.6b"));
}

void TestConfig::loadDtkConfIgnoresUnknownSections()
{
    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("[some.unknown.thing]\nvalue=ignored\n"
            "[basic.model.model_name]\nvalue=qwen-asr-0.6b\n");
    f.close();

    Config c = loadDtkConf(f.fileName().toStdString());
    QCOMPARE(QString::fromStdString(c.modelName), QStringLiteral("qwen-asr-0.6b"));
}

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"
