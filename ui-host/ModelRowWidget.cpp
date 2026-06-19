// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModelRowWidget.h"

#include "EchoFlowSettings.h"
#include "ModelDownloadCoordinator.h"

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

// Read the mirror setting live (so changing the combobox takes effect on the
// NEXT start; an in-flight download keeps the mirror it was started with).
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

    auto* c = ModelDownloadCoordinator::instance();
    // Both rows share the coordinator's signals; each slot filters by id.
    // Qt auto-disconnects when `this` is destroyed, so reopening the dialog
    // (which deletes the old widget) needs no manual cleanup.
    connect(c, &ModelDownloadCoordinator::progress, this,
            &ModelRowWidget::onCoordinatorProgress);
    connect(c, &ModelDownloadCoordinator::stateChanged, this,
            &ModelRowWidget::onCoordinatorStateChanged);

    // Paint immediately from the snapshot — the widget missed any progress
    // signals emitted before it existed.
    const DownloadSnapshot snap = c->snapshot(modelId());
    if (snap.state == DownloadState::Downloading) {
        renderProgress(snap.done, snap.total);
        button_->setText(QStringLiteral("取消"));
        button_->setEnabled(true);
    } else {
        refreshState();
    }
}

QString ModelRowWidget::modelId() const {
    return entry_ ? QString::fromStdString(entry_->id) : QString();
}

void ModelRowWidget::renderProgress(qint64 done, qint64 total) {
    if (total > 0) {
        const int pct = static_cast<int>((done * 100) / total);
        status_->setText(QString::number(qBound(0, pct, 100)) + QStringLiteral("%"));
    } else {
        // Indeterminate (no Content-Length): show downloaded megabytes.
        const double mb = done / (1024.0 * 1024.0);
        status_->setText(QString("%1 MB").arg(mb, 0, 'f', 1));
    }
}

void ModelRowWidget::refreshState() {
    if (!entry_) {
        status_->setText(QString());
        button_->setText(QStringLiteral("未知"));
        button_->setEnabled(false);
        return;
    }
    // A download in flight is driven by the coordinator signals, not disk.
    if (ModelDownloadCoordinator::instance()->snapshot(modelId()).state
        == DownloadState::Downloading) {
        return;
    }
    const QString dir = configDir() + QStringLiteral("/") + modelId();
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
    auto* c = ModelDownloadCoordinator::instance();
    if (c->snapshot(modelId()).state == DownloadState::Downloading) {
        // Download in flight: the button is the cancel affordance.
        c->cancel(modelId());  // coordinator emits stateChanged(Failed, "已取消")
        return;
    }
    const QString dir = configDir() + QStringLiteral("/") + modelId();
    c->start(*entry_, dir, baseUrlFromMirror());
    button_->setText(QStringLiteral("取消"));
    button_->setEnabled(true);
    status_->setText(QString());
}

void ModelRowWidget::onCoordinatorProgress(const QString& id, qint64 done, qint64 total,
                                           const QString& /*file*/) {
    if (id != modelId()) {
        return;
    }
    // Throughout a download the button is the cancel affordance and stays
    // enabled — including indeterminate mode and indeterminate->determinate
    // transitions (matches the pre-refactor behavior).
    button_->setText(QStringLiteral("取消"));
    button_->setEnabled(true);
    renderProgress(done, total);
}

void ModelRowWidget::onCoordinatorStateChanged(const QString& id, DownloadState state,
                                               const QString& error) {
    if (id != modelId()) {
        return;
    }
    if (state == DownloadState::Downloading) {
        button_->setText(QStringLiteral("取消"));
        button_->setEnabled(true);
        return;
    }
    if (state == DownloadState::Failed) {
        // Covers user cancel too: the downloader emits finished(false,"已取消"),
        // so the status reads 已取消 and the button becomes 重试.
        status_->setText(error);
        button_->setText(QStringLiteral("重试"));
        button_->setEnabled(true);
        return;
    }
    // Succeeded (or Idle): re-evaluate against disk; a model was (possibly)
    // added, so rebuild the model_name combobox so it becomes selectable.
    refreshState();
    EchoFlowSettings::instance()->refreshModelNameItems();
}

}  // namespace echoflow
