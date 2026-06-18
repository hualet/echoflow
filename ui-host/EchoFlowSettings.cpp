// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EchoFlowSettings.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

#include <DSettings>
#include <DSettingsOption>
#include <qsettingbackend.h>

namespace echoflow {

EchoFlowSettings *EchoFlowSettings::instance() {
    static EchoFlowSettings instance;
    return &instance;
}

EchoFlowSettings::EchoFlowSettings(QObject *parent) : QObject(parent) {}

EchoFlowSettings::~EchoFlowSettings() = default;

bool EchoFlowSettings::init(const QString &configPath) {
    QFile schemaFile(QStringLiteral(":/settings/settings-schema.json"));
    if (!schemaFile.open(QIODevice::ReadOnly)) {
        return false;
    }

    dsettings_ = Dtk::Core::DSettings::fromJson(schemaFile.readAll());
    if (!dsettings_) {
        return false;
    }

    populateComboBoxes();

    configPath_ = configPath;
    QDir().mkpath(QFileInfo(configPath_).path());

    if (!QFile::exists(configPath_)) {
        const QStringList paths = {
            QStringLiteral("basic.model.model_name"),
            QStringLiteral("basic.recognition.language"),
            QStringLiteral("basic.recognition.strip_trailing_punctuation"),
            QStringLiteral("basic.recording.min_record_seconds"),
            QStringLiteral("basic.recording.rate"),
            QStringLiteral("basic.recording.channels"),
            QStringLiteral("basic.recording.format"),
            QStringLiteral("advanced.runtime.asr_project_dir"),
            QStringLiteral("advanced.runtime.model_dir"),
            QStringLiteral("advanced.runtime.asr_runner"),
            QStringLiteral("advanced.runtime.asr_timeout_seconds"),
            QStringLiteral("advanced.fcitx.fcitx_commit"),
            QStringLiteral("advanced.storage.recordings_dir"),
        };
        QSettings defaultSettings(configPath_, QSettings::IniFormat);
        for (const QString &path : paths) {
            Dtk::Core::DSettingsOption *option = dsettings_->option(path);
            if (option) {
                defaultSettings.setValue(path + QStringLiteral("/value"), option->defaultValue());
            }
        }
        defaultSettings.sync();
    }

    backend_ = new Dtk::Core::QSettingBackend(configPath_);
    dsettings_->setBackend(backend_);

    return true;
}

QString EchoFlowSettings::configPath() const {
    return configPath_;
}

Dtk::Core::DSettings *EchoFlowSettings::dsettings() const {
    return dsettings_;
}

void EchoFlowSettings::sync() {
    if (dsettings_) {
        dsettings_->sync();
    }
}

void EchoFlowSettings::populateComboBoxes() {
    setComboBoxItems(dsettings_, QStringLiteral("basic.model.model_name"),
                     QStringList{QStringLiteral("qwen-asr-0.6b")});
    setComboBoxItems(dsettings_, QStringLiteral("basic.recognition.language"),
                     QStringList{QStringLiteral("Chinese"), QStringLiteral("English"),
                                  QStringLiteral("Japanese"), QStringLiteral("auto")});
    setComboBoxItems(dsettings_, QStringLiteral("basic.recording.rate"),
                     QStringList{QStringLiteral("8000"), QStringLiteral("16000"),
                                  QStringLiteral("22050"), QStringLiteral("44100"),
                                  QStringLiteral("48000")});
    setComboBoxItems(dsettings_, QStringLiteral("basic.recording.channels"),
                     QStringList{QStringLiteral("1"), QStringLiteral("2")});
    setComboBoxItems(dsettings_, QStringLiteral("basic.recording.format"),
                     QStringList{QStringLiteral("s16"), QStringLiteral("s32"),
                                  QStringLiteral("f32"), QStringLiteral("u8")});
}

void EchoFlowSettings::setComboBoxItems(Dtk::Core::DSettings *settings,
                                        const QString &path,
                                        const QStringList &items) {
    if (!settings) {
        return;
    }
    Dtk::Core::DSettingsOption *option = settings->option(path);
    if (!option) {
        return;
    }
    option->setData(QStringLiteral("items"),
                    QMap<QString, QVariant>{{QStringLiteral("keys"), items},
                                            {QStringLiteral("values"), items}});
}

} // namespace echoflow
