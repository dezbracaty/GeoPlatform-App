import QtQuick
import GPlatform

AppSurface {
    id: root

    property bool blocksPointerInput: true

    elevation: 2
    surfaceRadius: PrintWorkspaceStyle.dialogRadius
    shadowOpacity: elevation >= 8 ? 0.30 : 0.16

    // PanelSurface represents an interactive surface layered over the 3D
    // viewport. Visual items do not consume input by themselves, so cover the
    // surface with a receiver behind the panel's actual controls.
    MouseArea {
        anchors.fill: parent
        enabled: root.blocksPointerInput
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        hoverEnabled: true
        cursorShape: Qt.ArrowCursor

        onPressed: function(mouse) { mouse.accepted = true }
        onReleased: function(mouse) { mouse.accepted = true }
        onClicked: function(mouse) { mouse.accepted = true }
        onDoubleClicked: function(mouse) { mouse.accepted = true }
        onPositionChanged: function(mouse) { mouse.accepted = true }
        onWheel: function(wheel) { wheel.accepted = true }
    }
}
