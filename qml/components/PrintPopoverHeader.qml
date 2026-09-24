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
    property bool showBack: false
    property bool showIcon: false
    property var iconName: FluentIcons.graph_Info
    property real cornerRadius: PrintWorkspaceStyle.menuRadius

    signal backRequested()
    signal closeRequested()

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
            font: Typography.bodyStrong
            color: root.SmoothUI.theme.res.textFillColorPrimary
            elide: Text.ElideRight
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
