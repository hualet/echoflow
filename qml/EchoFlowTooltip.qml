// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

// Single fixed-position voice capsule. Morphs between idle (hint + mic button),
// recording (waveform + pause button), and transcribing (status text). When idle
// it fades out in two stages if the mouse is absent; the C++ host hides it
// outright when the service reports the user is typing.
Window {
    id: root
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    property string message: ""
    property bool busy: false
    property int targetX: 0
    property int targetY: 0
    readonly property color capsuleBackground: typeof theme !== "undefined" ? theme.capsuleBackground : "#232629"
    readonly property color capsuleBorder: typeof theme !== "undefined" ? theme.capsuleBorder : "#3f4348"
    readonly property color capsuleText: typeof theme !== "undefined" ? theme.capsuleText : "#f4f6f8"
    readonly property color accent: typeof theme !== "undefined" ? theme.accent : "#0081ff"
    readonly property color accentText: typeof theme !== "undefined" ? theme.accentText : "white"

    readonly property bool recording: root.busy && root.message === "正在聆听"
    readonly property bool transcribing: root.busy && root.message === "正在转写"
    readonly property bool idle: root.visible && !root.busy

    // (targetX, targetY) is the capsule's bottom-center anchor.
    width: capsule.width + 16
    height: capsule.height + 16
    x: targetX - width / 2
    y: targetY - height

    function stopFade() {
        fadeStage1.stop()
        fadeStage2.stop()
        hideAfterFade.stop()
    }

    Connections {
        target: tooltipController
        function onTooltipChanged(visibility, msg, isBusy, hasPosition, moveX, moveY) {
            root.message = msg
            root.busy = isBusy
            if (hasPosition) {
                root.targetX = moveX
                root.targetY = moveY
            }
            if (visibility) {
                root.visible = true
                root.stopFade()
                capsule.opacity = 1
                if (!isBusy) {
                    fadeStage1.restart()
                }
            } else {
                root.stopFade()
                root.visible = false
            }
        }
    }

    // stage 1: 2s of no mouse interaction -> opacity 0.1
    Timer {
        id: fadeStage1
        interval: 2000
        onTriggered: { capsule.opacity = 0.1; fadeStage2.restart() }
    }

    // stage 2: another 2s -> opacity 0, then hide
    Timer {
        id: fadeStage2
        interval: 2000
        onTriggered: { capsule.opacity = 0; hideAfterFade.start() }
    }

    // wait for the opacity animation (Behavior below) to finish before hiding
    Timer {
        id: hideAfterFade
        interval: 250
        onTriggered: {
            if (root.idle && capsule.opacity === 0) {
                root.visible = false
            }
        }
    }

    // ---------------- capsule body ----------------
    Rectangle {
        id: capsule
        anchors.centerIn: parent

        readonly property int kHeight: 40
        readonly property int kButtonSize: 32
        readonly property int kRadius: kHeight / 2
        readonly property int kButtonInset: kRadius - kButtonSize / 2
        readonly property int kHPad: 14
        readonly property int kGap: 12
        readonly property int kWaveBars: 11
        readonly property int kWaveBarWidth: 4
        readonly property int kWaveSpacing: 4
        readonly property int kWaveWidth: kWaveBars * kWaveBarWidth + (kWaveBars - 1) * kWaveSpacing

        height: kHeight
        radius: kRadius
        color: root.capsuleBackground
        border.color: root.capsuleBorder
        border.width: 1
        clip: true

        width: root.recording
               ? waveArea.width + kHPad + kGap + kButtonSize + kButtonInset
               : (root.transcribing
                  ? statusLabel.implicitWidth + 2 * kHPad
                  : idleHint.implicitWidth + kGap + kButtonSize + kHPad + kButtonInset)

        Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.InOutQuad } }
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }

        // ---- recording: waveform (left) ----
        Item {
            id: waveArea
            anchors {
                left: parent.left
                leftMargin: capsule.kHPad
                verticalCenter: parent.verticalCenter
            }
            width: capsule.kWaveWidth
            height: parent.height
            visible: root.recording

            Row {
                anchors.centerIn: parent
                spacing: capsule.kWaveSpacing

                Repeater {
                    model: capsule.kWaveBars
                    Rectangle {
                        width: capsule.kWaveBarWidth
                        radius: width / 2
                        color: root.accent
                        height: [4, 10, 16, 22, 18, 14, 18, 22, 16, 10, 4][index]
                        transformOrigin: Item.Center
                        SequentialAnimation on scale {
                            running: root.visible && root.recording
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 60 }
                            NumberAnimation { to: 1.5; duration: 250; easing.type: Easing.InOutQuad }
                            NumberAnimation { to: 0.5; duration: 250; easing.type: Easing.InOutQuad }
                        }
                    }
                }
            }
        }

        // ---- idle: hint text (left) ----
        Label {
            id: idleHint
            anchors {
                left: parent.left
                leftMargin: capsule.kHPad
                verticalCenter: parent.verticalCenter
            }
            visible: root.idle
            text: "按右 Ctrl 语音输入"
            color: root.capsuleText
            font.pixelSize: 13
        }

        // ---- transcribing: centered status ----
        Label {
            id: statusLabel
            anchors.centerIn: parent
            visible: root.transcribing
            text: root.message
            color: root.capsuleText
            font.pixelSize: 13
        }

        // ---- right-side action button: mic (idle) / pause (recording) ----
        Rectangle {
            id: actionButton
            width: capsule.kButtonSize
            height: capsule.kButtonSize
            radius: width / 2
            color: root.accent
            visible: root.idle || root.recording
            anchors {
                right: parent.right
                rightMargin: capsule.kButtonInset
                verticalCenter: parent.verticalCenter
            }

            // mic glyph (idle)
            Column {
                anchors.centerIn: parent
                spacing: 1
                visible: root.idle
                Rectangle { width: 8; height: 12; radius: 4; color: root.accentText; anchors.horizontalCenter: parent.horizontalCenter }
                Rectangle { width: 2; height: 3; color: root.accentText; anchors.horizontalCenter: parent.horizontalCenter }
                Rectangle { width: 12; height: 2; radius: 1; color: root.accentText; anchors.horizontalCenter: parent.horizontalCenter }
            }

            // pause glyph (recording)
            Row {
                anchors.centerIn: parent
                spacing: 4
                visible: root.recording
                Repeater {
                    model: 2
                    Rectangle {
                        width: 4; height: 12; radius: 1; color: root.accentText
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        // whole capsule is the hover + click target
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: (root.idle || root.recording) ? Qt.PointingHandCursor : Qt.ArrowCursor
            onEntered: {
                if (root.idle) {
                    root.stopFade()
                    capsule.opacity = 1
                }
            }
            onExited: {
                if (root.idle) {
                    root.stopFade()
                    fadeStage1.restart()
                }
            }
            onClicked: {
                if (root.idle || root.recording) {
                    tooltipController.requestToggle()
                }
            }
        }
    }
}
