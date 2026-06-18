// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

namespace Dtk {
namespace Core {
class DSettings;
class QSettingBackend;
}
} // namespace Dtk

namespace echoflow {

class EchoFlowSettings : public QObject {
    Q_OBJECT
public:
    static EchoFlowSettings *instance();

    bool init(const QString &configPath);
    QString configPath() const;
    Dtk::Core::DSettings *dsettings() const;
    void sync();

private:
    explicit EchoFlowSettings(QObject *parent = nullptr);
    ~EchoFlowSettings() override;

    void populateComboBoxes();
    static void setComboBoxItems(Dtk::Core::DSettings *settings,
                                 const QString &path,
                                 const QStringList &items);

    Dtk::Core::DSettings *dsettings_ = nullptr;
    Dtk::Core::QSettingBackend *backend_ = nullptr;
    QString configPath_;
};

} // namespace echoflow
