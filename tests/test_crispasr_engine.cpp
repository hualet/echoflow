// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "CrispAsrEngine.h"

using namespace echoflow;

class TestCrispAsrEngine : public QObject {
    Q_OBJECT

private slots:
    void transcribeReturnsEmptyWhenBinaryMissing();
    void transcribeReturnsEmptyWhenModelMissing();
};

void TestCrispAsrEngine::transcribeReturnsEmptyWhenBinaryMissing()
{
    Config cfg = Config::defaultConfig();
    cfg.crispBinary = "/tmp/echoflow-no-such-crispasr-binary";
    cfg.crispModelPath = "/tmp/echoflow-no-such-model.gguf";
    CrispAsrEngine engine(cfg);
    try {
        QCOMPARE(QString::fromStdString(engine.transcribe("/tmp/echoflow-no-such-audio.wav")),
                 QString());
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QStringLiteral("transcribe threw: %1").arg(e.what())));
    }
}

void TestCrispAsrEngine::transcribeReturnsEmptyWhenModelMissing()
{
    Config cfg = Config::defaultConfig();
    cfg.crispBinary = "crispasr";
    cfg.crispModelPath = "/tmp/echoflow-no-such-model.gguf";
    CrispAsrEngine engine(cfg);
    QVERIFY(engine.transcribe("/tmp/echoflow-no-such-audio.wav").empty());
}

QTEST_GUILESS_MAIN(TestCrispAsrEngine)
#include "test_crispasr_engine.moc"
