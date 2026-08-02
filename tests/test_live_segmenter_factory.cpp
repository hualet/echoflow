// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LiveSegmenterFactory.h"

#include <QTest>

#include <stdexcept>

using namespace echoflow;

class TestLiveSegmenterFactory : public QObject {
    Q_OBJECT

private slots:
    void energyIsTheDefault();
    void unknownBackendFailsClosed();
    void missingSileroModelFailsClosed();
};

void TestLiveSegmenterFactory::energyIsTheDefault()
{
    Config config;
    auto segmenter = makeLiveSegmenter(config);
    QCOMPARE(QString::fromStdString(segmenter->name()), QStringLiteral("energy"));
}

void TestLiveSegmenterFactory::unknownBackendFailsClosed()
{
    Config config;
    config.vadBackend = "unknown";
    QVERIFY_EXCEPTION_THROWN(makeLiveSegmenter(config), std::invalid_argument);
}

void TestLiveSegmenterFactory::missingSileroModelFailsClosed()
{
    Config config;
    config.vadBackend = "silero";
    config.vadModelPath = "/definitely/missing/silero.bin";
    QVERIFY_EXCEPTION_THROWN(makeLiveSegmenter(config), std::runtime_error);
}

QTEST_GUILESS_MAIN(TestLiveSegmenterFactory)
#include "test_live_segmenter_factory.moc"
