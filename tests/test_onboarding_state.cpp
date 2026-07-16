// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest/QtTest>

#include "OnboardingState.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

class TestOnboardingState : public QObject {
    Q_OBJECT

private slots:
    void absentFileIsIncomplete();
    void lowerVersionIsIncomplete();
    void currentVersionIsComplete();
    void recordsCurrentVersion();
    void reportsWriteFailure();
    void defaultPathUsesConfigLocation();
};

void TestOnboardingState::absentFileIsIncomplete()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    QVERIFY(!state.isComplete());
}

void TestOnboardingState::lowerVersionIsIncomplete()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui-state.ini"));
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("onboarding/version"),
                      OnboardingState::kCurrentVersion - 1);
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);

    QVERIFY(!OnboardingState(path).isComplete());
}

void TestOnboardingState::currentVersionIsComplete()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui-state.ini"));
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("onboarding/version"),
                      OnboardingState::kCurrentVersion);
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);

    QVERIFY(OnboardingState(path).isComplete());
}

void TestOnboardingState::recordsCurrentVersion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("nested/ui-state.ini"));
    OnboardingState state(path);
    QCOMPARE(state.path(), path);
    QVERIFY(!state.isComplete());

    QString error;
    QVERIFY2(state.markComplete(&error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(OnboardingState(path).isComplete());

    QSettings settings(path, QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("onboarding/version")).toInt(),
             OnboardingState::kCurrentVersion);
}

void TestOnboardingState::reportsWriteFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString statePath = dir.filePath(QStringLiteral("state-as-directory"));
    QVERIFY(QDir().mkpath(statePath));

    OnboardingState state(statePath);
    QString error;
    QVERIFY(!state.markComplete(&error));
    QVERIFY2(error.contains(statePath), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("AccessError")), qPrintable(error));
    QVERIFY(!state.isComplete());
}

void TestOnboardingState::defaultPathUsesConfigLocation()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString configLocation =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

    QCOMPARE(OnboardingState::defaultPath(),
             configLocation + QStringLiteral("/echoflow/ui-state.ini"));
    QVERIFY(OnboardingState::defaultPath().endsWith(
        QStringLiteral("/echoflow/ui-state.ini")));
    QStandardPaths::setTestModeEnabled(false);
}

QTEST_GUILESS_MAIN(TestOnboardingState)
#include "test_onboarding_state.moc"
