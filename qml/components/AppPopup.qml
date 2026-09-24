import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Popup {
    id: control

    default property alias bodyData: body.data
    property alias footerData: footer.data

    SmoothUI.theme: Theme.of(control)
    SmoothUI.radius: PrintWorkspaceStyle.menuRadius

    property Item popupAnchor: null
    property Item popupVerticalAnchor: null
    property bool centerHorizontally: popupAnchor === null
    property bool popupAbove: false
    property bool parentRelativeVerticalAnchor: false
    property int positionRevision: 0
    property real preferredWidth: 0
    property real preferredHeight: 0
    property string title: ""
    property bool showBack: false
    property bool showHeader: true
    property int surfaceElevation: 4
    property real surfaceShadowOpacity: 0.22
    property bool windowEscapeEnabled: false
    property bool windowEscapeOnlyOutsideFocus: false

    signal backRequested()

    function reposition() {
        ++positionRevision
    }

    function containsActiveFocus() {
        var windowItem = control.popupAnchor ? control.popupAnchor : control.parent
        var window = windowItem ? windowItem.Window.window : null
        var item = window ? window.activeFocusItem : null
        while (item) {
            if (item === control.contentItem)
                return true
            item = item.parent
        }
        return false
    }

    width: Math.min(preferredWidth,
                    parent ? parent.width - PrintWorkspaceStyle.space16 * 2
                           : preferredWidth)
    height: Math.min(preferredHeight,
                     parent ? parent.height - PrintWorkspaceStyle.space16 * 2
                            : preferredHeight)
    x: {
        // mapToItem() does not make changes in the mapped item's ancestor
        // geometry observable to this binding. positionRevision lets a popup
        // request one fresh calculation after an asynchronous layout pass.
        var revision = positionRevision
        if (!parent)
            return 0
        var parentPosition = parent.mapToItem(null, 0, 0)
        if (centerHorizontally || !popupAnchor)
            return Math.round(parentPosition.x + (parent.width - width) / 2)
        var position = popupAnchor.mapToItem(null, 0, 0)
        var preferredX = position.x + (popupAnchor.width - width) / 2
        return Math.round(Math.max(parentPosition.x + PrintWorkspaceStyle.space16,
                                   Math.min(preferredX,
                                            parentPosition.x + parent.width - width
                                            - PrintWorkspaceStyle.space16)))
    }
    y: {
        if (!parent)
            return 0
        var parentPosition = parentRelativeVerticalAnchor
                ? Qt.point(0, 0) : parent.mapToItem(null, 0, 0)
        if (!popupAnchor && !popupVerticalAnchor)
            return Math.round(parentPosition.y + (parent.height - height) / 2)
        var verticalAnchor = popupVerticalAnchor ? popupVerticalAnchor : popupAnchor
        var coordinateItem = parentRelativeVerticalAnchor ? parent : null
        var position = popupAbove
                ? verticalAnchor.mapToItem(
                      coordinateItem, 0,
                      -height - PrintWorkspaceStyle.machinePopoverOffset)
                : verticalAnchor.mapToItem(
                      coordinateItem, 0,
                      verticalAnchor.height + PrintWorkspaceStyle.machinePopoverOffset)
        return Math.round(Math.max(parentPosition.y + PrintWorkspaceStyle.space16,
                                   Math.min(position.y,
                                            parentPosition.y + parent.height - height
                                            - PrintWorkspaceStyle.space16)))
    }
    padding: 0
    modal: false
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: control.opened
                 && control.windowEscapeEnabled
                 && (!control.windowEscapeOnlyOutsideFocus
                     || !control.containsActiveFocus())
        onActivated: control.close()
        onActivatedAmbiguously: control.close()
    }

    background: AppSurface {
        surfaceRadius: control.SmoothUI.radius
        elevation: control.surfaceElevation
        shadowOpacity: control.surfaceShadowOpacity

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            preventStealing: true
            onPressed: function(mouse) {
                control.contentItem.forceActiveFocus(Qt.MouseFocusReason)
                mouse.accepted = true
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0
        focus: true

        Keys.priority: Keys.BeforeItem
        Keys.onEscapePressed: function(event) {
            control.close()
            event.accepted = true
        }

        PrintPopoverHeader {
            visible: control.showHeader
            Layout.fillWidth: true
            Layout.preferredHeight: control.showHeader
                                    ? PrintWorkspaceStyle.titleBarHeight : 0
            title: control.title
            showBack: control.showBack
            cornerRadius: control.SmoothUI.radius
            onBackRequested: control.backRequested()
            onCloseRequested: control.close()
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
        }

        Item {
            id: footer
            visible: children.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible
                                    ? PrintWorkspaceStyle.commandBarHeight : 0
        }
    }

}
