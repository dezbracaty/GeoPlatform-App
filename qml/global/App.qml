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

    function chooseModel(params) { return chooseModelFile("file.import", params) }
    function chooseStl(params) { return chooseModelFile("import_stl", params) }

    function chooseModelFile(actionCode, params) {
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
                ? [qsTr("STL Files (*.stl)"), qsTr("All Files (*)")]
                : [qsTr("Supported Files (*.stl *.obj *.3mf *.glb *.gltf *.ply *.3ds *.dae *.fbx *.gcode *.gco *.g)"),
                   qsTr("GCode Files (*.gcode *.gco *.g)"),
                   qsTr("3D Models (*.stl *.obj *.3mf *.glb *.gltf *.ply *.3ds *.dae *.fbx)"),
                   qsTr("3MF Projects (*.3mf)"), qsTr("STL Files (*.stl)"),
                   qsTr("OBJ Files (*.obj)"), qsTr("All Files (*)")]
        var requestId = FilePicker.openFile(Object.assign(SettingsHelper.getImportFileDialogState(), {
            title: actionCode === "import_stl" ? qsTr("Import STL") : qsTr("Import File"),
            nameFilters: filters, parentWindow: parentWindow
        }))
        if (requestId <= 0) return false
        pendingModelFiles[requestId] = {
            actionCode: actionCode, params: Object.assign({}, params || {})
        }
        return true
    }

    Connections {
        target: FilePicker
        function onViewStateChanged(requestId, state) {
            if (starter.pendingModelFiles[requestId])
                SettingsHelper.saveImportFileDialogState(state)
        }
        function onAccepted(requestId, urls) {
            var request = starter.pendingModelFiles[requestId]
            if (!request) return
            delete starter.pendingModelFiles[requestId]
            if (!urls || urls.length === 0) return
            request.params.filePath = urls[0]
            if (!ActionManager.triggerAction(request.actionCode, request.params))
                console.error("File import failed:", JSON.stringify(ActionManager.lastActionOutcome()))
        }
        function onRejected(requestId) {
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
