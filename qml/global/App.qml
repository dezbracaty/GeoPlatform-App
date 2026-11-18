import QtQuick
import QtQuick.Controls
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import SmoothUIDock
import GPlatform

Starter {
    id: starter

    property var pendingModelFiles: ({})
    property var pendingModelExports: ({})
    property bool themeInitialized: false

    function syncEffectiveTheme() {
        ActionManager.triggerAction("scene.theme.set", { dark: Theme.dark })
    }

    function chooseModel(params) { return chooseModelFile("file.import", params) }
    function chooseThreeMf(params) { return chooseModelFile("project.import", params) }
    function chooseStl(params) { return chooseModelFile("import_stl", params) }

    function chooseModelFile(actionCode, params) {
        if (!StartupState.ready) return false
        if (params && params.filePath !== undefined)
            return false
        var parentWindow = null
        for (var i = WindowRouter.windows.length - 1; i >= 0; --i) {
            var window = WindowRouter.windows[i]
            if (window && window.active) {
                parentWindow = window
                break
            }
            if (!parentWindow && window && window.visible)
                parentWindow = window
        }
        var filters = actionCode === "import_stl"
                ? [qsTr("STL Files (*.stl)")]
                : actionCode === "project.import" ? [qsTr("3MF Files (*.3mf)")]
                : [qsTr("Supported Files (*.stl *.3mf *.gcode)"),
                   qsTr("STL Files (*.stl)"), qsTr("3MF Files (*.3mf)"), qsTr("GCode Files (*.gcode)")]
        var requestId = FilePicker.openFile(Object.assign(SettingsHelper.getImportFileDialogState(), {
            title: actionCode === "import_stl" ? qsTr("Import STL") : actionCode === "project.import" ? qsTr("Open 3MF") : qsTr("Import File"),
            nameFilters: filters, parentWindow: parentWindow
        }))
        if (requestId <= 0) return false
        pendingModelFiles[requestId] = {
            actionCode: actionCode, params: Object.assign({}, params || {})
        }
        return true
    }

    function dispatchModelExport(action, params) {
        if (ActionManager.triggerAction(action, params)) return true
        var outcome = ActionManager.lastActionOutcome()
        NotificationManager.showWarning(qsTr("Export Models"), outcome.error || qsTr("The export could not be started."))
        return false
    }

    function exportModels(scope, format, individualFiles, saveAs) {
        if (!StartupState.ready || TaskStateNotifier.isBusy) return false
        var ids = ModelFileController.exportTargets(scope)
        if (!ids || ids.length === 0) return false
        var params = {scope: scope, format: format, modelIds: ids, individualFiles: !!individualFiles}
        var action = scope === "project" ? "project.save" : "model.export"
        if (scope === "project" && !saveAs && ModelFileController.projectFile !== "") {
            params.filePath = ModelFileController.projectFile
            return dispatchModelExport(action, params)
        }
        var parentWindow = null
        for (var i = 0; i < WindowRouter.windows.length; ++i)
            if (WindowRouter.windows[i].active) parentWindow = WindowRouter.windows[i]
        var options = {title: scope === "project" ? qsTr("Save Project") : qsTr("Export Models"),
            nameFilters: format === "stl" ? [qsTr("STL Files (*.stl)")] : [qsTr("3MF Files (*.3mf)")],
            parentWindow: parentWindow}
        if (scope === "project" && ModelFileController.projectFile !== "") options.currentFile = ModelFileController.projectFile
        var requestId = individualFiles ? FilePicker.openFolder(options) : FilePicker.saveFile(options)
        pendingModelExports[requestId] = {actionCode: action, params: params}
        return requestId > 0
    }

    Connections {
        target: FilePicker
        function onViewStateChanged(requestId, state) {
            if (starter.pendingModelFiles[requestId])
                SettingsHelper.saveImportFileDialogState(state)
        }
        function onAccepted(requestId, urls) {
            if (!StartupState.ready) return
            var exportRequest = starter.pendingModelExports[requestId]
            if (exportRequest) {
                delete starter.pendingModelExports[requestId]
                if (!urls || urls.length === 0) return
                exportRequest.params.filePath = urls[0].toString()
                starter.dispatchModelExport(exportRequest.actionCode, exportRequest.params)
                return
            }
            var request = starter.pendingModelFiles[requestId]
            if (!request) return
            delete starter.pendingModelFiles[requestId]
            if (!urls || urls.length === 0) return
            request.params.filePath = urls[0]
            if (!ActionManager.triggerAction(request.actionCode, request.params))
                console.error("File import failed:", JSON.stringify(ActionManager.lastActionOutcome()))
        }
        function onRejected(requestId) {
            delete starter.pendingModelExports[requestId]
            delete starter.pendingModelFiles[requestId]
        }
    }

    Component.onCompleted: {
        // Initialize Global starter reference
        Global.starter = starter

        // Register the KDDockWidgets QtQuick frontend and SmoothUI view factory
        // before any route can instantiate a docked page.
        SmoothUIDock.init()

        // SmoothUI controls explicitly bind their renderType to this value,
        // so keep them aligned with the native renderer selected in main().
        Theme.textRender = Text.NativeRendering

        // Load saved theme setting on startup
        Theme.darkMode = SettingsHelper.getDarkMode()
        themeInitialized = true
        syncEffectiveTheme()

        // Initialize Global settings
        Global.displayMode = SettingsHelper.getDisplayMode()
        Global.windowEffect = SettingsHelper.getWindowEffect()

        // Load saved primary color
        var savedPrimaryColor = SettingsHelper.getPrimaryColor()
        if (savedPrimaryColor !== "") {
            // Map color names to actual color objects
            switch(savedPrimaryColor) {
                case "Yellow": Theme.primaryColor = Colors.yellow; break;
                case "Orange": Theme.primaryColor = Colors.orange; break;
                case "Red": Theme.primaryColor = Colors.red; break;
                case "Magenta": Theme.primaryColor = Colors.magenta; break;
                case "Purple": Theme.primaryColor = Colors.purple; break;
                case "Blue": Theme.primaryColor = Colors.blue; break;
                case "Teal": Theme.primaryColor = Colors.teal; break;
                case "Green": Theme.primaryColor = Colors.green; break;
                default: break;
            }
        }

        // Mark Global as initialized to enable auto-save
        Global.initialized = true

        // Setup window routes
        WindowRouter.routes = {
            "/": "qrc:/qt/qml/GPlatform/qml/window/MainWindow.qml",
            "/page": "qrc:/qt/qml/GPlatform/qml/window/PageWindow.qml"
        }

        // Navigate to main window
        WindowRouter.go("/")
    }
    
    // Auto-save theme changes
    Connections {
        target: Theme
        function onDarkChanged() {
            if (starter.themeInitialized)
                starter.syncEffectiveTheme()
        }
        function onDarkModeChanged() {
            SettingsHelper.saveDarkMode(Theme.darkMode)
        }
        function onPrimaryColorChanged() {
            // Map color objects to names for saving
            var colorName = ""
            if (Theme.primaryColor === Colors.yellow) colorName = "Yellow"
            else if (Theme.primaryColor === Colors.orange) colorName = "Orange"
            else if (Theme.primaryColor === Colors.red) colorName = "Red"
            else if (Theme.primaryColor === Colors.magenta) colorName = "Magenta"
            else if (Theme.primaryColor === Colors.purple) colorName = "Purple"
            else if (Theme.primaryColor === Colors.blue) colorName = "Blue"
            else if (Theme.primaryColor === Colors.teal) colorName = "Teal"
            else if (Theme.primaryColor === Colors.green) colorName = "Green"
            
            if (colorName !== "") {
                SettingsHelper.savePrimaryColor(colorName)
            }
        }
    }
}
