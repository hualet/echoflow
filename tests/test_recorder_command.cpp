// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "Recorder.h"

using namespace echoflow;

class TestRecorderCommand : public QObject {
    Q_OBJECT

private slots:
    void fileRecorderUsesDefaultSourceWhenUnset();
    void fileRecorderAddsTargetForConfiguredSource();
    void liveRecorderAddsTargetForConfiguredSource();
};

void TestRecorderCommand::fileRecorderUsesDefaultSourceWhenUnset() {
    PipeWireRecordConfig cfg;

    const std::vector<std::string> args = buildPipeWireRecordArgs(cfg, "/tmp/voice.wav");

    QCOMPARE(args, std::vector<std::string>({"pw-record",
                                             "--rate", "16000",
                                             "--channels", "1",
                                             "--format", "s16",
                                             "/tmp/voice.wav"}));
}

void TestRecorderCommand::fileRecorderAddsTargetForConfiguredSource() {
    PipeWireRecordConfig cfg;
    cfg.source = "alsa_input.pci-test.Mic__source";

    const std::vector<std::string> args = buildPipeWireRecordArgs(cfg, "/tmp/voice.wav");

    QCOMPARE(args, std::vector<std::string>({"pw-record",
                                             "--rate", "16000",
                                             "--channels", "1",
                                             "--format", "s16",
                                             "--target", "alsa_input.pci-test.Mic__source",
                                             "/tmp/voice.wav"}));
}

void TestRecorderCommand::liveRecorderAddsTargetForConfiguredSource() {
    Config cfg = Config::defaultConfig();
    cfg.pwRecord.source = "alsa_input.pci-test.Mic__source";

    const std::vector<std::string> args = buildPipeWireLiveRecordArgs(cfg);

    QCOMPARE(args, std::vector<std::string>({"pw-record",
                                             "--rate", "16000",
                                             "--channels", "1",
                                             "--format", "s16",
                                             "--target", "alsa_input.pci-test.Mic__source",
                                             "--raw",
                                             "-"}));
}

QTEST_GUILESS_MAIN(TestRecorderCommand)
#include "test_recorder_command.moc"
