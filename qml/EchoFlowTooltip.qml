// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import org.deepin.dtk 1.0 as D

// Voice capsule shown only while a recording or transcription is in flight.
// Appears the moment recording starts and disappears when the session ends
// (IDLE). During recording the left X button cancels (discard), the right
// check button stops and starts transcription; a symbolic waveform animates
// between them. While transcribing it shows centered status text.
Window {
    id: root
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    D.DWindow.enableBlurWindow: root.visible && capsule.opacity > 0
    D.DWindow.enabled: true
    D.DWindow.windowRadius: capsule.kRadius
    visible: false

    property string message: ""
    property bool busy: false
    property int targetX: 0
    property int targetY: 0
    readonly property color capsuleBackground: typeof theme !== "undefined" ? theme.capsuleBackground : "#232629"
    readonly property color capsuleBlend: Qt.rgba(capsuleBackground.r,
                                                   capsuleBackground.g,
                                                   capsuleBackground.b,
                                                   D.DTK.themeType === D.ApplicationHelper.DarkType ? 0.6 : 0.4)
    readonly property color capsuleOutsideBorder: D.DTK.themeType === D.ApplicationHelper.DarkType
                                                    ? Qt.rgba(0, 0, 0, 0.6)
                                                    : Qt.rgba(0, 0, 0, 0.1)
    readonly property color capsuleInsideBorder: D.DTK.themeType === D.ApplicationHelper.DarkType
                                                   ? Qt.rgba(1, 1, 1, 0.1)
                                                   : Qt.rgba(1, 1, 1, 0.2)
    readonly property color capsuleBorder: typeof theme !== "undefined" ? theme.capsuleBorder : "#3f4348"
    readonly property color capsuleText: typeof theme !== "undefined" ? theme.capsuleText : "#f4f6f8"
    readonly property color accent: typeof theme !== "undefined" ? theme.accent : "#0081ff"
    readonly property color accentText: typeof theme !== "undefined" ? theme.accentText : "white"

    readonly property bool transcribing: root.busy && root.message === "正在转写"
    readonly property bool recording: root.busy && !root.transcribing
    readonly property bool hasLiveText: root.recording && root.message !== "正在聆听"

    // (targetX, targetY) is the capsule's bottom-center anchor.
    width: capsule.width
    height: capsule.height
    x: targetX - width / 2
    y: targetY - height

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
                capsule.opacity = 1
            } else {
                root.visible = false
            }
        }
    }

    // ---------------- capsule body ----------------
    Rectangle {
        id: capsule
        anchors.centerIn: parent

        readonly property int kHeight: 40
        readonly property int kButtonSize: 28
        readonly property int kRadius: kHeight / 2
        readonly property int kButtonInset: (kHeight - kButtonSize) / 2
        readonly property int kHPad: 10
        readonly property int kGap: 8
        // symbolic waveform geometry. kWaveHeight caps the tallest bar so the
        // wave stays roughly level with the 13px text and the 12-14px glyphs.
        readonly property int kWaveBars: 5
        readonly property int kWaveBarWidth: 3
        readonly property int kWaveSpacing: 5
        readonly property int kWaveHeight: 16
        readonly property int kWaveWidth: kWaveBars * kWaveBarWidth + (kWaveBars - 1) * kWaveSpacing

        height: kHeight
        radius: kRadius
        color: "transparent"
        clip: false

        // recording: [pad][X][gap][live text][gap][wave][gap][✓][pad]
        // transcribing: [ centered status text ]
        width: root.recording
               ? kHPad + kButtonSize            // X button
                 + (root.hasLiveText ? kGap + liveText.width : 0)
                 + kGap + waveArea.width        // waveform (always present)
                 + kGap + kButtonSize + kHPad   // ✓ button
               : statusLabel.implicitWidth + 2 * kHPad

        Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }

        D.StyledBehindWindowBlur {
            anchors.fill: parent
            control: root
            cornerRadius: capsule.kRadius
            blendColor: root.capsuleBlend
        }

        D.OutsideBoxBorder {
            anchors.fill: parent
            radius: capsule.kRadius
            color: root.capsuleOutsideBorder
            z: D.DTK.AboveOrder
        }

        D.InsideBoxBorder {
            anchors.fill: parent
            radius: capsule.kRadius
            color: root.capsuleInsideBorder
            z: D.DTK.AboveOrder
        }

        // ---- left: cancel (X) button ----
        Item {
            id: cancelButton
            width: capsule.kButtonSize
            height: capsule.kButtonSize
            anchors {
                left: parent.left
                leftMargin: capsule.kHPad
                verticalCenter: parent.verticalCenter
            }
            visible: root.recording

            Rectangle {
                id: cancelBg
                anchors.fill: parent
                radius: width / 2
                color: cancelArea.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                Behavior on color { ColorAnimation { duration: 120 } }
            }

            // symbolic X glyph
            Canvas {
                anchors.centerIn: parent
                width: 12; height: 12
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = root.capsuleText
                    ctx.lineWidth = 1.8
                    ctx.lineCap = "round"
                    ctx.beginPath()
                    ctx.moveTo(2, 2); ctx.lineTo(10, 10)
                    ctx.moveTo(10, 2); ctx.lineTo(2, 10)
                    ctx.stroke()
                }
            }

            MouseArea {
                id: cancelArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: tooltipController.requestCancel()
            }
        }

        // ---- recording: symbolic waveform (always shown, right of the text) ----
        Item {
            id: waveArea
            anchors {
                left: root.hasLiveText ? liveText.right : cancelButton.right
                leftMargin: capsule.kGap
                verticalCenter: parent.verticalCenter
            }
            width: capsule.kWaveWidth
            height: capsule.kWaveHeight
            visible: root.recording

            Row {
                anchors.centerIn: parent
                spacing: capsule.kWaveSpacing

                Repeater {
                    model: capsule.kWaveBars
                    Item {
                        width: capsule.kWaveBarWidth
                        height: waveArea.height
                        // symmetric, organic peak profile (center tallest)
                        readonly property real peak: [0.34, 0.66, 1.0, 0.66, 0.34][index]

                        Rectangle {
                            anchors.centerIn: parent
                            width: capsule.kWaveBarWidth
                            radius: width / 2
                            color: root.accent
                            // animated amplitude as a fraction of peak height
                            height: parent.peak * waveArea.height * (0.45 + 0.55 * waveAnim.amplitudes[index])
                            y: (parent.height - height) / 2
                            Behavior on height { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
                        }
                    }
                }
            }
        }

        // ---- recording: live ASR text (left of the waveform) ----
        Label {
            id: liveText
            anchors {
                left: cancelButton.right
                leftMargin: capsule.kGap
                verticalCenter: parent.verticalCenter
            }
            visible: root.hasLiveText
            text: root.message
            color: root.capsuleText
            font.pixelSize: 13
            width: Math.min(implicitWidth, 420)
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideRight
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

        // ---- right: stop & transcribe (✓) button ----
        Item {
            id: confirmButton
            width: capsule.kButtonSize
            height: capsule.kButtonSize
            anchors {
                right: parent.right
                rightMargin: capsule.kHPad
                verticalCenter: parent.verticalCenter
            }
            visible: root.recording

            Rectangle {
                id: confirmBg
                anchors.fill: parent
                radius: width / 2
                color: root.accent
            }

            // symbolic check glyph
            Canvas {
                anchors.centerIn: parent
                width: 14; height: 14
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = root.accentText
                    ctx.lineWidth = 2.0
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.moveTo(2, 7.5)
                    ctx.lineTo(5.5, 11)
                    ctx.lineTo(12, 3.5)
                    ctx.stroke()
                }
            }

            MouseArea {
                id: confirmArea
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: tooltipController.requestToggle()
            }
        }
    }

    // Drives the symbolic waveform: a small set of smoothly varying amplitudes
    // (0..1) updated on a short cadence so the bars breathe organically rather
    // than the old mechanical per-bar scale animation.
    Timer {
        id: waveTimer
        interval: 110
        repeat: true
        running: root.visible && root.recording && waveArea.visible
        onTriggered: {
            var next = []
            for (var i = 0; i < capsule.kWaveBars; ++i) {
                // ease toward a new random target to avoid jittery jumps
                var target = 0.25 + 0.75 * Math.random()
                var cur = waveAnim.amplitudes.length === capsule.kWaveBars
                          ? waveAnim.amplitudes[i] : 0.5
                next.push(cur + (target - cur) * 0.55)
            }
            waveAnim.amplitudes = next
        }
    }

    QtObject {
        id: waveAnim
        property var amplitudes: [0.5, 0.5, 0.5, 0.5, 0.5]
    }
}
