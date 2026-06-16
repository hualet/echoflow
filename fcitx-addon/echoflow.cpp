// SPDX-FileCopyrightText: 2026 HarryLoong
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/rect.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/instance.h>
#include <fcitx/userinterfacemanager.h>

namespace {

constexpr const char *kControlSocketName = "echoflow-control.sock";
constexpr const char *kCommitSocketName = "echoflow-fcitx.sock";
constexpr size_t kMaxMessageSize = 65536;
constexpr std::string_view kCommitCommand = "COMMIT\n";
constexpr uint64_t kHoldThresholdUs = 350000;

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

std::string controlSocketPath() {
    return runtimeDir() + "/" + kControlSocketName;
}

std::string commitSocketPath() { return runtimeDir() + "/" + kCommitSocketName; }

bool sendControlCommand(std::string_view command) {
    const std::string path = controlSocketPath();
    const std::string clientPath = runtimeDir() + "/echoflow-addon-" +
                                   std::to_string(getpid()) + "-" +
                                   std::to_string(fcitx::now(CLOCK_MONOTONIC)) +
                                   ".sock";
    sockaddr_un server {};
    sockaddr_un client {};
    if (path.size() >= sizeof(server.sun_path) ||
        clientPath.size() >= sizeof(client.sun_path)) {
        return false;
    }

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }

    server.sun_family = AF_UNIX;
    std::strncpy(server.sun_path, path.c_str(), sizeof(server.sun_path) - 1);
    client.sun_family = AF_UNIX;
    std::strncpy(client.sun_path, clientPath.c_str(),
                 sizeof(client.sun_path) - 1);

    unlink(clientPath.c_str());
    bool ok = bind(fd, reinterpret_cast<sockaddr *>(&client), sizeof(client)) ==
              0;
    if (ok) {
        ok = sendto(fd, command.data(), command.size(), 0,
                    reinterpret_cast<sockaddr *>(&server), sizeof(server)) >= 0;
    }

    close(fd);
    unlink(clientPath.c_str());
    return ok;
}

struct Request {
    enum class Type {
        Commit,
        Ping,
    };

    Type type = Type::Commit;
    std::string text;
};

Request parseRequest(const std::string &message) {
    if (message.empty() || message == "PING" || message == "PING\n") {
        return {Request::Type::Ping, {}};
    }
    if (message.rfind(kCommitCommand, 0) == 0) {
        return {Request::Type::Commit,
                message.substr(kCommitCommand.size())};
    }
    return {Request::Type::Commit, message};
}

class EchoFlow final : public fcitx::AddonInstance {
public:
    explicit EchoFlow(fcitx::AddonManager *manager)
        : instance_(manager->instance()) {
        setupCommitSocket();
        registerEventWatchers();
    }

    ~EchoFlow() override {
        ioEvent_.reset();
        holdTimer_.reset();
        unlinkOwnedCommitSocket();
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

private:
    void unlinkOwnedCommitSocket() {
        if (!socketBound_ || fd_ < 0) {
            return;
        }
        struct stat fdStat {};
        struct stat pathStat {};
        if (fstat(fd_, &fdStat) != 0 ||
            stat(commitSocketPath_.c_str(), &pathStat) != 0) {
            return;
        }
        if (fdStat.st_dev == pathStat.st_dev &&
            fdStat.st_ino == pathStat.st_ino) {
            unlink(commitSocketPath_.c_str());
        }
    }

    void setupCommitSocket() {
        commitSocketPath_ = commitSocketPath();
        sockaddr_un addr {};
        if (commitSocketPath_.size() >= sizeof(addr.sun_path)) {
            FCITX_ERROR() << "EchoFlow socket path too long: "
                          << commitSocketPath_;
            return;
        }

        fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd_ < 0) {
            FCITX_ERROR() << "EchoFlow failed to create socket: "
                          << strerror(errno);
            return;
        }

        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, commitSocketPath_.c_str(),
                     sizeof(addr.sun_path) - 1);
        unlink(commitSocketPath_.c_str());
        if (bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            FCITX_ERROR() << "EchoFlow failed to bind " << commitSocketPath_
                          << ": " << strerror(errno);
            close(fd_);
            fd_ = -1;
            return;
        }
        socketBound_ = true;
        chmod(commitSocketPath_.c_str(), S_IRUSR | S_IWUSR);

        ioEvent_ = instance_->eventLoop().addIOEvent(
            fd_, fcitx::IOEventFlag::In,
            [this](fcitx::EventSourceIO *, int fd, fcitx::IOEventFlags flags) {
                return handleReadable(fd, flags);
            });
        FCITX_INFO() << "EchoFlow commit socket listening on "
                     << commitSocketPath_;
    }

    void registerEventWatchers() {
        eventWatchers_.push_back(instance_->watchEvent(
            fcitx::EventType::InputContextFocusIn,
            fcitx::EventWatcherPhase::Default,
            [this](fcitx::Event &event) { handleFocusEvent(event, true); }));
        eventWatchers_.push_back(instance_->watchEvent(
            fcitx::EventType::InputContextFocusOut,
            fcitx::EventWatcherPhase::Default,
            [this](fcitx::Event &event) { handleFocusEvent(event, false); }));
        eventWatchers_.push_back(instance_->watchEvent(
            fcitx::EventType::InputContextCursorRectChanged,
            fcitx::EventWatcherPhase::Default,
            [this](fcitx::Event &event) { handleCursorRectChanged(event); }));
        eventWatchers_.push_back(instance_->watchEvent(
            fcitx::EventType::InputContextKeyEvent,
            fcitx::EventWatcherPhase::Default,
            [this](fcitx::Event &event) { handleKeyEvent(event); }));
    }

    void handleFocusEvent(fcitx::Event &event, bool focused) {
        auto *icEvent = static_cast<fcitx::InputContextEvent *>(&event);
        focusedInputContext_ = focused ? icEvent->inputContext() : nullptr;
        if (focused && focusedInputContext_) {
            sendFocusCommand(focusedInputContext_);
        } else {
            sendControlCommand("BLUR");
        }
    }

    void handleCursorRectChanged(fcitx::Event &event) {
        auto *icEvent = static_cast<fcitx::InputContextEvent *>(&event);
        if (focusedInputContext_ && icEvent->inputContext() == focusedInputContext_) {
            sendFocusCommand(focusedInputContext_);
        }
    }

    void sendFocusCommand(fcitx::InputContext *ic) {
        const fcitx::Rect &rect = ic->cursorRect();
        if (rect.isEmpty()) {
            sendControlCommand("FOCUS");
            return;
        }
        const std::string command =
            "FOCUS " + std::to_string(rect.left()) + " " +
            std::to_string(rect.top()) + " " + std::to_string(rect.width()) +
            " " + std::to_string(rect.height());
        sendControlCommand(command);
    }

    void handleKeyEvent(fcitx::Event &event) {
        auto *keyEvent = static_cast<fcitx::KeyEvent *>(&event);
        if (!isPlainCtrl(keyEvent->key())) {
            return;
        }
        if (keyEvent->isRelease()) {
            ctrlHeld_ = false;
            holdStarted_ = false;
            holdTimer_.reset();
            sendControlCommand("CTRL_UP");
            keyEvent->filterAndAccept();
            return;
        }

        if (!ctrlHeld_) {
            ctrlHeld_ = true;
            holdStarted_ = false;
            ctrlPressedAtUs_ = fcitx::now(CLOCK_MONOTONIC);
            sendControlCommand("CTRL_DOWN");
            armHoldTimer();
        }
        keyEvent->filterAndAccept();
    }

    static bool isPlainCtrl(const fcitx::Key &key) {
        const auto sym = key.sym();
        return sym == FcitxKey_Control_L || sym == FcitxKey_Control_R;
    }

    void armHoldTimer() {
        holdTimer_ = instance_->eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, nowPlus(kHoldThresholdUs), 0,
            [this](fcitx::EventSourceTime *, uint64_t) {
                if (!ctrlHeld_ || holdStarted_) {
                    return false;
                }
                const uint64_t now = fcitx::now(CLOCK_MONOTONIC);
                if (now >= ctrlPressedAtUs_ &&
                    now - ctrlPressedAtUs_ >= kHoldThresholdUs) {
                    holdStarted_ = true;
                    sendControlCommand("TICK");
                }
                return false;
            });
    }

    uint64_t nowPlus(uint64_t offsetUs) const {
        return fcitx::now(CLOCK_MONOTONIC) + offsetUs;
    }

    bool handleReadable(int fd, fcitx::IOEventFlags flags) {
        if (flags.testAny(fcitx::IOEventFlag::Err) ||
            flags.testAny(fcitx::IOEventFlag::Hup)) {
            FCITX_WARN() << "EchoFlow socket event error/hangup";
        }

        while (true) {
            sockaddr_un peer {};
            socklen_t peerLen = sizeof(peer);
            std::array<char, kMaxMessageSize> buffer {};
            ssize_t len =
                recvfrom(fd, buffer.data(), buffer.size(), 0,
                         reinterpret_cast<sockaddr *>(&peer), &peerLen);
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return true;
                }
                FCITX_WARN() << "EchoFlow recvfrom failed: "
                             << strerror(errno);
                return true;
            }
            handleMessage(std::string(buffer.data(), static_cast<size_t>(len)),
                          peer, peerLen);
        }
    }

    void handleMessage(const std::string &text, const sockaddr_un &peer,
                       socklen_t peerLen) {
        Request request = parseRequest(text);
        if (request.type == Request::Type::Ping) {
            reply(peer, peerLen, "PONG");
            return;
        }
        if (request.text.empty()) {
            reply(peer, peerLen, "ERR empty-text");
            return;
        }
        if (request.text.find('\0') != std::string::npos) {
            reply(peer, peerLen, "ERR invalid-text");
            return;
        }

        fcitx::InputContext *ic =
            focusedInputContext_ ? focusedInputContext_
                                 : instance_->lastFocusedInputContext();
        if (!ic) {
            reply(peer, peerLen, "ERR no-focused-input-context");
            return;
        }

        if (ic->capabilityFlags() &
            fcitx::CapabilityFlag::CommitStringWithCursor) {
            ic->commitStringWithCursor(request.text,
                                       fcitx::utf8::length(request.text));
        } else {
            ic->commitString(request.text);
        }
        reply(peer, peerLen, "OK");
    }

    void reply(const sockaddr_un &peer, socklen_t peerLen,
               const char *message) {
        if (peerLen == 0 || peer.sun_path[0] == '\0') {
            return;
        }
        if (sendto(fd_, message, strlen(message), 0,
                   reinterpret_cast<const sockaddr *>(&peer), peerLen) < 0) {
            FCITX_WARN() << "EchoFlow reply failed: " << strerror(errno);
        }
    }

    fcitx::Instance *instance_ = nullptr;
    int fd_ = -1;
    bool socketBound_ = false;
    bool ctrlHeld_ = false;
    bool holdStarted_ = false;
    uint64_t ctrlPressedAtUs_ = 0;
    std::string commitSocketPath_;
    std::unique_ptr<fcitx::EventSourceIO> ioEvent_;
    std::unique_ptr<fcitx::EventSourceTime> holdTimer_;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
        eventWatchers_;
    fcitx::InputContext *focusedInputContext_ = nullptr;
};

class EchoFlowFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new EchoFlow(manager);
    }
};

} // namespace

FCITX_ADDON_FACTORY(EchoFlowFactory)
