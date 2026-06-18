// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModelRowWidget.h"

#include "EchoFlowSettings.h"
#include "ModelDownloader.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <DSettings>
#include <DSettingsOption>

namespace echoflow {

namespace {

// Config dir = parent of echoflow.conf.
QString configDir() {
    const QString conf = EchoFlowSettings::instance()->configPath();
    const int slash = conf.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? conf.left(slash) : QStringLiteral(".");
}

// Read the mirror setting live (so changing the combobox takes effect).
QString baseUrlFromMirror() {
    auto* ds = EchoFlowSettings::instance()->dsettings();
    QString mirror = QStringLiteral("hf-mirror");
    if (ds) {
        auto opt = ds->option(QStringLiteral("basic.model.mirror"));
        if (opt) {
            const QVariant v = opt->value();
            if (v.isValid() && !v.toString().isEmpty()) {
                mirror = v.toString();
            }
        }
    }
    return mirror == QLatin1String("official")
               ? QStringLiteral("https://huggingface.co")
               : QStringLiteral("https://hf-mirror.com");
}

}  // namespace

ModelRowWidget::ModelRowWidget(const ModelEntry* entry, QWidget* parent)
    : QWidget(parent), entry_(entry)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    status_ = new QLabel(this);
    button_ = new QPushButton(this);
    lay->addStretch(1);
    lay->addWidget(status_);
    lay->addWidget(button_);

    connect(button_, &QPushButton::clicked, this, &ModelRowWidget::onClicked);
    refreshState();
}

void ModelRowWidget::refreshState() {
    if (!entry_) {
        status_->setText(QString());
        button_->setText(QStringLiteral("未知"));
        button_->setEnabled(false);
        return;
    }
    if (downloader_) {
        return;  // downloading; state driven by onProgress/onFinished
    }
    const QString dir = configDir() + QStringLiteral("/") +
                        QString::fromStdString(entry_->id);
    const bool present = isModelPresent(std::filesystem::path(dir.toStdString()), *entry_);
    if (present) {
        status_->setText(QString());
        button_->setText(QStringLiteral("已下载"));
        button_->setEnabled(false);
    } else {
        status_->setText(QString());
        button_->setText(QStringLiteral("下载"));
        button_->setEnabled(true);
    }
}

void ModelRowWidget::onClicked() {
    if (!entry_) {
        return;
    }
    if (downloader_) {
        // A download is in flight: the button is the cancel affordance.
        downloader_->cancel();  // emits finished(false, "已取消"); onFinished resets state
        return;
    }
    const QString dir = configDir() + QStringLiteral("/") +
                        QString::fromStdString(entry_->id);
    downloader_ = new ModelDownloader(*entry_, dir, baseUrlFromMirror(), this);
    connect(downloader_, &ModelDownloader::progress, this, &ModelRowWidget::onProgress);
    connect(downloader_, &ModelDownloader::finished, this, &ModelRowWidget::onFinished);
    button_->setText(QStringLiteral("取消"));
    button_->setEnabled(true);
    status_->setText(QString());
    downloader_->start();
}

void ModelRowWidget::onProgress(qint64 done, qint64 total, const QString& /*currentFile*/) {
    // Throughout a download the button is the cancel affordance and stays enabled
    // — including indeterminate mode and indeterminate->determinate transitions.
    button_->setText(QStringLiteral("取消"));
    button_->setEnabled(true);
    if (total > 0) {
        const int pct = static_cast<int>((done * 100) / total);
        status_->setText(QString::number(qBound(0, pct, 100)) + QStringLiteral("%"));
    } else {
        // Indeterminate (no Content-Length yet): show downloaded megabytes.
        const double mb = done / (1024.0 * 1024.0);
        status_->setText(QString("%1 MB").arg(mb, 0, 'f', 1));
    }
}

void ModelRowWidget::onFinished(bool ok, const QString& error) {
    downloader_->deleteLater();
    downloader_ = nullptr;
    if (!ok && !error.isEmpty()) {
        status_->setText(error);
        button_->setText(QStringLiteral("重试"));
        button_->setEnabled(true);
        return;
    }
    refreshState();
    // A model was (possibly) added to disk; rebuild the model_name combobox
    // so the just-downloaded model becomes selectable.
    EchoFlowSettings::instance()->refreshModelNameItems();
}

}  // namespace echoflow
