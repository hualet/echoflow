// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

// One window (QQmlApplicationEngine only shows the root Window). It morphs between
// two modes that are never visible at once: the idle trigger button (at the cursor)
// and the recording capsule (bottom-center). Position/size switch instantly so the
// button "disappears" and the capsule "appears" elsewhere, per the design.
Window {
    id: root
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    property string message: ""
    property bool busy: false
    property int targetX: 0
    property int targetY: 0

    readonly property bool isTrigger: root.visible && !root.busy
    readonly property bool listening: root.busy && root.message === "正在聆听"
    readonly property int kFloat: 11 // trigger center sits this far up-left of the cursor corner

    // geometry morphs with the mode (no behavior -> instant switch, no flying)
    width: isTrigger ? triggerBtn.width + 10 : bubble.width + 16
    height: isTrigger ? triggerBtn.height + 10 : bubble.height + 16
    x: isTrigger ? targetX - kFloat - width / 2 : targetX - width / 2
    y: isTrigger ? targetY - kFloat - height / 2 : targetY - height

    Connections {
        target: tooltipController
        function onTooltipChanged(visible, message, busy, hasPosition, moveX, moveY) {
            root.message = message
            root.busy = busy
            if (hasPosition) {
                root.targetX = moveX
                root.targetY = moveY
            }
            root.visible = visible
            if (visible && !busy) {
                triggerBtn.opacity = 1
                appearAnim.start()
                fadeTimer.restart()
            } else {
                fadeTimer.stop()
            }
        }
    }

    SequentialAnimation {
        id: appearAnim
        running: false
        NumberAnimation { target: triggerBtn; property: "scale"; from: 0.5; to: 1; duration: 150; easing.type: Easing.OutQuad }
    }

    // ---------------- idle trigger button (only in trigger mode) ----------------
    Rectangle {
        id: triggerBtn
        anchors.centerIn: parent
        visible: root.isTrigger
        width: 30
        height: 30
        radius: width / 2
        color: "#18a6a7"
        opacity: 1

        readonly property real restOpacity: 0.2
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }

        Column {
            anchors.centerIn: parent
            spacing: 1
            Rectangle { width: 8; height: 12; radius: 4; color: "white"; anchors.horizontalCenter: parent.horizontalCenter }
            Rectangle { width: 2; height: 3; color: "white"; anchors.horizontalCenter: parent.horizontalCenter }
            Rectangle { width: 12; height: 2; radius: 1; color: "white"; anchors.horizontalCenter: parent.horizontalCenter }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: { triggerBtn.opacity = 1; fadeTimer.stop() }
            onExited: triggerBtn.opacity = triggerBtn.restOpacity
            onClicked: tooltipController.requestToggle()
        }
    }

    Timer {
        id: fadeTimer
        interval: 1500
        onTriggered: triggerBtn.opacity = triggerBtn.restOpacity
    }

    // ---------------- recording capsule (only when busy) ----------------
    Rectangle {
        id: bubble
        anchors.centerIn: parent
        visible: root.busy

        readonly property int kBubbleHeight: 40
        readonly property int kButtonSize: 32
        readonly property int kRadius: kBubbleHeight / 2
        readonly property int kHPad: 14
        readonly property int kGap: 12
        readonly property int kWaveBars: 11
        readonly property int kWaveBarWidth: 4
        readonly property int kWaveSpacing: 4
        readonly property int kWaveWidth: kWaveBars * kWaveBarWidth + (kWaveBars - 1) * kWaveSpacing

        height: kBubbleHeight
        radius: kRadius
        color: "#0f3d3e"
        border.color: "#18a6a7"
        border.width: 1
        clip: true
        width: root.listening
               ? waveArea.width + kHPad + kGap + kButtonSize + kHPad
               : statusLabel.implicitWidth + 2 * kHPad

        Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.InOutQuad } }

        Item {
            id: waveArea
            anchors {
                left: parent.left
                leftMargin: bubble.kHPad
                verticalCenter: parent.verticalCenter
            }
            width: bubble.kWaveWidth
            height: parent.height
            visible: root.listening

            Row {
                anchors.centerIn: parent
                spacing: bubble.kWaveSpacing

                Repeater {
                    model: bubble.kWaveBars
                    Rectangle {
                        width: bubble.kWaveBarWidth
                        radius: width / 2
                        color: "#18a6a7"
                        height: [4, 10, 16, 22, 18, 14, 18, 22, 16, 10, 4][index]
                        transformOrigin: Item.Center
                        SequentialAnimation on scale {
                            running: root.visible && root.listening
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 60 }
                            NumberAnimation { to: 1.5; duration: 250; easing.type: Easing.InOutQuad }
                            NumberAnimation { to: 0.5; duration: 250; easing.type: Easing.InOutQuad }
                        }
                    }
                }
            }
        }

        Label {
            id: statusLabel
            anchors.centerIn: parent
            visible: !root.listening
            text: root.message
            color: "#f4f6f8"
            font.pixelSize: 13
        }

        Rectangle {
            id: pauseButton
            width: bubble.kButtonSize
            height: bubble.kButtonSize
            radius: width / 2
            color: "#18a6a7"
            visible: root.listening
            anchors {
                right: parent.right
                rightMargin: bubble.kHPad
                verticalCenter: parent.verticalCenter
            }

            Row {
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: 2
                    Rectangle {
                        width: 4
                        height: 12
                        radius: 1
                        color: "white"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: tooltipController.requestToggle()
            }
        }
    }
}
