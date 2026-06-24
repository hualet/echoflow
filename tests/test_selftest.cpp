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
    void resolveModelDirIsTrivial();
    void canCreateDirectoryOnTmp();
    void runtimeChecksReportsMissingCrispModel();
    void runtimeChecksActionableWhenCrispModelPathEmpty();
    void runtimeChecksPassesWhenCrispModelExists();
    void runtimeChecksReportsCrispBinary();
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

void TestSelfTest::runtimeChecksReportsMissingCrispModel() {
    Config c;
    c.crispModelPath = "/tmp/echoflow-no-such-crisp-model.gguf";
    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "crisp model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(!it->passed);
    QVERIFY(it->detail.find("missing:") != std::string::npos);
}

void TestSelfTest::runtimeChecksActionableWhenCrispModelPathEmpty() {
    Config c;
    c.crispModelPath.clear();
    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "crisp model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(!it->passed);
    QVERIFY(it->detail.find("not set") != std::string::npos);
}

void TestSelfTest::runtimeChecksPassesWhenCrispModelExists() {
    QTemporaryDir dir;
    auto modelPath = std::filesystem::path(dir.path().toStdString()) / "model.gguf";
    std::ofstream(modelPath).put('x');
    Config c;
    c.crispModelPath = modelPath.string();
    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "crisp model available"; });
    QVERIFY(it != checks.end());
    QVERIFY(it->passed);
}

void TestSelfTest::runtimeChecksReportsCrispBinary() {
    Config c;
    c.crispBinary = "crispasr";
    auto checks = runtimeChecks(c);
    auto it = std::find_if(checks.begin(), checks.end(),
                           [](const RuntimeCheck& r) { return r.name == "crispasr available"; });
    QVERIFY(it != checks.end());
}

QTEST_GUILESS_MAIN(TestSelfTest)
#include "test_selftest.moc"
