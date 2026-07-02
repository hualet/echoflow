// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BenchmarkManifest.h"

#include <QTemporaryFile>
#include <QTest>

class TestBenchmarkManifest : public QObject {
    Q_OBJECT

private slots:
    void loadsValidEntry();
    void rejectsMissingId();
    void rejectsDuplicateIds();
    void rejectsInvalidSha256();
};

void TestBenchmarkManifest::loadsValidEntry()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(R"([{"id":"normal","audio":"a.wav","sha256":"0000000000000000000000000000000000000000000000000000000000000000","reference":"甲","condition":"normal","speech":[[0.1,1.2]]}])");
    file.flush();

    const auto entries = echoflow::loadBenchmarkManifest(file.fileName());
    QCOMPARE(entries.size(), size_t(1));
    QCOMPARE(entries[0].id, QStringLiteral("normal"));
    QVERIFY(entries[0].audio.is_absolute());
    QCOMPARE(entries[0].speech.size(), size_t(1));
}

void TestBenchmarkManifest::rejectsMissingId()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(R"([{"audio":"a.wav","sha256":"0000000000000000000000000000000000000000000000000000000000000000","reference":"甲"}])");
    file.flush();
    QVERIFY_EXCEPTION_THROWN(echoflow::loadBenchmarkManifest(file.fileName()), std::runtime_error);
}

void TestBenchmarkManifest::rejectsDuplicateIds()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(R"([{"id":"normal","audio":"a.wav","sha256":"0000000000000000000000000000000000000000000000000000000000000000","reference":"甲"},{"id":"normal","audio":"b.wav","sha256":"1111111111111111111111111111111111111111111111111111111111111111","reference":"乙"}])");
    file.flush();
    QVERIFY_EXCEPTION_THROWN(echoflow::loadBenchmarkManifest(file.fileName()), std::runtime_error);
}

void TestBenchmarkManifest::rejectsInvalidSha256()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(R"([{"id":"normal","audio":"a.wav","sha256":"00","reference":"甲"}])");
    file.flush();
    QVERIFY_EXCEPTION_THROWN(echoflow::loadBenchmarkManifest(file.fileName()), std::runtime_error);
}

QTEST_GUILESS_MAIN(TestBenchmarkManifest)
#include "test_benchmark_manifest.moc"
