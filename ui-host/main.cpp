// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QSocketNotifier>
#include <QString>
#include <QUrl>

namespace {

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

QString defaultQmlPath() {
    return QStringLiteral(ECHOFLOW_QML_DIR) + QStringLiteral("/EchoFlowTooltip.qml");
}

std::string defaultControlSocketPath() {
    return runtimeDir() + "/echoflow-control.sock";
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

signals:
    void tooltipChanged(bool visible, const QString &message, bool busy,
                        bool hasPosition, int moveX, int moveY);

private:
    QString controlPath_;
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
        if (message.startsWith(QStringLiteral("SHOW_TOOLTIP"))) {
            QString text = message.mid(QStringLiteral("SHOW_TOOLTIP").size()).trimmed();
            bool hasPosition = false;
            int moveX = 0;
            int moveY = 0;
            const QStringList parts = text.split(QChar(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 5) {
                bool okX = false;
                bool okY = false;
                bool okW = false;
                bool okH = false;
                const int x = parts.at(0).toInt(&okX);
                const int y = parts.at(1).toInt(&okY);
                const int width = parts.at(2).toInt(&okW);
                const int height = parts.at(3).toInt(&okH);
                if (okX && okY && okW && okH) {
                    hasPosition = true;
                    qreal dpr = 1.0;
                    if (auto *screen = QGuiApplication::primaryScreen()) {
                        dpr = screen->devicePixelRatio();
                    }
                    const int physX = x;
                    const int physY = y + height;
                    moveX = dpr > 1.0
                                ? static_cast<int>(std::round(physX / dpr))
                                : physX;
                    moveY = (dpr > 1.0
                                 ? static_cast<int>(std::round(physY / dpr))
                                 : physY) +
                            8;
                    text = parts.mid(4).join(QChar(' '));
                }
            }
            if (text.isEmpty()) {
                text = QStringLiteral("长按 Ctrl 语音输入");
            }
            controller_->setTooltip(true, text, false, hasPosition, moveX, moveY);
        } else if (message == QStringLiteral("RECORDING")) {
            controller_->setTooltip(true, QStringLiteral("正在聆听"), true);
        } else if (message == QStringLiteral("TRANSCRIBING")) {
            controller_->setTooltip(true, QStringLiteral("正在转写"), true);
        } else if (message == QStringLiteral("HIDE_TOOLTIP") || message == QStringLiteral("IDLE")) {
            controller_->setTooltip(false, QString(), false);
        }
    }

    QString path_;
    TooltipController *controller_ = nullptr;
    int fd_ = -1;
    std::unique_ptr<QSocketNotifier> notifier_;
};

} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("EchoFlow"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("EchoFlow QML tooltip host"));
    parser.addHelpOption();
    QCommandLineOption socketOption(QStringLiteral("socket"), QStringLiteral("UI socket path"), QStringLiteral("path"),
                                    QString::fromStdString(defaultUiSocketPath()));
    QCommandLineOption qmlOption(QStringLiteral("qml"), QStringLiteral("QML file path"), QStringLiteral("path"),
                                 defaultQmlPath());
    QCommandLineOption controlSocketOption(
        QStringLiteral("control-socket"), QStringLiteral("echoflow-service control socket path"),
        QStringLiteral("path"), QString::fromStdString(defaultControlSocketPath()));
    parser.addOption(socketOption);
    parser.addOption(qmlOption);
    parser.addOption(controlSocketOption);
    parser.process(app);

    TooltipController controller(parser.value(controlSocketOption));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("tooltipController"), &controller);
    engine.load(QUrl::fromLocalFile(parser.value(qmlOption)));
    if (engine.rootObjects().isEmpty()) {
        return 2;
    }

    UiSocketServer server(parser.value(socketOption), &controller);
    QString error;
    if (!server.start(&error)) {
        qCritical("%s", qPrintable(error));
        return 1;
    }

    return app.exec();
}

#include "main.moc"
