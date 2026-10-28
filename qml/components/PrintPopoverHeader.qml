import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Rectangle {
    id: root

    SmoothUI.theme: Theme.of(root)

    required property string title
    property bool showExpand: false
    property bool expanded: false
    signal expandRequested
    property bool showBack: false
    property bool showIcon: false
    property var iconName: FluentIcons.graph_Info
    property real cornerRadius: PrintWorkspaceStyle.menuRadius

    signal backRequested
    signal closeRequested

    implicitHeight: PrintWorkspaceStyle.titleBarHeight
    topLeftRadius: root.cornerRadius
    topRightRadius: root.cornerRadius
    color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.SmoothUI.theme.res.dividerStrokeColorDefault
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.showBack ? 8 : 16
        anchors.rightMargin: 8
        spacing: 8

        IconButton {
            visible: root.showBack
            implicitWidth: PrintWorkspaceStyle.closeButtonSize
            implicitHeight: PrintWorkspaceStyle.closeButtonSize
            radius: PrintWorkspaceStyle.controlRadius
            flat: true
            icon.name: FluentIcons.graph_ChevronLeft
            icon.width: 14
            icon.height: 14
            onClicked: root.backRequested()

            ToolTip {
                visible: parent.hovered
                text: qsTr("Back to Print Configuration")
            }
        }

        Icon {
            visible: root.showIcon
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            source: root.iconName
            color: root.SmoothUI.theme.res.textFillColorPrimary
        }

        Label {
            Layout.fillWidth: true
            text: root.title
            font: Typography.subtitle
            color: root.SmoothUI.theme.res.textFillColorPrimary
            elide: Text.ElideRight
        }

        IconButton {
            objectName: root.showExpand ? "printerPopupExpandButton" : ""
            visible: root.showExpand
            implicitWidth: PrintWorkspaceStyle.closeButtonSize
            implicitHeight: PrintWorkspaceStyle.closeButtonSize
            flat: true
            padding: 7
            leftPadding: 7
            rightPadding: 7
            contentItem: Canvas {
                property color stroke: root.SmoothUI.theme.res.textFillColorPrimary
                property bool expanded: root.expanded
                onStrokeChanged: requestPaint()
                onExpandedChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    ctx.scale(width / 18, height / 18);
                    ctx.strokeStyle = stroke;
                    ctx.lineWidth = 1.3;
                    if (expanded) {
                        ctx.strokeRect(3, 6, 9, 9);
                        ctx.beginPath();
                        ctx.moveTo(6, 4);
                        ctx.lineTo(6, 2);
                        ctx.lineTo(15, 2);
                        ctx.lineTo(15, 11);
                        ctx.lineTo(13, 11);
                        ctx.stroke();
                    } else {
                        ctx.beginPath();
                        ctx.moveTo(2, 7);
                        ctx.lineTo(2, 2);
                        ctx.lineTo(7, 2);
                        ctx.moveTo(11, 2);
                        ctx.lineTo(16, 2);
                        ctx.lineTo(16, 7);
                        ctx.moveTo(16, 11);
                        ctx.lineTo(16, 16);
                        ctx.lineTo(11, 16);
                        ctx.moveTo(7, 16);
                        ctx.lineTo(2, 16);
                        ctx.lineTo(2, 11);
                        ctx.stroke();
                    }
                }
            }
            Accessible.name: root.expanded ? qsTr("Restore") : qsTr("Expand")
            onClicked: root.expandRequested()
            ToolTip {
                visible: parent.hovered
                text: parent.Accessible.name
            }
        }
        IconButton {
            implicitWidth: PrintWorkspaceStyle.closeButtonSize
            implicitHeight: PrintWorkspaceStyle.closeButtonSize
            radius: PrintWorkspaceStyle.controlRadius
            flat: true
            icon.name: FluentIcons.graph_ChromeClose
            icon.width: 14
            icon.height: 14
            onClicked: root.closeRequested()

            ToolTip {
                visible: parent.hovered
                text: qsTr("Close")
            }
        }
    }
}
