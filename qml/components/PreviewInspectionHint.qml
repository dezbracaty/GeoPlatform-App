import QtQuick
import SmoothUI
import SmoothUI.Controls
import GPlatform

Item {
    id: root
    property bool compact: false
    readonly property string modifierName: Qt.platform.os === "osx" ? "⌥ Option" : "Alt"
    readonly property string fullHint: qsTr("Hold %1 to highlight paths and inspect G-code").arg(modifierName)
    implicitWidth: Math.max(hint.implicitWidth, cursorSnap.implicitWidth)
    implicitHeight: 24

    Label {
        id: hint
        objectName: "preview.inspection.hint"
        anchors.fill: parent
        visible: !SlicingPreviewBridge.previewInspectModifierPressed
        text: root.compact ? qsTr("Hold %1 to inspect paths").arg(root.modifierName) : root.fullHint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font: Typography.caption
        color: Theme.of(root).res.textFillColorTertiary
        ToolTip {
            text: root.fullHint
            visible: hintHover.hovered && hint.truncated
            delay: Theme.tooltipDelay
        }
        HoverHandler { id: hintHover }
    }

    CheckBox {
        id: cursorSnap
        objectName: "preview.inspection.cursorSnap"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        visible: SlicingPreviewBridge.previewInspectModifierPressed
        text: qsTr("Cursor snapping")
        checked: SlicingPreviewBridge.previewCursorSnapEnabled
        onToggled: SlicingPreviewBridge.previewCursorSnapEnabled = checked
        padding: 2
        leftPadding: 0
        font: Typography.caption
    }
}
