import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform
import "../components"
import "../dialogs"

Item {
    id: root
    property alias pageRouter: page_router
    property Item viewMenuAnchor: null
    property bool debugWindowOpened: false
    property alias userGuide: workspaceGuide
    property bool automaticGuidePending: false
    property string pendingStartupPage: ""
    readonly property bool startupReady: StartupState.ready

    function restoreStartupPage() {
        if (!startupReady || pendingStartupPage.length === 0)
            return
        const page = pendingStartupPage
        pendingStartupPage = ""
        page_router.go(page)
    }
    onStartupReadyChanged: restoreStartupPage()

    function currentPrintHost() {
        const routed = navigation_view.routerView.currentItem
        const page = routed ? (routed.singleton && routed.singletonItem
                               ? routed.singletonItem.item : (routed.item || routed)) : null
        return page && page.objectName === "printWorkspaceHost"
                && typeof page.rendererForPage === "function"
                && page.activeWorkspaceContent ? page : null
    }
    function startUserGuide(workspace) {
        return workspaceGuide.start(workspace, currentPrintHost())
    }
    function automaticGuideHost() {
        const window = root.Window.window
        const host = currentPrintHost()
        if (!window || !window.visible || !window.active || !root.visible
                || !host || !host.visible || !host.dockReady || host.activePageName !== "prepare")
            return null
        const workspace = host.activeWorkspaceContent
        const renderer = host.rendererForPage("prepare")
        const overlay = workspace ? workspace.guideOverlay : null
        if (!workspace || !workspace.renderActive || !renderer || !renderer.visible
                || !renderer.renderActive || !renderer.hasPresentedFrame
                || renderer.width <= 0 || renderer.height <= 0
                || workspace.guideViewport !== renderer || renderer.Window.window !== window
                || !overlay || overlay.Window.window !== window
                || overlay.width <= 0 || overlay.height <= 0)
            return null
        return host
    }
    function tryAutomaticGuide() {
        if (!StartupState.ready || StartupState.replayMode || automaticGuidePending || workspaceGuide.running || SettingsHelper.printGuideHandled
                || SettingsHelper.printGuideAutoAttempted || !automaticGuideHost())
            return
        automaticGuidePending = true
        // Recheck after dock/layout callbacks, before consuming the one process attempt.
        Qt.callLater(function() {
            automaticGuidePending = false
            const host = automaticGuideHost()
            if (!host || workspaceGuide.running || !SettingsHelper.claimPrintGuideAutoAttempt())
                return
            workspaceGuide.start(host.activeWorkspaceContent, host)
        })
    }
    Timer {
        interval: 200
        repeat: true
        running: StartupState.ready && !StartupState.replayMode && !SettingsHelper.printGuideHandled && !SettingsHelper.printGuideAutoAttempted
                 && !workspaceGuide.running
        onTriggered: root.tryAutomaticGuide()
    }
    UserGuide {
        id: workspaceGuide
        navigation: navigation_view
        aiPanel: aiAssistantPanel
        onSessionEnded: function(reason, shown) {
            if (shown && (reason === "complete" || reason === "dismissed"))
                SettingsHelper.markPrintGuideHandled()
        }
    }
    Connections {
        target: navigation_view.routerView
        function onCurrentItemChanged() { workspaceGuide.close("interrupted") }
    }

    Component.onCompleted: Global.mainScreen = root
    Component.onDestruction: {
        if (Global.mainScreen === root)
            Global.mainScreen = null
    }

    function openSliceSettings(params) {
        var page = currentPrintHost()
        if (!page || page.objectName !== "printWorkspaceHost"
                || typeof page.openSliceSettings !== "function") {
            console.error("Slice settings are unavailable on the current page")
            return false
        }
        return page.openSliceSettings(params || {})
    }

    function handleMenuAction(item) {
        if (!StartupState.ready) return
        if (!item || !item.actionCode)
            return
        var actionCode = String(item.actionCode)
        if (actionCode.indexOf("calibration.") === 0) {
            const host = currentPrintHost()
            if (!host || host.activePageName !== "prepare" || TaskStateNotifier.isBusy)
                return
            const modes = {"calibration.pa":3,"calibration.flow":5,"calibration.temperature":6,
                "calibration.volumetric":7,"calibration.vfa":8,"calibration.retraction":9,
                "calibration.frequency":10,"calibration.damping":11,"calibration.cornering":12,"calibration.tolerance":13}
            if (modes[actionCode] !== undefined) {
                calibrationLoader.active = true
                const mode = modes[actionCode]
                Qt.callLater(function() { CalibrationBridge.openParameters(mode) })
            }
            return
        }
        if (actionCode === "view.sceneSettings") {
            const host = currentPrintHost()
            if (host) host.openSceneSettings()
            return
        }
        if (actionCode === "ui.guide.start") {
            const host = currentPrintHost()
            if (host && host.activePageName === "prepare" && host.activeWorkspaceContent)
                root.startUserGuide(host.activeWorkspaceContent)
            return
        }
        if (actionCode === "help.about") {
            page_router.go("/about")
            return
        }
        if (actionCode === "model.import.choose") {
            Global.starter.chooseModel({})
            return
        }
        if (actionCode === "model.import.choose_stl") {
            Global.starter.chooseStl({})
            return
        }
        if (actionCode === "slice.settings") {
            root.openSliceSettings({})
            return
        }
        if (!ActionManager.triggerAction(actionCode))
            console.error("Menu action failed:", actionCode)
    }

    Connections {
        target: MenuData
        function onMenuActionDispatch(item) {
            root.handleMenuAction(item)
        }
    }

    property list<QtObject> originalItems: [
        PaneItem {
            key: "/dock"
            title: qsTr("3D Print")
            icon.name: FluentIcons.graph_Printer3D
        },
        PaneItem {
            key: "/model-center"
            title: qsTr("Model Center")
            icon.name: FluentIcons.graph_Package
        },
        PaneItem {
            key: "/"
            title: qsTr("Dashboard")
            icon.name: FluentIcons.graph_Home
        }
    ]

    property list<QtObject> originalFooterItems: [
        PaneItem {
            icon.name: FluentIcons.graph_Contact
            key: "/about"
            title: qsTr("About")
        },
        PaneItem {
            icon.name: FluentIcons.graph_Settings
            key: "/settings"
            title: qsTr("Settings")
        }
    ]

    PageRouter {
        id: page_router

        // Save current page when navigating
        onSendRouter: function(val) {
            if (StartupState.ready && SettingsHelper.getRememberLastPage() && val) {
                SettingsHelper.saveLastPage(val)
            }
        }

        routes: {
            "/dock": { url: "qrc:/qt/qml/GPlatform/qml/pages/3DPrintPage.qml", singleton: true },
            "/model-center": { url: "qrc:/qt/qml/GPlatform/qml/pages/ModelCenterPage.qml", singleton: true },
            "/": { url: "qrc:/qt/qml/GPlatform/qml/pages/DashboardPage.qml", singleton: true },
            "/about": "qrc:/qt/qml/GPlatform/qml/pages/AboutPage.qml",
            "/settings": "qrc:/qt/qml/GPlatform/qml/pages/SettingsPage.qml"
        }
    }

    Menu {
        id: item_menu
        property var item
        MenuItem {
            text: qsTr("Open in Separate Window")
            onClicked: {
                var url = page_router.toUrl(item_menu.item.key)
                var title = item_menu.item.title
                if (url) {
                    WindowRouter.go("/page", {url: url, title: title})
                }
            }
        }
        function showMenu(item) {
            item_menu.item = item
            item_menu.popup()
        }
    }

    NavigationView {
        id: navigation_view
        router: page_router
        anchors.fill: parent
        logo: "qrc:/qt/qml/GPlatform/res/images/logo.png"
        title: "GPlatform"
        items: originalItems
        footerItems: originalFooterItems
        displayMode: Global.displayMode
        appBarHeight: Qt.platform.os === "osx" ? 60 : (Qt.platform.os === "windows" ? 36 : 48)
        titleBarTopMargin: Qt.platform.os === "osx" ? 20 : 0
        sideBarWidth: 280
        framePadding: 0

        // MenuBar 通过 menuBar 属性传入，由 NavigationView 自动布局到标题后面
        menuBar: MenuBar {
            id: main_menu_bar
            height: 30
            groupName: "Menu"  // 使用 groupName 属性，菜单会自动从当前环境加载
            delegate: MenuBarItem {
                objectName: "mainMenu_" + (menu ? menu.title : "")
                function syncMenu() {
                    if (root.viewMenuAnchor === this) root.viewMenuAnchor = null
                    if (menu) menu.focus = true
                    if (menu && menu.title === "View") root.viewMenuAnchor = this
                }
                onMenuChanged: syncMenu()
                Component.onCompleted: syncMenu()
                Component.onDestruction: { if (root.viewMenuAnchor === this) root.viewMenuAnchor = null }
                enabled: !menu || menu.title !== "Calibration" || (CalibrationBridge.available
                    && !TaskStateNotifier.isBusy && root.currentPrintHost()
                    && root.currentPrintHost().activePageName === "prepare")
                text: {
                    var title = menu ? menu.title : ""
                    if (title === "File") return qsTr("File")
                    if (title === "Edit") return qsTr("Edit")
                    if (title === "Help") return qsTr("Help")
                    if (title === "View") return qsTr("View")
                    if (title === "Calibration") return qsTr("Calibration")
                    return title
                }
            }
        }

        autoSuggestBox: AutoSuggestBox {
            id: auto_suggest_search
            placeholderText: qsTr("Search models, settings...")
            items: []
            textRole: "title"
            trailing: RowLayout {
                IconButton {
                    implicitWidth: 30
                    implicitHeight: 20
                    icon.name: FluentIcons.graph_ChromeClose
                    icon.width: 10
                    icon.height: 10
                    visible: auto_suggest_search.text !== ""
                    onClicked: {
                        auto_suggest_search.clear()
                    }
                }
                IconButton {
                    implicitWidth: 30
                    implicitHeight: 20
                    icon.name: FluentIcons.graph_Search
                    enabled: false
                    icon.width: 14
                    icon.height: 14
                }
            }
            onTap: (item) => {
                if (item.key) {
                    page_router.go(item.key)
                }
            }
            Connections {
                target: navigation_view
                function onSourceItemsChanged(data) {
                    auto_suggest_search.items = data.filter((item) => { return item instanceof PaneItem })
                }
            }
        }

        onTap: (item) => {
            if (item.key) {
                page_router.go(item.key)
            }
        }

        onRightTap: (item) => {
            if (item.key) {
                item_menu.showMenu(item)
            }
        }

        Component.onCompleted: {
            AIAccountBridge.refreshAccount()

            if (TransactionDebugBridge.runtimeEnabled) {
                if (!root.debugWindowOpened) {
                    WindowRouter.go("/page", {
                        url: "qrc:/qt/qml/GPlatform/qml/pages/TransactionDebugPage.qml",
                        title: qsTr("Transaction Debug")
                    })
                    root.debugWindowOpened = true
                }
            }

            // Prepare supplies the startup frame; restore the saved route only
            // after that barrier, without overwriting it during initialization.
            pendingStartupPage = !StartupState.replayMode && SettingsHelper.getRememberLastPage()
                    ? (SettingsHelper.getLastPage() || "/dock") : "/dock"
            if (startupReady)
                restoreStartupPage()
            else
                page_router.go("/dock")
        }
    }

    AIAssistantPanel {
        id: aiAssistantPanel
    }
    Loader {
        id: calibrationLoader
        anchors.fill: parent
        active: false
        sourceComponent: Component {
            CalibrationPanel { printHost: root.currentPrintHost() }
        }
        onLoaded: {
            const host = root.currentPrintHost()
            if (host) host.calibrationPanel = item
        }
    }
    Connections {
        target: CalibrationBridge
        function onChanged() {
            const host = root.currentPrintHost()
            if (host && host.calibrationPanel !== calibrationLoader.item)
                host.calibrationPanel = calibrationLoader.item
        }
    }

    Shortcut {
        id: aiAssistantShortcut
        objectName: "aiAssistantShortcut"
        sequence: "Alt+I"
        enabled: StartupState.ready && !workspaceGuide.running
        context: Qt.ApplicationShortcut
        onActivated: {
            if (!StartupState.ready) return
            if (!ActionManager.triggerAction("ui.ai.toggle"))
                AIChatBridge.togglePanel()
        }
    }

    Shortcut {
        sequence: "Ctrl+O"
        enabled: StartupState.ready && !workspaceGuide.running
        context: Qt.ApplicationShortcut
        onActivated: Global.starter.chooseModel({})
    }

    Shortcut {
        sequence: "Ctrl+I"
        enabled: StartupState.ready && !workspaceGuide.running
        context: Qt.ApplicationShortcut
        onActivated: Global.starter.chooseStl({})
    }

    AccountCenterPopup {
        id: accountCenterPopup
    }

    Connections {
        target: AIAccountBridge

        function onAccountCenterRequested(section) {
            accountCenterPopup.openSection(section)
        }
    }

    Item {
        id: aiTopBarDock
        z: 1300
        height: 34
        width: aiTopBarRow.implicitWidth
        anchors.top: navigation_view.top
        anchors.right: navigation_view.right
        anchors.rightMargin: 16
        anchors.topMargin: navigation_view.titleBarTopMargin
                           + Math.max(0, Math.round((navigation_view.appBarHeight
                                                     - navigation_view.titleBarTopMargin
                                                     - height) / 2) - 1)

        RowLayout {
            id: aiTopBarRow
            anchors.fill: parent
            spacing: 8

            AIAssistantToggleButton {
                id: aiTopBarButton
                Layout.alignment: Qt.AlignVCenter
                onClicked: {
                    var ok = ActionManager.triggerAction("ui.ai.toggle")
                    if (!ok) {
                        AIChatBridge.togglePanel()
                    }
                }
            }

            Label {
                Layout.alignment: Qt.AlignVCenter
                text: AIChatBridge.streaming
                      ? qsTr("AI is responding")
                      : (AIChatBridge.panelVisible ? qsTr("AI opened") : "Alt+I")
                color: AIChatBridge.streaming
                       ? Theme.accentColor.defaultBrushFor()
                       : Theme.res.textFillColorPrimary
                font.pixelSize: 12
            }
        }
    }
}
