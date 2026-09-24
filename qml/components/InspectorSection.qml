import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

ColumnLayout {
    id: root

    property string title: ""
    default property alias content: sectionContent.data

    SmoothUI.theme: Theme.of(root)

    Layout.fillWidth: true
    spacing: 0

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 36
        color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)

        Label {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            verticalAlignment: Text.AlignVCenter
            text: root.title
            font: Typography.bodyStrong
            color: root.SmoothUI.theme.res.textFillColorSecondary
        }
    }

    ColumnLayout {
        id: sectionContent
        Layout.fillWidth: true
        spacing: 0
    }
}
