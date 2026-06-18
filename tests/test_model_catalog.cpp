// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "ModelCatalog.h"

#include <algorithm>
#include <fstream>

using namespace echoflow;

class TestModelCatalog : public QObject {
    Q_OBJECT

private slots:
    void catalogHasTwoModels();
    void findModelReturnsNullForUnknown();
    void missingFilesListsAbsent();
    void presentWhenAllFilesExist();
    void largeModelCompleteWhenAllShardsPresent();
    void largeModelMissingWhenShardRemoved();
};

void TestModelCatalog::catalogHasTwoModels() {
    auto& c = modelCatalog();
    QCOMPARE(c.size(), size_t(2));
    QCOMPARE(QString::fromStdString(c[0].id), QStringLiteral("qwen3-asr-0.6b"));
    QCOMPARE(QString::fromStdString(c[1].id), QStringLiteral("qwen3-asr-1.7b"));
}

void TestModelCatalog::findModelReturnsNullForUnknown() {
    QVERIFY(findModel("qwen3-asr-0.6b") != nullptr);
    QVERIFY(findModel("qwen3-asr-1.7b") != nullptr);
    QVERIFY(findModel("nope") == nullptr);
    QVERIFY(findModel("") == nullptr);
}

void TestModelCatalog::missingFilesListsAbsent() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    std::ofstream(modelDir / "config.json").put('x');

    const ModelEntry* e = findModel("qwen3-asr-0.6b");
    QVERIFY(e != nullptr);
    auto missing = missingModelFiles(modelDir, *e);
    QCOMPARE(missing.size(), size_t(4));
    QVERIFY(std::find(missing.begin(), missing.end(), std::string("model.safetensors")) != missing.end());
    QVERIFY(std::find(missing.begin(), missing.end(), std::string("vocab.json")) != missing.end());
}

void TestModelCatalog::presentWhenAllFilesExist() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    const ModelEntry* e = findModel("qwen3-asr-0.6b");
    for (const auto& f : e->files) {
        std::ofstream(modelDir / f).put('x');
    }
    QVERIFY(isModelPresent(modelDir, *e));
    QVERIFY(missingModelFiles(modelDir, *e).empty());
}

void TestModelCatalog::largeModelCompleteWhenAllShardsPresent() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    const ModelEntry* e = findModel("qwen3-asr-1.7b");
    for (const auto& f : e->files) {
        std::ofstream(modelDir / f).put('x');
    }
    QVERIFY(isModelPresent(modelDir, *e));
}

void TestModelCatalog::largeModelMissingWhenShardRemoved() {
    QTemporaryDir dir;
    auto modelDir = std::filesystem::path(dir.path().toStdString());
    const ModelEntry* e = findModel("qwen3-asr-1.7b");
    for (const auto& f : e->files) {
        std::ofstream(modelDir / f).put('x');
    }
    std::filesystem::remove(modelDir / "model-00002-of-00002.safetensors");
    QVERIFY(!isModelPresent(modelDir, *e));
    auto missing = missingModelFiles(modelDir, *e);
    QVERIFY(std::find(missing.begin(), missing.end(),
                      std::string("model-00002-of-00002.safetensors")) != missing.end());
}

QTEST_GUILESS_MAIN(TestModelCatalog)
#include "test_model_catalog.moc"
