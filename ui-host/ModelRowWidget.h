// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ECHOFLOW_MODEL_ROW_WIDGET_H
#define ECHOFLOW_MODEL_ROW_WIDGET_H

#include "ModelCatalog.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace echoflow {

class ModelDownloader;

class ModelRowWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModelRowWidget(const ModelEntry* entry, QWidget* parent = nullptr);

private slots:
    void onClicked();
    void onProgress(qint64 done, qint64 total, const QString& currentFile);
    void onFinished(bool ok, const QString& error);

private:
    void refreshState();

    const ModelEntry* entry_;
    QLabel* status_ = nullptr;
    QPushButton* button_ = nullptr;
    ModelDownloader* downloader_ = nullptr;
};

}  // namespace echoflow

#endif  // ECHOFLOW_MODEL_ROW_WIDGET_H
