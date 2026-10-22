import QtQuick
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Item {
    id: root

    SmoothUI.theme: Theme.of(root)

    property alias color: surface.color
    property alias border: surface.border
    property alias radius: surface.radius
    property alias elevation: surface.elevation
    property alias shadowOpacity: surface.shadowOpacity
    property real surfaceRadius: Theme.panelRadius

    radius: surfaceRadius

    Frame {
        id: surface
        anchors.fill: parent
        z: -1
        padding: 0
        enabled: false
        SmoothUI.dark: root.SmoothUI.dark
        SmoothUI.primaryColor: root.SmoothUI.primaryColor
        elevation: 2
    }
}
