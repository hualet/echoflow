// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcess>
#include <QScreen>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QString>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

#include <DSettings>
#include <DSettingsOption>
#include <DGuiApplicationHelper>
#include <DPalette>

#include "EchoFlowSettings.h"
#include "ModelDownloadCoordinator.h"
#include "ModelSetupAdapter.h"
#include "OnboardingDialog.h"
#include "OnboardingSetupController.h"
#include "OnboardingState.h"
#include "SettingsDialog.h"
#include "SetupCommandRunner.h"
#include "UiActivationServer.h"

namespace {

QtMessageHandler previousMessageHandler = nullptr;

void echoflowMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    if (message == QStringLiteral(
                       "setHighDpiScaleFactorRoundingPolicy must be called before creating the QGuiApplication instance")) {
        return;
    }

    if (previousMessageHandler) {
        previousMessageHandler(type, context, message);
        return;
    }
    qt_message_output(type, context, message);
}

bool setDtkSingleInstance(const QString &key) {
    return Dtk::Gui::DGuiApplicationHelper::setSingleInstance(key);
}

void logDuplicateInstanceExit() {
    qInfo("echoflow-ui is already running; exiting duplicate instance");
    std::fprintf(stderr, "echoflow-ui is already running; exiting duplicate instance\n");
}

bool isUsableRuntimeDirectory(const std::string &path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
           access(path.c_str(), W_OK | X_OK) == 0;
}

std::string runtimeDir() {
    if (const char *runtime = std::getenv("XDG_RUNTIME_DIR")) {
        if (runtime[0] != '\0' && isUsableRuntimeDirectory(runtime)) {
            return runtime;
        }
    }
    std::string runUserDir = "/run/user/" + std::to_string(getuid());
    if (isUsableRuntimeDirectory(runUserDir)) {
        return runUserDir;
    }
    return "/tmp";
}

std::string defaultUiSocketPath() {
    return runtimeDir() + "/echoflow-ui.sock";
}

QString defaultUiLockPath() {
    return QString::fromStdString(runtimeDir() + "/echoflow-ui.instance");
}

QString defaultQmlPath() {
    return QStringLiteral("qrc:/qml/EchoFlowTooltip.qml");
}

std::string defaultControlSocketPath() {
    return runtimeDir() + "/echoflow-control.sock";
}

QString defaultConfigPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
           QStringLiteral("/echoflow/echoflow.conf");
}

// Fire-and-forget datagram to the echoflow-service control socket, mirroring the
// fcitx addon's sendControlCommand. A throwaway bound client path is used so the
// service's reply lands on a path we unlink (-> FileNotFoundError, caught) rather
// than an abstract autobind address (-> ECONNREFUSED, which would crash the service).
bool sendControlCommand(const QString &controlPath, std::string_view command) {
    const QByteArray serverPath = controlPath.toLocal8Bit();
    if (serverPath.size() >= static_cast<int>(sizeof(sockaddr_un::sun_path))) {
        return false;
    }

    static std::atomic<long long> seq{0};
    const std::string clientPath = runtimeDir() + "/echoflow-ui-ctrl-" +
                                   std::to_string(getpid()) + "-" +
                                   std::to_string(seq.fetch_add(1)) + ".sock";
    if (clientPath.size() >= sizeof(sockaddr_un::sun_path)) {
        return false;
    }

    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_un server{};
    sockaddr_un client{};
    server.sun_family = AF_UNIX;
    std::strncpy(server.sun_path, serverPath.constData(), sizeof(server.sun_path) - 1);
    client.sun_family = AF_UNIX;
    std::strncpy(client.sun_path, clientPath.c_str(), sizeof(client.sun_path) - 1);

    unlink(clientPath.c_str());
    bool ok = bind(fd, reinterpret_cast<sockaddr *>(&client), sizeof(client)) == 0;
    if (ok) {
        ok = sendto(fd, command.data(), command.size(), 0,
                    reinterpret_cast<sockaddr *>(&server), sizeof(server)) >= 0;
    }
    close(fd);
    unlink(clientPath.c_str());
    return ok;
}

void restartServiceAfterSettingsChange() {
    const bool started = QProcess::startDetached(
        QStringLiteral("systemctl"),
        QStringList{QStringLiteral("--user"), QStringLiteral("restart"), QStringLiteral("echoflow.service")});
    if (!started) {
        qWarning("failed to restart echoflow.service after settings change");
    }
}

struct TooltipPos {
    bool hasPosition = false;
    int x = 0;
    int y = 0;
};

// The voice capsule always sits at the primary screen's bottom-center, ~8px
// above the panel (availableGeometry already excludes panels).
TooltipPos fixedCapsulePosition() {
    if (auto *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        return {true, avail.left() + avail.width() / 2, avail.bottom() - 8};
    }
    return {};
}

class TooltipController final : public QObject {
    Q_OBJECT
public:
    explicit TooltipController(QString controlPath, QObject *parent = nullptr)
        : QObject(parent), controlPath_(std::move(controlPath)) {}

public slots:
    void setTooltip(bool visible, const QString &message, bool busy = false,
                    bool hasPosition = false, int moveX = 0, int moveY = 0) {
        emit tooltipChanged(visible, message, busy, hasPosition, moveX, moveY);
    }

    // Equivalent to pressing the right Ctrl key: toggles recording on/off.
    void requestToggle() {
        sendControlCommand(controlPath_, "CTRL_DOWN");
    }

    // The capsule's X button: discard the recording and hide the capsule.
    void requestCancel() {
        sendControlCommand(controlPath_, "CANCEL");
    }

signals:
    void tooltipChanged(bool visible, const QString &message, bool busy,
                        bool hasPosition, int moveX, int moveY);

private:
    QString controlPath_;
};

class ThemeBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor capsuleBackground READ capsuleBackground NOTIFY paletteChanged)
    Q_PROPERTY(QColor capsuleBorder READ capsuleBorder NOTIFY paletteChanged)
    Q_PROPERTY(QColor capsuleText READ capsuleText NOTIFY paletteChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY paletteChanged)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY paletteChanged)
public:
    explicit ThemeBridge(QObject *parent = nullptr)
        : QObject(parent)
    {
        auto *helper = Dtk::Gui::DGuiApplicationHelper::instance();
        connect(helper, &Dtk::Gui::DGuiApplicationHelper::applicationPaletteChanged,
                this, &ThemeBridge::paletteChanged);
        connect(helper, &Dtk::Gui::DGuiApplicationHelper::themeTypeChanged,
                this, &ThemeBridge::paletteChanged);
    }

    QColor capsuleBackground() const {
        return palette().color(QPalette::ToolTipBase);
    }

    QColor capsuleBorder() const {
        return palette().color(Dtk::Gui::DPalette::FrameBorder);
    }

    QColor capsuleText() const {
        return palette().color(QPalette::ToolTipText);
    }

    QColor accent() const {
        return palette().color(QPalette::Highlight);
    }

    QColor accentText() const {
        return palette().color(QPalette::HighlightedText);
    }

signals:
    void paletteChanged();

private:
    Dtk::Gui::DPalette palette() const {
        return Dtk::Gui::DGuiApplicationHelper::instance()->applicationPalette();
    }
};

class UiSocketServer final : public QObject {
    Q_OBJECT
public:
    UiSocketServer(QString path, TooltipController *controller, QObject *parent = nullptr)
        : QObject(parent), path_(std::move(path)), controller_(controller) {}

    ~UiSocketServer() override {
        notifier_.reset();
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
        if (!path_.isEmpty()) {
            unlink(path_.toLocal8Bit().constData());
        }
    }

    bool start(QString *error) {
        QByteArray socketPath = path_.toLocal8Bit();
        sockaddr_un addr {};
        if (socketPath.size() >= static_cast<int>(sizeof(addr.sun_path))) {
            *error = QStringLiteral("UI socket path is too long: %1").arg(path_);
            return false;
        }

        fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd_ < 0) {
            *error = QStringLiteral("failed to create UI socket: %1").arg(strerror(errno));
            return false;
        }

        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socketPath.constData(), sizeof(addr.sun_path) - 1);
        unlink(socketPath.constData());
        if (bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            *error = QStringLiteral("failed to bind %1: %2").arg(path_, strerror(errno));
            close(fd_);
            fd_ = -1;
            return false;
        }
        chmod(socketPath.constData(), S_IRUSR | S_IWUSR);

        notifier_ = std::make_unique<QSocketNotifier>(fd_, QSocketNotifier::Read, this);
        connect(notifier_.get(), &QSocketNotifier::activated, this, &UiSocketServer::readPending);
        return true;
    }

private slots:
    void readPending() {
        while (true) {
            char buffer[4096] = {};
            ssize_t len = recv(fd_, buffer, sizeof(buffer) - 1, 0);
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return;
                }
                return;
            }
            applyMessage(QString::fromUtf8(buffer, static_cast<qsizetype>(len)).trimmed());
        }
    }

private:
    void applyMessage(const QString &message) {
        // The capsule now appears only when recording starts, so the legacy
        // SHOW_TOOLTIP (idle hint on input focus) is intentionally ignored.
        // Kept here so a still-running older service can't bring back the old
        // focus-tracking capsule.
        if (message.startsWith(QStringLiteral("SHOW_TOOLTIP"))) {
            return;
        } else if (message == QStringLiteral("RECORDING")) {
            recordingStreamActive_ = true;
            const TooltipPos pos = fixedCapsulePosition();
            controller_->setTooltip(true, QStringLiteral("正在聆听"), true,
                                     pos.hasPosition, pos.x, pos.y);
        } else if (message.startsWith(QStringLiteral("STREAM_TEXT"))) {
            if (!recordingStreamActive_) {
                return;
            }
            QString text = message.mid(QStringLiteral("STREAM_TEXT").size()).trimmed();
            if (!text.isEmpty()) {
                const TooltipPos pos = fixedCapsulePosition();
                controller_->setTooltip(true, text, true, pos.hasPosition, pos.x, pos.y);
            }
        } else if (message == QStringLiteral("TRANSCRIBING")) {
            recordingStreamActive_ = false;
            controller_->setTooltip(true, QStringLiteral("正在转写"), true);
        } else if (message == QStringLiteral("HIDE_TOOLTIP") || message == QStringLiteral("IDLE")) {
            recordingStreamActive_ = false;
            controller_->setTooltip(false, QString(), false);
        }
    }

    QString path_;
    TooltipController *controller_ = nullptr;
    int fd_ = -1;
    bool recordingStreamActive_ = false;
    std::unique_ptr<QSocketNotifier> notifier_;
};

} // namespace

int main(int argc, char **argv) {
    previousMessageHandler = qInstallMessageHandler(echoflowMessageHandler);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("echoflow-ui"));
    app.setOrganizationName(QStringLiteral("echoflow"));
    app.setQuitOnLastWindowClosed(false);

    const QIcon appIcon(QStringLiteral(":/icons/echoflow.svg"));
    app.setWindowIcon(appIcon);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("EchoFlow QML tooltip host"));
    parser.addHelpOption();

    QCommandLineOption activateOption(
        QStringLiteral("activate"),
        QStringLiteral("Open onboarding or settings in the running UI host"));
    QCommandLineOption qmlOption(QStringLiteral("qml"), QStringLiteral("QML file path"), QStringLiteral("path"),
                                 defaultQmlPath());
    QCommandLineOption configOption(
        QStringLiteral("config"), QStringLiteral("Path to echoflow.conf"),
        QStringLiteral("path"), defaultConfigPath());
    parser.addOption(activateOption);
    parser.addOption(qmlOption);
    parser.addOption(configOption);
    parser.process(app);

    bool pendingActivation = parser.isSet(activateOption);
    bool uiReady = false;
    std::function<void()> activationHandler;
    UiActivationServer activationServer(defaultUiLockPath());
    QObject::connect(&activationServer, &UiActivationServer::activateRequested,
                     &app, [&] {
        if (uiReady && activationHandler) {
            activationHandler();
        } else {
            pendingActivation = true;
        }
    });

    QString activationError;
    const UiActivationServer::Result activationResult =
        activationServer.acquire(pendingActivation, &activationError);
    if (activationResult == UiActivationServer::Result::ActivatedExisting) {
        return 0;
    }
    if (activationResult == UiActivationServer::Result::Failed) {
        qCritical("failed to acquire EchoFlow UI activation endpoint %s: %s",
                  qPrintable(defaultUiLockPath()), qPrintable(activationError));
        return 1;
    }

    // UiActivationServer is the authoritative instance endpoint. Keep DTK's
    // guard as a secondary safety check only after this process is primary.
    if (!setDtkSingleInstance(QStringLiteral("echoflow-ui"))) {
        logDuplicateInstanceExit();
        return 0;
    }

    const QString configPath = parser.value(configOption);
    if (!echoflow::EchoFlowSettings::instance()->init(configPath)) {
        QMessageBox::critical(nullptr, QStringLiteral("EchoFlow"),
                              QStringLiteral("无法加载设置 schema。"));
        return 1;
    }

    TooltipController controller(QString::fromStdString(defaultControlSocketPath()));
    ThemeBridge theme;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("tooltipController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    const QString qmlValue = parser.value(qmlOption);
    const QUrl qmlUrl = qmlValue.startsWith(QStringLiteral("qrc:"))
                            ? QUrl(qmlValue)
                            : QUrl::fromLocalFile(qmlValue);
    engine.load(qmlUrl);
    if (engine.rootObjects().isEmpty()) {
        return 2;
    }

    UiSocketServer server(QString::fromStdString(defaultUiSocketPath()), &controller);
    QString error;
    if (!server.start(&error)) {
        qCritical("%s", qPrintable(error));
        return 1;
    }

    QMenu trayMenu;
    QAction *guideAction = trayMenu.addAction(QObject::tr("使用引导"));
    QAction *settingsAction = trayMenu.addAction(QObject::tr("设置"));
    trayMenu.addSeparator();
    QAction *quitAction = trayMenu.addAction(QObject::tr("退出"));

    QSystemTrayIcon trayIcon;
    trayIcon.setContextMenu(&trayMenu);
    trayIcon.setIcon(appIcon);
    trayIcon.setToolTip(QStringLiteral("EchoFlow"));
    trayIcon.show();

    OnboardingState onboardingState;
    QProcessSetupCommandRunner setupCommandRunner;
    const QString configDir = QFileInfo(configPath).absolutePath();
    ModelSetupAdapter modelSetupAdapter(
        configDir,
        [] {
            auto *settings = echoflow::EchoFlowSettings::instance()->dsettings();
            if (!settings) {
                return QStringLiteral("hf-mirror");
            }
            auto option = settings->option(QStringLiteral("basic.model.mirror"));
            const QString mirror = option ? option->value().toString() : QString();
            return mirror.isEmpty() ? QStringLiteral("hf-mirror") : mirror;
        });
    modelSetupAdapter.observeCoordinator(
        echoflow::ModelDownloadCoordinator::instance());
    OnboardingSetupController onboardingController(
        &modelSetupAdapter, &setupCommandRunner, &onboardingState);

    QPointer<echoflow::SettingsDialog> settingsDialog;
    QPointer<OnboardingDialog> onboardingDialog;
    auto openSettings = [&]() {
        if (!settingsDialog) {
            settingsDialog = new echoflow::SettingsDialog(
                echoflow::EchoFlowSettings::instance()->dsettings());
            settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(settingsDialog, &echoflow::SettingsDialog::usageGuideRequested,
                             guideAction, &QAction::trigger);
            QObject::connect(settingsDialog, &QObject::destroyed, &app, [&]() {
                echoflow::EchoFlowSettings::instance()->sync();
                restartServiceAfterSettingsChange();
                settingsDialog = nullptr;
            });
        }
        echoflow::EchoFlowSettings::instance()->refreshModelNameItems();
        settingsDialog->show();
        settingsDialog->raise();
        settingsDialog->activateWindow();
    };

    auto showOnboarding = [&](bool replay) {
        if (!onboardingDialog) {
            onboardingDialog = new OnboardingDialog(&onboardingController);
            onboardingDialog->setWindowIcon(appIcon);
            onboardingDialog->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(
                onboardingDialog,
                &OnboardingDialog::finishedAndSettingsRequested,
                &app, [&] {
                    if (onboardingDialog) {
                        onboardingDialog->close();
                    }
                    openSettings();
                });
        }
        if (replay) {
            onboardingDialog->showForReplay();
        } else {
            onboardingDialog->showForIncompleteSetup();
        }
    };

    activationHandler = [&] {
        if (onboardingState.isComplete()) {
            openSettings();
        } else {
            showOnboarding(false);
        }
    };

    QObject::connect(guideAction, &QAction::triggered, &app,
                     [&] { showOnboarding(true); });
    QObject::connect(settingsAction, &QAction::triggered, openSettings);
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    uiReady = true;
    if (pendingActivation) {
        pendingActivation = false;
        QTimer::singleShot(0, &app, activationHandler);
    }

    return app.exec();
}

#include "main.moc"
