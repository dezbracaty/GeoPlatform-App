import QtQuick
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
    property bool propertiesExpanded: false
    property bool inspectorAutoExpandSuppressed: false
    property bool rotationWidgetActive: false  // 跟踪旋转widget是否真正激活
    property bool cellHighlightActive: false  // 跟踪面片高亮功能是否激活

    function openSceneSettingsPanel() {
        sceneSettingsPopup.triggerButton = rightPrintToolbar
        sceneSettingsPopup.open()
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
        x: 16
        y: Math.max(70, (parent.height - height) / 2)
        z: 10

        // 传递 gridVisible 和 showBedBounds 属性
        gridVisible: centralFragment.gridVisible
        showBedBounds: centralFragment.showBedBounds

    }

    // Right Print Toolbar
    RightPrintToolbar {
        id: rightPrintToolbar
        x: parent.width - width - 16
        y: Math.max(70, (parent.height - height) / 2)
        z: 10

        // 连接打开设置面板信号
        onOpenSettingsPanel: function(panelType, triggerButton) {
            if (panelType !== "scene") {
                console.warn("[SettingsPanel] 未知面板类型:", panelType)
                return
            }
            sceneSettingsPopup.triggerButton = triggerButton
            sceneSettingsPopup.open()
        }
    }

    ModelInspectorPanel {
        id: modelInspector
        objectName: "modelInspectorPanel"
        anchors.top: parent.top
        anchors.topMargin: 132
        anchors.right: rightPrintToolbar.left
        anchors.rightMargin: 12
        z: 50
        expanded: centralFragment.propertiesExpanded
        maximumHeight: Math.max(240, centralFragment.height - 220)

        onExpandRequested: {
            if (!ModelPropertyBridge.hasSelectedModel)
                return
            centralFragment.inspectorAutoExpandSuppressed = false
            centralFragment.propertiesExpanded = true
        }

        onCollapseRequested: {
            centralFragment.propertiesExpanded = false
            centralFragment.inspectorAutoExpandSuppressed = true
        }
    }

    FloatingSettingsPanel {
        id: sceneSettingsPopup
        viewportItem: centralFragment
        title: qsTr("Scene Settings")
        panelWidth: 400
        panelHeight: 560
        integratedTitleBar: false

        panelContent: Component {
            SceneSettingsPanel {}
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

    Connections {
        target: ModelPropertyBridge
        function onHasSelectedModelChanged() {
            if (!ModelPropertyBridge.hasSelectedModel) {
                centralFragment.propertiesExpanded = false
            } else if (!centralFragment.inspectorAutoExpandSuppressed) {
                centralFragment.propertiesExpanded = true
            }
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
        if (ModelPropertyBridge.hasSelectedModel
                && !centralFragment.inspectorAutoExpandSuppressed)
            centralFragment.propertiesExpanded = true
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
