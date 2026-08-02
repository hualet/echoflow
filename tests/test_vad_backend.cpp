// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SileroVadBackend.h"

#include <QTemporaryDir>
#include <QTest>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace echoflow;

class TestVadBackend : public QObject {
    Q_OBJECT

private slots:
    void missingModelFailsExplicitly();
    void corruptModelFailsExplicitly();
    void rejectsInvalidParameters();
};

void TestVadBackend::missingModelFailsExplicitly()
{
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend("/definitely/missing/ggml-silero.bin", 0.1f, 100, 300),
        std::runtime_error);
}

void TestVadBackend::corruptModelFailsExplicitly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::filesystem::path path =
        std::filesystem::path(dir.path().toStdString()) / "ggml-silero.bin";
    std::ofstream(path) << "not a model";
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend(path, 0.1f, 100, 300),
        std::runtime_error);
}

void TestVadBackend::rejectsInvalidParameters()
{
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend("/unused", 0.0f, 100, 300), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend("/unused", 0.1f, 0, 300), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(
        SileroVadBackend("/unused", 0.1f, 100, 0), std::invalid_argument);
}

QTEST_GUILESS_MAIN(TestVadBackend)
#include "test_vad_backend.moc"
