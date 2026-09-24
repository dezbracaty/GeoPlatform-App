import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Item {
    id: root

    property string label: ""
    default property alias content: valueLayout.data

    SmoothUI.theme: Theme.of(root)

    Layout.fillWidth: true
    Layout.minimumHeight: 44
    Layout.preferredHeight: Math.max(44, valueLayout.implicitHeight + 8)
    opacity: enabled ? 1.0 : 0.5

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        Label {
            Layout.preferredWidth: 132
            Layout.minimumWidth: 132
            Layout.maximumWidth: 132
            text: root.label
            font: Typography.body
            color: root.SmoothUI.theme.res.textFillColorPrimary
            elide: Text.ElideRight
        }

        RowLayout {
            id: valueLayout
            Layout.preferredWidth: 224
            Layout.minimumWidth: 224
            Layout.maximumWidth: 224
            spacing: 8

            Item { Layout.fillWidth: true }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        height: 1
        color: PrintWorkspaceStyle.divider(root.SmoothUI.dark)
    }
}
