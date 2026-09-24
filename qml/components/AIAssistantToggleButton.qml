import QtQuick
import SmoothUI
import SmoothUI.Controls
import GPlatform 1.0

Item {
    id: root
    implicitWidth: 38
    implicitHeight: 38

    property bool opened: AIChatBridge.panelVisible
    property bool streaming: AIChatBridge.streaming

    signal clicked()

    readonly property color accentColor: Theme.accentColor.defaultBrushFor()

    Rectangle {
        id: pulseRing
        anchors.centerIn: assistantButton
        width: assistantButton.width + 6
        height: assistantButton.height + 6
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: root.accentColor
        opacity: 0
        visible: root.streaming
    }

    IconButton {
        id: assistantButton
        objectName: "aiAssistantToggleButton"
        anchors.centerIn: parent
        implicitWidth: 34
        implicitHeight: 34
        flat: true
        focusPolicy: Qt.StrongFocus
        scale: down ? 0.94 : 1

        background: Rectangle {
            radius: width / 2
            color: root.opened
                   ? root.accentColor
                   : (assistantButton.hovered
                      ? Theme.accentColor.tertiaryBrushFor()
                      : Theme.res.subtleFillColorSecondary)
            border.width: 1
            border.color: root.opened || assistantButton.hovered
                          ? root.accentColor
                          : Theme.res.cardStrokeColorDefaultSolid

            Behavior on color {
                ColorAnimation { duration: 160 }
            }
            Behavior on border.color {
                ColorAnimation { duration: 160 }
            }
        }

        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: -1
                anchors.verticalCenterOffset: 1
                width: 17
                height: 17
                source: FluentIcons.graph_ChatBubbles
                color: root.opened ? "#FFFFFF" : root.accentColor

                Behavior on color {
                    ColorAnimation { duration: 160 }
                }
            }

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.horizontalCenterOffset: 7
                anchors.verticalCenterOffset: -7
                width: 8
                height: 8
                source: FluentIcons.graph_FavoriteStarFill
                color: root.opened ? "#FFFFFF" : root.accentColor

                SequentialAnimation on scale {
                    running: root.streaming
                    loops: Animation.Infinite
                    NumberAnimation {
                        from: 0.8
                        to: 1.15
                        duration: 480
                        easing.type: Easing.InOutCubic
                    }
                    NumberAnimation {
                        from: 1.15
                        to: 0.8
                        duration: 480
                        easing.type: Easing.InOutCubic
                    }
                }
            }
        }

        onClicked: root.clicked()

        ToolTip.visible: hovered
        ToolTip.delay: Theme.tooltipDelay
        ToolTip.text: root.streaming
                      ? qsTr("AI is responding…")
                      : (root.opened ? qsTr("Collapse AI Assistant")
                                     : qsTr("Open AI Assistant (Alt+I)"))

        Behavior on scale {
            NumberAnimation {
                duration: 120
                easing.type: Easing.OutCubic
            }
        }
    }

    Rectangle {
        width: 7
        height: 7
        radius: width / 2
        color: root.streaming ? "#00B77A" : root.accentColor
        border.width: 1
        border.color: "#FFFFFF"
        anchors.right: assistantButton.right
        anchors.bottom: assistantButton.bottom
        anchors.rightMargin: -1
        anchors.bottomMargin: -1
        visible: root.opened || root.streaming
    }

    SequentialAnimation {
        running: root.streaming
        loops: Animation.Infinite

        ParallelAnimation {
            NumberAnimation {
                target: pulseRing
                property: "scale"
                from: 0.92
                to: 1.24
                duration: 980
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: pulseRing
                property: "opacity"
                from: 0.38
                to: 0
                duration: 980
                easing.type: Easing.OutCubic
            }
        }
    }
}
