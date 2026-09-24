import QtQuick
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl

Item {
    id: root

    property real markSize: 36
    property string mainIcon: FluentIcons.graph_Contact
    property string badgeIcon: FluentIcons.graph_Link

    readonly property color accentColor: root.SmoothUI.theme.accentColor.defaultBrushFor()

    implicitWidth: markSize
    implicitHeight: markSize
    SmoothUI.theme: Theme.of(root)

    Rectangle {
        anchors.fill: parent
        radius: Math.round(root.markSize * 0.28)
        color: Colors.withOpacity(root.accentColor,
                                  root.SmoothUI.dark ? 0.18 : 0.10)
        border.width: 1
        border.color: Colors.withOpacity(root.accentColor,
                                         root.SmoothUI.dark ? 0.34 : 0.24)
    }

    Icon {
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: -1
        anchors.verticalCenterOffset: 1
        width: Math.round(root.markSize * 0.48)
        height: width
        source: root.mainIcon
        color: root.accentColor
    }

    Icon {
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: Math.round(root.markSize * 0.19)
        anchors.verticalCenterOffset: -Math.round(root.markSize * 0.19)
        width: Math.round(root.markSize * 0.22)
        height: width
        source: root.badgeIcon
        color: root.accentColor
        visible: root.badgeIcon.length > 0
    }
}
