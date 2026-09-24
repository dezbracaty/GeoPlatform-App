import QtQuick
import GPlatform

AppPopup {
    id: root

    property alias panelContent: contentLoader.sourceComponent
    property int panelWidth: 320
    property int panelHeight: 480
    property bool integratedTitleBar: false
    property bool lazyContent: false
    property var viewportItem: null
    property var triggerButton: null

    readonly property int viewportTopInset: viewportItem
                                               ? 132
                                               : PrintWorkspaceStyle.space16
    readonly property int viewportRightInset: viewportItem
                                                 ? PrintWorkspaceStyle.space16
                                                   + PrintWorkspaceStyle.toolbarWidth
                                                   + PrintWorkspaceStyle.space12
                                                 : PrintWorkspaceStyle.space16
    readonly property int viewportBottomInset: viewportItem
                                                  ? PrintWorkspaceStyle.space16
                                                    + PrintWorkspaceStyle.commandBarHeight
                                                    + PrintWorkspaceStyle.space16
                                                  : PrintWorkspaceStyle.space16
    readonly property int viewportLeftInset: PrintWorkspaceStyle.space16
    readonly property int availSpaceWidth: parent
                                           ? Math.max(0, parent.width
                                                      - viewportLeftInset
                                                      - viewportRightInset)
                                           : panelWidth
    readonly property int availSpaceHeight: parent
                                            ? Math.max(0, parent.height
                                                       - viewportTopInset
                                                       - viewportBottomInset)
                                            : panelHeight
    readonly property int contentPreferredWidth: {
        var itemObject = contentLoader.item
        if (itemObject && itemObject.preferredWidth !== undefined)
            return Math.max(panelWidth, itemObject.preferredWidth)
        return panelWidth
    }

    width: Math.min(availSpaceWidth, contentPreferredWidth)
    height: Math.min(availSpaceHeight, panelHeight)
    showHeader: !integratedTitleBar
    surfaceElevation: 2
    surfaceShadowOpacity: 0.16

    parent: viewportItem
    z: 200
    x: parent ? Math.max(viewportLeftInset,
                         parent.width - width - viewportRightInset)
              : viewportLeftInset
    y: viewportTopInset

    onOpened: root.forceActiveFocus()

    Loader {
        id: contentLoader
        anchors.fill: parent
        clip: true
        active: !root.lazyContent || root.visible

        onLoaded: {
            if (root.integratedTitleBar && item
                    && item.closeRequested !== undefined) {
                item.closeRequested.connect(root.close)
            }
        }
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0.0
            to: 1.0
            duration: 150
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1.0
            to: 0.0
            duration: 100
            easing.type: Easing.InCubic
        }
    }
}
