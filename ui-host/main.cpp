// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>
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
#include <QQmlApplicationEngine>
#include <QQmlContext>
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

class TooltipController final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

public slots:
    void setTooltip(bool visible, const QString &message, bool busy = false,
                    bool hasPosition = false, int moveX = 0, int moveY = 0) {
        emit tooltipChanged(visible, message, busy, hasPosition, moveX, moveY);
    }

signals:
    void tooltipChanged(bool visible, const QString &message, bool busy,
                        bool hasPosition, int moveX, int moveY);
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
                    moveX = x + width;
                    moveY = y + height + 8;
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
    parser.addOption(socketOption);
    parser.addOption(qmlOption);
    parser.process(app);

    TooltipController controller;
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
