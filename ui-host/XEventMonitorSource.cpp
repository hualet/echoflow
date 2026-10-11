// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "XEventMonitorSource.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QtGlobal>

namespace {

constexpr int kLeftButton = 1;

// Current deepin first, then the name used before the dde rename.
constexpr XEventMonitorSource::Endpoint kEndpoints[] = {
    {"org.deepin.dde.XEventMonitor1", "/org/deepin/dde/XEventMonitor1",
     "org.deepin.dde.XEventMonitor1"},
    {"com.deepin.api.XEventMonitor", "/com/deepin/api/XEventMonitor",
     "com.deepin.api.XEventMonitor"},
};

} // namespace

XEventMonitorSource::XEventMonitorSource(QObject *parent)
    : QObject(parent) {}

XEventMonitorSource::~XEventMonitorSource() {
    stop();
}

bool XEventMonitorSource::platformSupported() {
    // deepin runs DTK apps on its "dxcb" wrapper around the xcb platform.
    const QString platform = QGuiApplication::platformName();
    return (platform == QStringLiteral("xcb") || platform == QStringLiteral("dxcb"))
        && qEnvironmentVariable("XDG_SESSION_TYPE") != QStringLiteral("wayland");
}

bool XEventMonitorSource::active() const {
    return endpoint_ != nullptr;
}

bool XEventMonitorSource::start() {
    wanted_ = true;
    if (active()) {
        return true;
    }
    if (!platformSupported()) {
        qInfo("EchoFlow long-press: unsupported platform %s",
              qPrintable(QGuiApplication::platformName()));
        return false;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!watcher_) {
        watcher_ = new QDBusServiceWatcher(this);
        watcher_->setConnection(bus);
        for (const Endpoint &endpoint : kEndpoints) {
            watcher_->addWatchedService(QString::fromLatin1(endpoint.service));
        }
        // The monitor forgets registrations when it restarts.
        connect(watcher_, &QDBusServiceWatcher::serviceRegistered, this, [this] {
            if (wanted_ && !active()) {
                start();
            }
        });
        connect(watcher_, &QDBusServiceWatcher::serviceUnregistered, this,
                [this](const QString &service) {
                    if (endpoint_ && service == QString::fromLatin1(endpoint_->service)) {
                        disconnectSignals();
                        setRegistration(nullptr, {});
                    }
                });
    }

    for (const Endpoint &endpoint : kEndpoints) {
        if (registerWith(endpoint)) {
            return true;
        }
    }
    qWarning("EchoFlow long-press: XEventMonitor service is unavailable");
    return false;
}

bool XEventMonitorSource::registerWith(const Endpoint &endpoint) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString service = QString::fromLatin1(endpoint.service);
    if (!bus.interface() || !bus.interface()->isServiceRegistered(service)) {
        return false;
    }

    const QString path = QString::fromLatin1(endpoint.path);
    const QString interface = QString::fromLatin1(endpoint.interface);
    QDBusMessage call = QDBusMessage::createMethodCall(
        service, path, interface, QStringLiteral("RegisterFullScreen"));
    const QDBusReply<QString> reply = bus.call(call, QDBus::Block, 2000);
    if (!reply.isValid() || reply.value().isEmpty()) {
        qWarning("EchoFlow long-press: RegisterFullScreen failed on %s: %s",
                 endpoint.service, qPrintable(reply.error().message()));
        return false;
    }

    const bool pressConnected = bus.connect(
        service, path, interface, QStringLiteral("ButtonPress"), this,
        SLOT(handleButtonPress(int,int,int,QString)));
    const bool releaseConnected = bus.connect(
        service, path, interface, QStringLiteral("ButtonRelease"), this,
        SLOT(handleButtonRelease(int,int,int,QString)));
    if (!pressConnected || !releaseConnected) {
        qWarning("EchoFlow long-press: cannot subscribe to %s button signals",
                 endpoint.service);
        return false;
    }

    setRegistration(&endpoint, reply.value());
    qInfo("EchoFlow long-press: listening for buttons via %s", endpoint.service);
    return true;
}

void XEventMonitorSource::stop() {
    wanted_ = false;
    if (!endpoint_) {
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage call = QDBusMessage::createMethodCall(
        QString::fromLatin1(endpoint_->service), QString::fromLatin1(endpoint_->path),
        QString::fromLatin1(endpoint_->interface), QStringLiteral("UnregisterArea"));
    call << registrationId_;
    bus.call(call, QDBus::NoBlock);
    disconnectSignals();
    setRegistration(nullptr, {});
}

void XEventMonitorSource::disconnectSignals() {
    if (!endpoint_) {
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString service = QString::fromLatin1(endpoint_->service);
    const QString path = QString::fromLatin1(endpoint_->path);
    const QString interface = QString::fromLatin1(endpoint_->interface);
    bus.disconnect(service, path, interface, QStringLiteral("ButtonPress"), this,
                   SLOT(handleButtonPress(int,int,int,QString)));
    bus.disconnect(service, path, interface, QStringLiteral("ButtonRelease"), this,
                   SLOT(handleButtonRelease(int,int,int,QString)));
}

void XEventMonitorSource::setRegistration(const Endpoint *endpoint, const QString &id) {
    const bool wasActive = active();
    endpoint_ = endpoint;
    registrationId_ = id;
    if (wasActive != active()) {
        emit activeChanged(active());
    }
}

void XEventMonitorSource::handleButtonPress(int button, int x, int y, const QString &id) {
    if (button == kLeftButton && id == registrationId_) {
        emit leftPressed(QPoint(x, y));
    }
}

void XEventMonitorSource::handleButtonRelease(int button, int x, int y, const QString &id) {
    if (button == kLeftButton && id == registrationId_) {
        emit leftReleased(QPoint(x, y));
    }
}
