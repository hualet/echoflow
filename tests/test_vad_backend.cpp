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
};

void TestVadBackend::missingModelFailsExplicitly()
{
    SileroVadBackend backend("/definitely/missing/ggml-silero.bin");
    const std::vector<int16_t> silence(16000, 0);
    QVERIFY_EXCEPTION_THROWN(backend.detect(silence.data(), silence.size()), std::runtime_error);
}

void TestVadBackend::corruptModelFailsExplicitly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::filesystem::path path =
        std::filesystem::path(dir.path().toStdString()) / "ggml-silero.bin";
    std::ofstream(path) << "not a model";
    SileroVadBackend backend(path);
    const std::vector<int16_t> silence(16000, 0);
    QVERIFY_EXCEPTION_THROWN(backend.detect(silence.data(), silence.size()), std::runtime_error);
}

QTEST_GUILESS_MAIN(TestVadBackend)
#include "test_vad_backend.moc"
