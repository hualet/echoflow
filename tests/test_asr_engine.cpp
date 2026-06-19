// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "AsrEngine.h"
#include "LiveAudioBuffer.h"

using namespace echoflow;

class TestAsrEngine : public QObject {
    Q_OBJECT

private slots:
    void preloadReturnsFalseWhenModelCannotLoad();
    void transcribeReturnsEmptyWhenModelCannotLoad();
    void transcribeLiveReturnsEmptyWhenModelCannotLoad();
};

void TestAsrEngine::preloadReturnsFalseWhenModelCannotLoad()
{
    Config cfg = Config::defaultConfig();
    cfg.modelDir = "/tmp/echoflow-model-that-does-not-exist";

    AsrEngine engine(cfg);
    QVERIFY(!engine.preload());
}

void TestAsrEngine::transcribeReturnsEmptyWhenModelCannotLoad()
{
    Config cfg = Config::defaultConfig();
    cfg.modelDir = "/tmp/echoflow-model-that-does-not-exist";

    AsrEngine engine(cfg);
    try {
        QCOMPARE(QString::fromStdString(engine.transcribe("/tmp/echoflow-audio-that-does-not-exist.wav")),
                 QString());
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QStringLiteral("transcribe threw: %1").arg(e.what())));
    }
}

void TestAsrEngine::transcribeLiveReturnsEmptyWhenModelCannotLoad()
{
    Config cfg = Config::defaultConfig();
    cfg.modelDir = "/tmp/echoflow-model-that-does-not-exist";

    AsrEngine engine(cfg);
    LiveAudioBuffer buffer;
    buffer.markEof();
    try {
        QCOMPARE(QString::fromStdString(engine.transcribeLive(buffer.get())), QString());
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QStringLiteral("transcribeLive threw: %1").arg(e.what())));
    }
}

QTEST_GUILESS_MAIN(TestAsrEngine)
#include "test_asr_engine.moc"
