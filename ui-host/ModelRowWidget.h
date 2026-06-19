// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_ROW_WIDGET_H
#define ECHOFLOW_MODEL_ROW_WIDGET_H

#include "ModelCatalog.h"
#include "ModelDownloadCoordinator.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace echoflow {

// Pure view of one model's download. Owns no downloader; it queries
// ModelDownloadCoordinator for the current snapshot and connects to its
// id-tagged signals. Constructed fresh every time the (destroy-on-close)
// settings dialog opens, so it always reflects live progress if a download is
// in flight, or fresh disk state otherwise.
class ModelRowWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModelRowWidget(const ModelEntry* entry, QWidget* parent = nullptr);

private slots:
    void onClicked();
    void onCoordinatorProgress(const QString& id, qint64 done, qint64 total, const QString& file);
    void onCoordinatorStateChanged(const QString& id, DownloadState state, const QString& error);

private:
    void refreshState();
    void renderProgress(qint64 done, qint64 total);
    QString modelId() const;

    const ModelEntry* entry_;
    QLabel* status_ = nullptr;
    QPushButton* button_ = nullptr;
};

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_ROW_WIDGET_H
