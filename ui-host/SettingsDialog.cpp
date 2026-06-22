// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsDialog.h"

#include "ModelCatalog.h"
#include "ModelRowWidget.h"

#include <QLabel>

#include <DSettings>
#include <DSettingsOption>
#include <DSettingsWidgetFactory>

namespace echoflow {

namespace {

const ModelEntry* entryForOption(Dtk::Core::DSettingsOption* opt) {
    if (!opt) {
        return nullptr;
    }
    // DTK6 hands the factory a QObject*; map by option key (reliable across
    // DTK versions — schema JSON -> option data wiring is not guaranteed).
    const QString key = opt->key();
    if (key.endsWith(QStringLiteral("download_sensevoice"))) {
        return findModel("sensevoice-small-q8");
    }
    if (key.endsWith(QStringLiteral("download_0.6b"))) {
        return findModel("qwen3-asr-0.6b");
    }
    if (key.endsWith(QStringLiteral("download_1.7b"))) {
        return findModel("qwen3-asr-1.7b");
    }
    return nullptr;
}

}  // namespace

SettingsDialog::SettingsDialog(Dtk::Core::DSettings *settings, QWidget *parent)
    : Dtk::Widget::DSettingsDialog(parent) {
    setWindowTitle(tr("EchoFlow 设置"));

    // DSettingsDialog owns its factory (no global singleton). Register the
    // custom type on it BEFORE updateSettings builds the rows. Use the
    // ItemCreateHandler (pair) form so we supply BOTH the left-hand name label
    // and the right-hand download widget — DTK does not render the option
    // `name` as a label for custom-registered widget types.
    auto* factory = widgetFactory();
    factory->registerWidget(QStringLiteral("modeldownload"),
        [](QObject* obj) -> QPair<QWidget*, QWidget*> {
            auto* opt = qobject_cast<Dtk::Core::DSettingsOption*>(obj);
            const ModelEntry* e = entryForOption(opt);
            auto* name = new QLabel(e ? QString::fromStdString(e->displayName)
                                      : QStringLiteral("未知模型"));
            name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            return {name, new ModelRowWidget(e)};
        });

    updateSettings(settings);
}

}  // namespace echoflow
