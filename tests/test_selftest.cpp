// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "SelfTest.h"
#include "ModelCatalog.h"

#include <algorithm>
#include <fstream>

using namespace echoflow;

class TestSelfTest : public QObject {
    Q_OBJECT

private slots:
    void resolveModelDirIsTrivial();
    void canCreateDirectoryOnTmp();
    void runtimeChecksReportsMissingModelFiles();
    void runtimeChecksActionableWhenModelDirEmpty();
    void runtimeChecksActionableWhenModelIdUnknown();
    void runtimeChecksPassesWhenModelComplete();
};

void TestSelfTest::resolveModelDirIsTrivial() {
    Config c;
    c.modelDir = "/some/derived/path";
    QCOMPARE(resolveModelDir(c), std::filesystem::path("/some/derived/path"));
}

void TestSelfTest::canCreateDirectoryOnTmp() {
    QTemporaryDir dir;
    QVERIFY(canCreateDirectory(std::filesystem::path(dir.path().toStdString()) / "sub/deep"));
}

void TestSelfTest::runtimeChecksReportsMissingModelFiles() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    std::ofstream(modelDir / "config.json").put('x');

    Config c;
    c.modelName = "qwen3-asr-0.6b";
    c.modelDir = modelDir.string();

    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(!it->passed);
    QVERIFY(it->detail.find("model.safetensors") != std::string::npos);
    QVERIFY(it->detail.find("vocab.json") != std::string::npos);
}

void TestSelfTest::runtimeChecksActionableWhenModelDirEmpty() {
    Config c;
    c.modelName = "qwen3-asr-0.6b";
    c.modelDir.clear();

    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(!it->passed);
    QVERIFY(it->detail.find("未下载") != std::string::npos);
    QVERIFY(it->detail.find("Qwen3-ASR-0.6B") != std::string::npos);
}

void TestSelfTest::runtimeChecksActionableWhenModelIdUnknown() {
    QTemporaryDir dir;
    Config c;
    c.modelName = "unknown-model";
    c.modelDir = dir.path().toStdString();  // exists, but id is unknown
    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(!it->passed);
    QVERIFY(it->detail.find("未下载") != std::string::npos);
}

void TestSelfTest::runtimeChecksPassesWhenModelComplete() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    const ModelEntry* e = findModel("qwen3-asr-0.6b");
    for (const auto& f : e->files) {
        std::ofstream(modelDir / f).put('x');
    }
    Config c;
    c.modelName = "qwen3-asr-0.6b";
    c.modelDir = modelDir.string();
    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(it->passed);
}

QTEST_GUILESS_MAIN(TestSelfTest)
#include "test_selftest.moc"
