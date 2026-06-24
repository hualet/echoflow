// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "CrispAsrEngine.h"

using namespace echoflow;

class TestCrispAsrEngine : public QObject {
    Q_OBJECT

private slots:
    void transcribeReturnsEmptyWhenModelMissing();
};

void TestCrispAsrEngine::transcribeReturnsEmptyWhenModelMissing()
{
    Config cfg = Config::defaultConfig();
    cfg.crispModelPath = "/tmp/echoflow-no-such-model.gguf";
    CrispAsrEngine engine(cfg);
    try {
        QCOMPARE(QString::fromStdString(engine.transcribe("/tmp/echoflow-no-such-audio.wav")),
                 QString());
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QStringLiteral("transcribe threw: %1").arg(e.what())));
    }
}

QTEST_GUILESS_MAIN(TestCrispAsrEngine)
#include "test_crispasr_engine.moc"
