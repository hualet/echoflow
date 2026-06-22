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
    void runtimeChecksReportsMissingSenseVoiceVad();
    void runtimeChecksActionableWhenModelDirEmpty();
    void runtimeChecksActionableWhenModelIdUnknown();
    void runtimeChecksPassesWhenModelComplete();
    void runtimeChecksPassesWhenSenseVoiceComplete();
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

void TestSelfTest::runtimeChecksReportsMissingSenseVoiceVad() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    std::ofstream(modelDir / "sensevoice-small-q8.gguf").put('x');

    Config c;
    c.modelName = "sensevoice-small-q8";
    c.modelDir = modelDir.string();

    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(!it->passed);
    QVERIFY(it->detail.find("fsmn-vad.gguf") != std::string::npos);
}

void TestSelfTest::runtimeChecksActionableWhenModelDirEmpty() {
    Config c;
    c.modelName = "sensevoice-small-q8";
    c.modelDir.clear();

    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(!it->passed);
    QVERIFY(it->detail.find("未下载") != std::string::npos);
    QVERIFY(it->detail.find("SenseVoiceSmall Q8") != std::string::npos);
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
        std::ofstream(modelDir / f.path).put('x');
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

void TestSelfTest::runtimeChecksPassesWhenSenseVoiceComplete() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    const ModelEntry* e = findModel("sensevoice-small-q8");
    for (const auto& f : e->files) {
        std::ofstream(modelDir / f.path).put('x');
    }
    Config c;
    c.modelName = "sensevoice-small-q8";
    c.modelDir = modelDir.string();
    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(it->passed);
}

QTEST_GUILESS_MAIN(TestSelfTest)
#include "test_selftest.moc"
