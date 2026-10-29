import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Qt.labs.platform as Platform
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0
import Render 1.0
import "../../components"

Item {
    id: centralFragment
    objectName: "normalWorkspaceCentralPart"
    anchors.fill: parent

    // 通过父级访问 ThreadRendererQmlItem
    property var threadRenderer: parent && parent.parent ? parent.parent.renderer : null
    readonly property bool renderActive: threadRenderer
                                         ? threadRenderer.renderActive
                                         : false
    onRenderActiveChanged: {
        if (!renderActive && !(Global.mainScreen && Global.mainScreen.userGuide.switchingWorkspace)) {
            const guide = Global.mainScreen ? Global.mainScreen.userGuide : null
            const session = guide ? guide.saved : null
            // Restore after the external activatePage call finishes, not inside its binding update.
            Qt.callLater(function() {
                if (guide && guide.saved === session && !centralFragment.renderActive)
                    centralFragment.closeGuide()
            })
        }
        if (!renderActive && FiberMaskBridge.active)
            FiberMaskBridge.cancel()
        if (!renderActive && CutToolBridge.active)
            CutToolBridge.cancel()
    }
    clip: true

    // 暴露右侧切换按钮的引用，让外部可以访问
    // (moved to line 435 for compatibility)

    DBPropertyBridge {
        id: dbBridge
    }

    property string selectedTool: "select"
    property bool gridVisible: true  // Default to true for print bed grid
    property bool snapEnabled: true
    readonly property int objectCount: dbBridge.meshCount
    property string currentMode: qsTr("3D Printing")
    property real modelZoom: 100
    property bool showBedBounds: true

    // Print status properties
    property bool isPrinting: false
    property real printProgress: 0.0  // 0-100
    property string printTime: "00:00"
    property string estimatedTime: "00:00"

    // 登录状态属性
    property bool isLoggedIn: false
    property string userName: ""

    // Selected model for properties display
    property var selectedModel: SelectionBridge.selectedModel
    property bool hasSelection: SelectionBridge.hasSelection
    readonly property var configurationPanel: Global.mainScreen && Global.mainScreen.currentPrintHost() ? Global.mainScreen.currentPrintHost().configurationPanel : null
    readonly property bool propertiesExpanded: configurationPanel && configurationPanel.shown && configurationPanel.page === 1
    property bool rotationWidgetActive: false  // 跟踪旋转widget是否真正激活
    property bool cellHighlightActive: false  // 跟踪面片高亮功能是否激活

    readonly property Item guideViewport: threadRenderer
    readonly property Item guideOverlay: Overlay.overlay
    readonly property var guideGroups: leftPrintToolbar.guideGroups
    readonly property Item guideLeftToolbar: leftPrintToolbar
    readonly property Item guideRightToolbar: Global.mainScreen ? Global.mainScreen.viewMenuAnchor : null
    readonly property var sceneSettingsPopup: Global.mainScreen && Global.mainScreen.currentPrintHost() ? Global.mainScreen.currentPrintHost().sceneSettingsPopup : null

    function startGuide() {
        if (renderActive && Global.mainScreen)
            Global.mainScreen.startUserGuide(centralFragment)
    }
    function closeGuide() {
        if (Global.mainScreen && Global.mainScreen.userGuide.sourceWorkspace === centralFragment)
            Global.mainScreen.userGuide.close("interrupted")
    }
    function guideState() {
        return { inspector: propertiesExpanded, scene: sceneSettingsPopup ? sceneSettingsPopup.visible : false,
                 configuration: configurationPanel ? {shown: configurationPanel.shown,
                     collapsed: configurationPanel.collapsed, page: configurationPanel.page} : null,
                 selection: SelectionBridge.selectedId }
    }
    function guideSummary() {
        return { inspector: propertiesExpanded, scene: sceneSettingsPopup ? sceneSettingsPopup.visible : false,
                 selection: SelectionBridge.selectedId, selectionCount: SelectionBridge.selectionCount,
                 modelCount: objectCount, rendererRole: threadRenderer ? threadRenderer.rendererRole : -1,
                 camera: [CameraBridge.cameraX, CameraBridge.cameraY, CameraBridge.cameraZ,
                          CameraBridge.focalX, CameraBridge.focalY, CameraBridge.focalZ],
                 fiberEditing: FiberMaskBridge.active, cutEditing: CutToolBridge.active }
    }
    function restoreGuideState(state) {
        if (SelectionBridge.selectedId === state.selection && configurationPanel && state.configuration) {
            configurationPanel.page = state.configuration.page
            configurationPanel.collapsed = state.configuration.collapsed
            configurationPanel.shown = state.configuration.shown
        }
        if (sceneSettingsPopup && !state.scene && sceneSettingsPopup.visible)
            sceneSettingsPopup.close()
        else if (sceneSettingsPopup && state.scene && !sceneSettingsPopup.visible)
            sceneSettingsPopup.open()
    }
    Component.onDestruction: closeGuide()
    Connections {
        target: centralFragment.Window
        function onWindowChanged() { centralFragment.closeGuide() }
    }

    function openSceneSettingsPanel() {
        if (Global.mainScreen && Global.mainScreen.currentPrintHost())
            Global.mainScreen.currentPrintHost().openSceneSettings()
    }

    CameraViewAnimation {
        id: cameraAnimator
        active: threadRenderer ? threadRenderer.renderActive : false
        viewport: threadRenderer
    }

    // The Qt overlay hit-tests locally and delegates transitions to QML.
    OrcaViewNavigator {
        objectName: "print.viewport.normal.navigator"
        anchors.left: leftPrintToolbar.right
        anchors.leftMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16 + PrintWorkspaceStyle.globalCommandBarHeight + 12
        z: 20
        visible: threadRenderer ? threadRenderer.renderActive : false
        viewDirection: Qt.vector3d(CameraBridge.cameraX - CameraBridge.focalX,
                                   CameraBridge.cameraY - CameraBridge.focalY,
                                   CameraBridge.cameraZ - CameraBridge.focalZ)
        viewUp: Qt.vector3d(CameraBridge.upX, CameraBridge.upY, CameraBridge.upZ)
        onDirectionActivated: function(direction) { cameraAnimator.animateToDirection(direction) }
    }

    FPSDisplay {
        id: fpsDisplay
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        z: 100
        fpsMonitor: threadRenderer ? threadRenderer.fpsMonitor : null
    }

    // Left Print Toolbar
    LeftPrintToolbar {
        id: leftPrintToolbar
        objectName: "print.toolbar.left"
        x: 16
        y: Math.max(70, (parent.height - height) / 2)
        z: 10

        // 传递 gridVisible 和 showBedBounds 属性
        gridVisible: centralFragment.gridVisible
        showBedBounds: centralFragment.showBedBounds

    }

    RowLayout {
        anchors.top: parent.top
        anchors.topMargin: 64
        anchors.right: parent.right
        anchors.rightMargin: PrintWorkspaceStyle.toolbarWidth + 28
        spacing: PrintWorkspaceStyle.space8
        visible: centralFragment.width >= 760
                 && x >= leftPrintToolbar.x + leftPrintToolbar.width + PrintWorkspaceStyle.space12
        z: 20

        Button {
            objectName: "print.viewport.fitScene"
            text: qsTr("Fit Scene")
            icon.name: FluentIcons.graph_Zoom
            enabled: centralFragment.objectCount > 0
            onClicked: ActionManager.triggerAction("camera.fitScene", {})
        }
        Button {
            objectName: "print.viewport.viewPlatform"
            text: qsTr("View Platform")
            icon.name: FluentIcons.graph_GridView
            onClicked: ActionManager.triggerAction("camera.resetDefault", {})
        }
    }

    FloatingSettingsPanel {
        id: modelColorPaintPopup
        objectName: "modelColorPaintPopup"
        viewportItem: centralFragment
        title: qsTr("Paint Model")
        panelWidth: 400
        panelHeight: 680
        integratedTitleBar: false
        lazyContent: true
        closePolicy: Popup.NoAutoClose

        panelContent: Component {
            ModelColorPaintPanel {}
        }

        onClosed: {
            if (ModelColorPaintBridge.active)
                ModelColorPaintBridge.leavePainting()
        }
    }

    FloatingSettingsPanel {
        id: cutToolPopup
        objectName: "cutToolPopup"
        viewportItem: centralFragment
        title: qsTr("Cut Model")
        panelWidth: 430
        panelHeight: 590
        integratedTitleBar: false
        lazyContent: true
        closePolicy: Popup.NoAutoClose

        panelContent: Component {
            CutToolPanel {}
        }

        onClosed: {
            if (CutToolBridge.active)
                CutToolBridge.cancel()
        }
    }

    PanelSurface {
        id: fiberMaskSidebar
        objectName: "fiberMaskSidebar"
        anchors.top: parent.top
        anchors.topMargin: 132
        anchors.right: parent.right
        anchors.rightMargin: 16
        width: Math.max(0, Math.min(430, centralFragment.width - 100))
        // The global command bar belongs to the Dock host and sits above this
        // entire viewport. Reserve its footprint instead of extending beneath it.
        readonly property int viewportBottomInset: PrintWorkspaceStyle.space16
                                                   + PrintWorkspaceStyle.globalCommandBarHeight
                                                   + PrintWorkspaceStyle.space12
        height: Math.max(0, centralFragment.height - anchors.topMargin - viewportBottomInset)
        visible: centralFragment.renderActive && FiberMaskBridge.active
        z: 100
        clip: true
        Loader {
            anchors.fill: parent
            active: FiberMaskBridge.active
            sourceComponent: Component { FiberMaskPanel {} }
        }
    }

    Connections {
        target: ModelColorPaintBridge
        function onActiveChanged() {
            if (ModelColorPaintBridge.active)
                modelColorPaintPopup.open()
            else
                modelColorPaintPopup.close()
        }
    }

    Connections {
        target: CutToolBridge
        function onActiveChanged() {
            if (CutToolBridge.active)
                cutToolPopup.open()
            else
                cutToolPopup.close()
        }
    }

    Component.onCompleted: {
        if (ModelColorPaintBridge.active)
            modelColorPaintPopup.open()
        if (CutToolBridge.active)
            cutToolPopup.open()
    }

    // 兼容性属性（保持向后兼容）
// Left Print Toolbar - 使用 HoverExpandableButton 分组

    // Functions

    function createPrintObject(objectType) {

        // Create object on print bed surface (z = 0)
        let params = {
            "x": 0,
            "y": 0,
            "z": 0  // On print bed surface
        };

        // Add specific parameters for each object type
        switch (objectType) {
        case "cube":
            params.width = 20.0;  // 20mm cube
            params.height = 20.0;
            params.depth = 20.0;
            ActionManager.triggerAction("geometry.create.cube", params);
            break;
        case "sphere":
            params.radius = 10.0;  // 10mm radius sphere
            ActionManager.triggerAction("geometry.create.sphere", params);
            break;
        case "cylinder":
            params.radius = 8.0;   // 8mm radius cylinder
            params.height = 15.0;  // 15mm height
            ActionManager.triggerAction("geometry.create.cylinder", params);
            break;
        }

    }


    // 快速操作菜单组件 - 使用 SmoothUI.QuickActionMenu 支持多级菜单和搜索过滤
    // 直接使用 TMenuGroup.items，与 MenuBar 统一数据源
    QuickActionMenu {
        id: quickActionMenu

        // 当前菜单组 - 由 QuickActionController.getMenuGroupForContext() 返回
        property var currentMenuGroup: null

        // menuItems 直接使用 TMenuGroup.items（TMenuItem 列表）
        menuItems: currentMenuGroup ? currentMenuGroup.items : []

        // 搜索配置
        searchEnabled: true
        searchPlaceholder: qsTr("Search menu...")
        minItemsForSearch: 3  // 菜单项 >= 3 时显示搜索框
        autoFocusSearch: true

        // 刷新菜单数据 - 获取当前上下文对应的 TMenuGroup
        function refreshMenuItems() {
            var contextType = QuickActionController.getCurrentContext() || "NoSelection"
            currentMenuGroup = QuickActionController.getMenuGroupForContext(contextType)
            // console.log("📋 刷新菜单项，上下文:", contextType,
            //             "菜单组:", currentMenuGroup ? "已获取" : "未找到")
        }
    }

    // 连接QuickActionController信号
    Connections {
        target: QuickActionController

        function onShowQuickActionMenu(x, y, initialText) {
            // console.log("显示快速操作菜单在位置:", x, y)

            // 先刷新菜单数据
            quickActionMenu.refreshMenuItems()

            // 设置弹出位置，并把触发字符作为初始搜索文本原子化地打开。
            quickActionMenu.x = x
            quickActionMenu.y = y
            quickActionMenu.openWithQuery(initialText)
        }
    }

    // 连接QuickActionController信号以响应上下文变化
    Connections {
        target: QuickActionController

        function onContextChanged() {
            // console.log("📋 菜单上下文已更改:", QuickActionController.currentContext)
            // 如果菜单已打开，刷新菜单项
            if (quickActionMenu.visible) {
                quickActionMenu.refreshMenuItems()
            }
        }
    }

}
