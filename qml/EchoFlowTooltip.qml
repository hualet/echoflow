// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

Window {
    id: root
    width: bubble.width + 16
    height: bubble.height + 16
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    property string message: qsTr("长按 Ctrl 语音输入")
    property bool busy: false
    property bool collapsed: false
    // actively listening (recording) -> show the wave; transcribing/idle -> show text
    readonly property bool listening: root.busy && root.message === "正在聆听"

    // The collapse is tied to APPEARANCE, not to the recording state:
    // every time the capsule shows up, count 3s then shrink to the circle.
    onVisibleChanged: {
        if (root.visible) {
            root.collapsed = false
            collapseTimer.restart()
        } else {
            collapseTimer.stop()
            root.collapsed = false
        }
    }

    Connections {
        target: tooltipController
        function onTooltipChanged(visible, message, busy, hasPosition, moveX, moveY) {
            if (hasPosition) {
                root.x = moveX
                root.y = moveY
            }
            root.message = message
            root.busy = busy
            root.visible = visible
            if (message === "正在聆听") {
                // recording just started (e.g. user clicked the button) -> reveal the wave
                root.collapsed = false
                collapseTimer.restart()
            } else if (message === "正在转写") {
                // brief status, don't auto-collapse while it's shown
                root.collapsed = false
                collapseTimer.stop()
            }
        }
    }

    Rectangle {
        id: bubble
        anchors.centerIn: parent

        readonly property int kBubbleHeight: 40
        readonly property int kButtonSize: 32
        readonly property int kRadius: kBubbleHeight / 2
        readonly property int kButtonRadius: kButtonSize / 2
        // button concentric with the right arc: gap from bubble edge to button edge
        readonly property int kRightInset: kRadius - kButtonRadius
        // collapsed state is a circle: width == height, button sits dead center
        readonly property int kCollapsedWidth: kBubbleHeight
        readonly property int kLeftPad: 14
        readonly property int kGap: 12
        readonly property int kTextPad: 6

        // sound wave geometry — ~3x wider than the original 5-bar wave
        readonly property int kWaveBars: 11
        readonly property int kWaveBarWidth: 4
        readonly property int kWaveSpacing: 4
        readonly property int kWaveWidth: kWaveBars * kWaveBarWidth
                                        + (kWaveBars - 1) * kWaveSpacing

        height: kBubbleHeight
        radius: kRadius
        color: root.busy ? "#0f3d3e" : "#202124"
        border.color: root.busy ? "#18a6a7" : "#4a4d52"
        border.width: 1
        width: root.collapsed ? collapsedWidth : expandedWidth
        clip: true

        readonly property int collapsedWidth: kCollapsedWidth
        readonly property int expandedWidth:
            kLeftPad + leftContent.width + kGap + kButtonSize + kRightInset

        Behavior on width {
            NumberAnimation { duration: 300; easing.type: Easing.InOutQuad }
        }

        Item {
            id: leftContent
            anchors {
                left: parent.left
                leftMargin: bubble.kLeftPad
                verticalCenter: parent.verticalCenter
            }
            width: root.listening ? bubble.kWaveWidth
                                  : label.implicitWidth + 2 * bubble.kTextPad
            height: parent.height
            opacity: root.collapsed ? 0 : 1
            Behavior on opacity {
                NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
            }

            Label {
                id: label
                anchors.centerIn: parent
                text: root.message
                color: "#f4f6f8"
                font.pixelSize: 13
                visible: !root.listening
            }

            Row {
                id: waveRow
                anchors.centerIn: parent
                spacing: bubble.kWaveSpacing
                visible: root.listening

                Repeater {
                    model: bubble.kWaveBars
                    Rectangle {
                        width: bubble.kWaveBarWidth
                        radius: width / 2
                        color: "#18a6a7"
                        height: [4, 10, 16, 22, 18, 14, 18, 22, 16, 10, 4][index]
                        transformOrigin: Item.Center

                        SequentialAnimation on scale {
                            running: root.visible && root.listening && !root.collapsed
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 60 }
                            NumberAnimation {
                                to: 1.5
                                duration: 250
                                easing.type: Easing.InOutQuad
                            }
                            NumberAnimation {
                                to: 0.5
                                duration: 250
                                easing.type: Easing.InOutQuad
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: roundButton
            width: bubble.kButtonSize
            height: bubble.kButtonSize
            radius: width / 2
            color: "#18a6a7"
            // anchored to the right so its center = right-arc center (expanded)
            // and drifts to the bubble center as width collapses to a circle
            anchors {
                right: parent.right
                rightMargin: bubble.kRightInset
                verticalCenter: parent.verticalCenter
            }

            Row {
                anchors.centerIn: parent
                spacing: 1.5
                Repeater {
                    model: 3
                    Rectangle {
                        width: 2
                        radius: width / 2
                        color: "white"
                        height: [4, 8, 12][index]
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                // click == press right Ctrl -> toggle recording on/off
                onClicked: tooltipController.requestToggle()
            }
        }
    }

    Timer {
        id: collapseTimer
        interval: 3000
        onTriggered: root.collapsed = true
    }
}
