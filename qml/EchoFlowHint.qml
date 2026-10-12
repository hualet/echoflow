// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import org.deepin.dtk 1.0 as D

// The one-per-program focus hint, drawn in the capsule's visual language:
// a blurred, rounded pill with the DTK double border. The controller owns
// visibility, text, and placement; the window never accepts input so the
// press that starts a long press still reaches the text field below.
Window {
    id: root
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    D.DWindow.enableBlurWindow: root.visible
    D.DWindow.enabled: true
    D.DWindow.windowRadius: pill.kRadius
    visible: false

    property string hintText: ""
    property int hintX: 0
    property int hintY: 0
    readonly property color pillBackground: typeof theme !== "undefined" ? theme.capsuleBackground : "#232629"
    readonly property color pillBlend: Qt.rgba(pillBackground.r,
                                               pillBackground.g,
                                               pillBackground.b,
                                               D.DTK.themeType === D.ApplicationHelper.DarkType ? 0.6 : 0.4)
    readonly property color pillOutsideBorder: D.DTK.themeType === D.ApplicationHelper.DarkType
                                                ? Qt.rgba(0, 0, 0, 0.6)
                                                : Qt.rgba(0, 0, 0, 0.1)
    readonly property color pillInsideBorder: D.DTK.themeType === D.ApplicationHelper.DarkType
                                               ? Qt.rgba(1, 1, 1, 0.1)
                                               : Qt.rgba(1, 1, 1, 0.2)
    readonly property color pillText: typeof theme !== "undefined" ? theme.capsuleText : "#f4f6f8"

    // (hintX, hintY) is the pill's top-left anchor beside the caret; the
    // height matches CapsuleController::kHintHeight used for placement.
    width: pill.width
    height: pill.height
    x: hintX
    y: hintY

    Connections {
        target: tooltipController
        function onHintChanged(show, text, moveX, moveY) {
            root.hintText = text
            root.hintX = moveX
            root.hintY = moveY
            root.visible = show
        }
    }

    Rectangle {
        id: pill
        anchors.centerIn: parent

        readonly property int kHeight: 28
        readonly property int kHPad: 10
        readonly property int kRadius: kHeight / 2

        height: kHeight
        radius: kRadius
        color: "transparent"
        width: hintLabel.implicitWidth + 2 * kHPad

        D.StyledBehindWindowBlur {
            anchors.fill: parent
            control: root
            cornerRadius: pill.kRadius
            blendColor: root.pillBlend
        }

        D.OutsideBoxBorder {
            anchors.fill: parent
            radius: pill.kRadius
            color: root.pillOutsideBorder
            z: D.DTK.AboveOrder
        }

        D.InsideBoxBorder {
            anchors.fill: parent
            radius: pill.kRadius
            color: root.pillInsideBorder
            z: D.DTK.AboveOrder
        }

        Label {
            id: hintLabel
            anchors.centerIn: parent
            text: root.hintText
            color: root.pillText
            font.pixelSize: 13
        }
    }
}
