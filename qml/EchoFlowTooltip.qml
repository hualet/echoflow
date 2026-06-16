// SPDX-FileCopyrightText: 2026 HarryLoong
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

Window {
    id: root
    width: bubble.implicitWidth + 28
    height: bubble.implicitHeight + 20
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    property string message: qsTr("长按 Ctrl 语音输入")
    property bool busy: false

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
        }
    }

    Rectangle {
        id: bubble
        anchors.centerIn: parent
        radius: 8
        color: root.busy ? "#0f3d3e" : "#202124"
        border.color: root.busy ? "#18a6a7" : "#4a4d52"
        border.width: 1
        implicitWidth: label.implicitWidth + 24
        implicitHeight: label.implicitHeight + 14

        Label {
            id: label
            anchors.centerIn: parent
            text: root.message
            color: "#f4f6f8"
            font.pixelSize: 13
        }
    }
}
