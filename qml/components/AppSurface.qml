import QtQuick
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Rectangle {
    id: root

    SmoothUI.theme: Theme.of(root)

    property int elevation: 2
    property real surfaceRadius: PrintWorkspaceStyle.dialogRadius
    property real shadowOpacity: 0.16

    radius: surfaceRadius
    color: PrintWorkspaceStyle.surface(root.SmoothUI.dark)
    border.width: 1
    border.color: PrintWorkspaceStyle.border(root.SmoothUI.dark)

    Shadow {
        radius: root.radius
        elevation: root.elevation
        color: PrintWorkspaceStyle.shadow(root.SmoothUI.dark,
                                          root.shadowOpacity)
    }
}
