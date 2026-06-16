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

    onBusyChanged: {
        if (!busy) {
            collapseTimer.stop()
            root.collapsed = false
        }
    }

    onVisibleChanged: {
        if (!visible) {
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
            root.visible = visible
            root.message = message
            root.busy = busy
            if (message === "正在转写") {
                collapseTimer.stop()
                root.collapsed = false
            } else if (busy && message === "正在聆听") {
                collapseTimer.restart()
            }
        }
    }

    Rectangle {
        id: bubble
        anchors.centerIn: parent
        height: 40
        radius: height / 2
        color: root.busy ? "#0f3d3e" : "#202124"
        border.color: root.busy ? "#18a6a7" : "#4a4d52"
        border.width: 1
        width: root.collapsed ? collapsedWidth : expandedWidth
        clip: true

        readonly property int collapsedWidth: 40
        readonly property int expandedWidth: leftContent.width + 16 + roundButton.width

        Behavior on width {
            NumberAnimation { duration: 300; easing.type: Easing.InOutQuad }
        }

        Item {
            id: leftContent
            anchors {
                left: parent.left
                leftMargin: 8
                verticalCenter: parent.verticalCenter
            }
            width: root.busy ? waveRow.width : label.implicitWidth
            height: parent.height

            Label {
                id: label
                anchors.centerIn: parent
                text: root.message
                color: "#f4f6f8"
                font.pixelSize: 13
                visible: !root.busy
            }

            Row {
                id: waveRow
                anchors.centerIn: parent
                spacing: 3
                visible: root.busy

                Repeater {
                    model: 5
                    Rectangle {
                        width: 3
                        radius: width / 2
                        color: "#18a6a7"
                        height: [6, 12, 18, 10, 14][index]
                        transformOrigin: Item.Center

                        SequentialAnimation on scaleY {
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 100 }
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
            width: 32
            height: 32
            radius: width / 2
            color: "#18a6a7"
            anchors {
                right: parent.right
                rightMargin: 4
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
                onClicked: {
                    if (root.busy) {
                        root.collapsed = !root.collapsed
                    }
                }
            }
        }
    }

    Timer {
        id: collapseTimer
        interval: 3000
        onTriggered: {
            if (root.busy) {
                root.collapsed = true
            }
        }
    }
}
