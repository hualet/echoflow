// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "SelfTest.h"

#include <algorithm>
#include <fstream>

using namespace echoflow;

class TestSelfTest : public QObject {
    Q_OBJECT

private slots:
    void modelDirCandidatesHasFallback();
    void canCreateDirectoryOnTmp();
    void missingModelFilesListsAbsent();
    void missingModelFilesAcceptsLargeModelShards();
};

void TestSelfTest::modelDirCandidatesHasFallback()
{
    auto candidates = modelDirCandidates("/x/model-0.6B");
    QVERIFY(std::find(candidates.begin(), candidates.end(), std::filesystem::path("/x/model")) != candidates.end());

    auto reverse = modelDirCandidates("/x/model");
    QVERIFY(std::find(reverse.begin(), reverse.end(), std::filesystem::path("/x/model-0.6B")) != reverse.end());
}

void TestSelfTest::canCreateDirectoryOnTmp()
{
    QTemporaryDir dir;
    QVERIFY(canCreateDirectory(std::filesystem::path(dir.path().toStdString()) / "sub/deep"));
}

void TestSelfTest::missingModelFilesListsAbsent()
{
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    std::ofstream(modelDir / "config.json").put('x');

    auto missing = missingModelFiles(modelDir);
    QCOMPARE(missing.size(), size_t(4));
    QVERIFY(std::find(missing.begin(), missing.end(), std::string("model.safetensors")) != missing.end());
}

void TestSelfTest::missingModelFilesAcceptsLargeModelShards()
{
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    for (const char* file : {"config.json", "generation_config.json", "vocab.json", "merges.txt",
                            "model.safetensors.index.json", "model-00001-of-00002.safetensors",
                            "model-00002-of-00002.safetensors"}) {
        std::ofstream(modelDir / file).put('x');
    }

    auto missing = missingModelFiles(modelDir);
    QVERIFY(missing.empty());
}

QTEST_GUILESS_MAIN(TestSelfTest)
#include "test_selftest.moc"
