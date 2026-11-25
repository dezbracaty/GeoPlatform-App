import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import SmoothUIDock
import GPlatform
import com.kdab.dockwidgets as KDDW
import "../components"
import "../dialogs"

ContentPage {
    id: root
    objectName: "printWorkspaceHost"
    title: ""
    padding: 0


    property bool dockReady: false
    property bool changingPage: false
    property bool pendingPrintAfterConnect: false
    property string activePageName: "prepare"
    property var pendingWorkspace: null
    property var calibrationPanel: null
    readonly property var previewPage: previewLoader.item
    readonly property var realisticPage: realisticLoader.item
    readonly property var printerControlPopup: printerControlLoader.item
    readonly property var activeWorkspaceContent:
        activePageName === "preview" ? (previewPage ? previewPage.workspaceContent : null)
        : (activePageName === "realistic" ? (realisticPage ? realisticPage.workspaceContent : null)
                                           : preparePage.workspaceContent)
    readonly property var workspacePageNames: ["prepare", "preview", "realistic"]
    readonly property Item guidePrintBar: globalPrintBar
    property var guideTabs: ({})
    function guideTab(name) { return guideTabs[name] || null }
    function guidePageReady(name) {
        const renderer = rendererForPage(name)
        return dockReady && activePageName === name && renderer && renderer.visible
            && renderer.renderActive && renderer.hasPresentedFrame
    }

    function openPrinterControl() {
        globalPrintBar.closeMachinePopup()
        printerControlLoader.active = true
        if (printerControlPopup) printerControlPopup.open()
    }

    function togglePrinterControl() {
        if (printerControlPopup && printerControlPopup.opened) {
            printerControlPopup.close()
            return
        }
        openPrinterControl()
    }

    readonly property alias configurationPanel: printConfiguration
    readonly property alias sceneSettingsPopup: workspaceSceneSettings

    function openSliceSettings(params) {
        if (root.activePageName !== "prepare") return false
        params = params || {}
        if (params.workspaceIndex !== undefined)
            return globalPrintBar.openMachineSettings(params)
        const key = params.settingKey || params.paramKey || params.key
        if (key) {
            const tab = SliceSettingsBridge.getSettingSchema(key).tab
            if (tab === "printer" || tab === "filament")
                return globalPrintBar.openMachineSettings({workspaceIndex: tab === "printer" ? 2 : 1, settingKey: key})
            printConfiguration.focusSetting(key)
        } else
            printConfiguration.showGlobalProcess()
        return true
    }

    function openSceneSettings() {
        workspaceSceneSettings.triggerButton = Global.mainScreen ? Global.mainScreen.viewMenuAnchor : null
        workspaceSceneSettings.open()
    }

    // Capture an immutable send intent. Never submit from a connection callback.
    function availablePrintFormat() {
        const formats = PrinterService.supportedPrintFormats
        for (let i = 0; i < formats.length; ++i) {
            const format = formats[i]
            if ((format === "gcode" || format === "gcode-3mf")
                    && SlicingPreviewBridge.canExportPrintOutput(format))
                return format
        }
        return ""
    }

    function outputPath(format) {
        return format === "gcode-3mf" ? SlicingPreviewBridge.printPackagePath
                                      : SlicingPreviewBridge.printOutputPath
    }

    function openPrintConfirmation() {
        pendingPrintAfterConnect = false
        const format = availablePrintFormat()
        if (!PrinterService.connected || PrinterService.printing
                || PrinterService.transferBusy || TaskStateNotifier.isBusy
                || SlicingPreviewBridge.isLoading
                || SliceSettingsBridge.hasPendingReslice
                || !SlicingPreviewBridge.printOutputReady || !format)
            return
        sendConfirmation.settingsRevision = SliceSettingsBridge.schemaRevision
        levelingOption.checked = false
        sendConfirmation.deviceId = PrinterService.selectedDeviceId
        sendConfirmation.deviceName = PrinterService.selectedDeviceName
        sendConfirmation.formatId = format
        sendConfirmation.filePath = outputPath(format)
        sendConfirmation.destinationName = SlicingPreviewBridge.defaultPrintOutputFileName(format)
        sendConfirmation.invalidated = false
        if (printerControlPopup) printerControlPopup.close()
        sendConfirmation.open()
    }

    function confirmPrintSend(printNow) {
        // Recheck at the action boundary, including paths and capability changes.
        if (!sendConfirmation.intentValid || (printNow && !PrinterService.supportsStart))
            return
        const path = sendConfirmation.filePath
        const name = sendConfirmation.destinationName
        const format = sendConfirmation.formatId
        const leveling = printNow && PrinterService.supportsVendorOptions
            && format === "gcode-3mf" && levelingOption.checked
        sendConfirmation.invalidated = true
        sendConfirmation.close()
        PrinterService.submitJob(path, name, format, printNow,
                                 leveling, false, false, false, false)
        openPrinterControl()
    }

    Connections {
        target: SliceSettingsBridge
        function onPrinterSwitchDecisionRequested(preset) {
            presetSwitch.preset = preset
            presetSwitch.decided = false
            presetSwitch.open()
        }
    }

    ContentDialog {
        id: presetSwitch
        objectName: "printerSwitch.dialog"
        property var preset: ({})
        property bool decided: false
        width: Math.min(660, root.width - 32)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        title: qsTr("Transfer or discard changes")

        header: RowLayout {
            Label {
                Layout.fillWidth: true
                leftPadding: presetSwitch.leftPadding
                topPadding: Theme.dialogPadding
                bottomPadding: Theme.dialogPadding
                font: Typography.subtitle
                text: presetSwitch.title
            }
            IconButton {
                objectName: "printerSwitch.close"
                Layout.rightMargin: Theme.dialogPadding
                flat: true
                icon.name: FluentIcons.graph_ChromeClose
                Accessible.name: qsTr("Close")
                onClicked: presetSwitch.close()
            }
        }

        function choose(action, name = "") {
            if (decided)
                return
            decided = true
            close()
            Qt.callLater(function () { SliceSettingsBridge.resolvePrinterSwitch(action, name) })
        }
        onClosed: choose("cancel")

        ColumnLayout {
            width: presetSwitch.availableWidth
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: qsTr("You have unsaved changes in preset “%1”.").arg(presetSwitch.preset.name || "")
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(230, changeList.implicitHeight)
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    id: changeList
                    width: parent.width
                    Repeater {
                        model: presetSwitch.preset.changes || []
                        Label {
                            required property var modelData
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            text: {
                                const key = modelData.key.split("#")[0]
                                const schema = SliceSettingsBridge.getSettingSchema(key)
                                const title = schema.label || modelData.key
                                return title + "   " + modelData.before + " → " + modelData.after
                            }
                        }
                    }
                }
            }
            Label {
                visible: presetSwitch.preset.transferAllowed === true
                    && (presetSwitch.preset.changes || []).some(function (row) { return !row.transferable })
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Changes tied to a different extruder configuration will be skipped during transfer.")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "printerSwitch.discard"
                text: qsTr("Discard")
                ToolTip.visible: hovered
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: qsTr("Discard unsaved changes and use the target printer's preset.")
                onClicked: presetSwitch.choose("discard")
            }
            Button {
                objectName: "printerSwitch.transfer"
                visible: presetSwitch.preset.transferAllowed === true
                text: qsTr("Transfer")
                ToolTip.visible: hovered
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: qsTr("Apply transferable changes to the target printer.")
                onClicked: presetSwitch.choose("transfer")
            }
            Button {
                objectName: "printerSwitch.save"
                text: qsTr("Save")
                ToolTip.visible: hovered && enabled
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: qsTr("Save the preset changes, then continue switching.")
                onClicked: {
                    if (presetSwitch.preset.saveAs) {
                        presetName.text = presetSwitch.preset.name + " - " + qsTr("Copy")
                        savePresetName.open()
                    } else {
                        presetSwitch.choose("save")
                    }
                }
            }
        }
    }

    ContentDialog {
        id: savePresetName
        width: presetSwitch.width
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        title: qsTr("Save user preset")
        readonly property var nameStatus: visible
            ? SliceSettingsBridge.printerSwitchPresetNameStatus(presetName.text) : ({})
        onOpened: {
            presetName.forceActiveFocus()
            presetName.selectAll()
        }

        ColumnLayout {
            width: savePresetName.availableWidth
            TextField {
                id: presetName
                objectName: "printerSwitch.presetName"
                Layout.fillWidth: true
                placeholderText: qsTr("Preset name")
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: savePresetName.nameStatus.message || ""
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "printerSwitch.cancel"
                text: qsTr("Cancel")
                onClicked: savePresetName.close()
            }
            Button {
                objectName: "printerSwitch.confirmSave"
                enabled: savePresetName.nameStatus.valid === true
                text: qsTr("Save")
                onClicked: {
                    const name = presetName.text
                    savePresetName.close()
                    presetSwitch.choose("save", name)
                }
            }
        }
    }

    ContentDialog {
        id: presetSaveDialog
        objectName: "presetSaveDialog"
        width: Math.min(460, root.width - 24)
        title: duplicate ? qsTr("Duplicate preset") : qsTr("Save user preset")
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        property string domain: ""
        property int filamentSlot: -1
        property string revision: ""
        property string error: ""
        property bool canSaveCurrent: false
        property bool duplicate: false
        readonly property var nameStatus: visible
            ? SliceSettingsBridge.savePreset(domain, filamentSlot, revision, true, newPresetName.text, true, false)
            : ({})

        function showFor(domain, slot, sourceName, duplicate) {
            presetSaveDialog.domain = domain
            presetSaveDialog.duplicate = duplicate
            filamentSlot = slot
            revision = SliceSettingsBridge.configurationRevisionToken()
            canSaveCurrent = !duplicate && SliceSettingsBridge.savePreset(domain, slot, revision, false, "", true, false).valid
            newPresetName.text = sourceName + " - " + qsTr("Copy")
            overwritePreset.checked = false
            error = ""
            open()
        }
        function submit(saveAs) {
            const result = SliceSettingsBridge.savePreset(domain, filamentSlot, revision,
                saveAs, newPresetName.text, false, saveAs && overwritePreset.checked)
            if (result.valid)
                close()
            else
                error = result.message
        }
        onOpened: {
            newPresetName.forceActiveFocus()
            newPresetName.selectAll()
        }
        ColumnLayout {
            width: presetSaveDialog.availableWidth
            TextField {
                id: newPresetName
                objectName: "presetSaveDialog.name"
                Layout.fillWidth: true
                placeholderText: qsTr("Preset name")
                onTextEdited: {
                    overwritePreset.checked = false
                    presetSaveDialog.error = ""
                }
                onAccepted: if (savePresetCopy.enabled) presetSaveDialog.submit(true)
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: presetSaveDialog.error || presetSaveDialog.nameStatus.message
                    || presetSaveDialog.nameStatus.overwriteWarning || ""
            }
            CheckBox {
                id: overwritePreset
                objectName: "presetSaveDialog.overwrite"
                visible: !!presetSaveDialog.nameStatus.overwriteWarning
                text: qsTr("Overwrite existing user preset")
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                onClicked: presetSaveDialog.close()
            }
            Button {
                objectName: "presetSaveDialog.saveCurrent"
                visible: presetSaveDialog.canSaveCurrent
                text: qsTr("Save")
                onClicked: presetSaveDialog.submit(false)
            }
            Button {
                id: savePresetCopy
                objectName: "presetSaveDialog.saveAs"
                text: qsTr("Save as New Preset")
                enabled: presetSaveDialog.nameStatus.valid === true
                    && (!overwritePreset.visible || overwritePreset.checked)
                onClicked: presetSaveDialog.submit(true)
            }
        }
    }

    ContentDialog {
        id: sendConfirmation
        objectName: "printerSendConfirmation"
        title: qsTr("Send to Printer")
        width: Math.min(520, root.width - 24)
        property string deviceId: ""
        property string deviceName: ""
        property string formatId: ""
        property string filePath: ""
        property string destinationName: ""
        property int settingsRevision: -1
        property bool invalidated: true
        readonly property bool intentValid: !invalidated && contextValid
        readonly property bool contextValid: PrinterService.connected && PrinterService.supportsUpload && !PrinterService.printing
            && !PrinterService.transferBusy && !TaskStateNotifier.isBusy
            && !SlicingPreviewBridge.isLoading
            && !SliceSettingsBridge.hasPendingReslice
            && settingsRevision === SliceSettingsBridge.schemaRevision
            && SliceSettingsBridge.configurationError.length === 0
            && SlicingPreviewBridge.printOutputReady
            && deviceId.length > 0 && deviceId === PrinterService.selectedDeviceId
            && PrinterService.supportedPrintFormats.indexOf(formatId) >= 0
            && SlicingPreviewBridge.canExportPrintOutput(formatId)
            && filePath.length > 0 && filePath === root.outputPath(formatId)
            && destinationName === SlicingPreviewBridge.defaultPrintOutputFileName(formatId)
        onContextValidChanged: {
            if (visible && !contextValid)
                invalidated = true
        }
        onOpened: uploadOnlyButton.forceActiveFocus()
        ColumnLayout {
            width: sendConfirmation.availableWidth
            spacing: 12
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: qsTr("Send %1 to %2 (%3)?")
                      .arg(sendConfirmation.destinationName)
                      .arg(sendConfirmation.deviceName).arg(sendConfirmation.formatId)
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: qsTr("Upload Only stores the file without starting the printer. Upload and Print starts printing after upload.")
                wrapMode: Text.Wrap
            }
            CheckBox {
                id: levelingOption
                objectName: "printerSendLeveling"
                visible: PrinterService.supportsVendorOptions && sendConfirmation.formatId === "gcode-3mf"
                enabled: sendConfirmation.intentValid
                checked: false
                text: qsTr("Level the bed before printing")
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: !sendConfirmation.intentValid
                text: qsTr("The preview or printer changed. Cancel and send again to review the updated job.")
                wrapMode: Text.Wrap
            }
        }
        footer: DialogButtonBox {
            alignment: Qt.AlignRight
            Button {
                implicitWidth: Math.max(96, implicitContentWidth + leftPadding + rightPadding)
                objectName: "printerSendCancel"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                text: qsTr("Cancel")
                onClicked: sendConfirmation.close()
            }
            Button {
                implicitWidth: Math.max(96, implicitContentWidth + leftPadding + rightPadding)
                id: uploadOnlyButton
                objectName: "printerSendUploadOnly"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                text: qsTr("Upload Only")
                highlighted: true
                enabled: sendConfirmation.intentValid
                onClicked: root.confirmPrintSend(false)
            }
            Button {
                implicitWidth: Math.max(96, implicitContentWidth + leftPadding + rightPadding)
                objectName: "printerSendUploadAndPrint"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                text: qsTr("Upload and Print")
                enabled: sendConfirmation.intentValid && PrinterService.supportsStart
                onClicked: root.confirmPrintSend(true)
            }
        }
    }

    Connections {
        target: SlicingPreviewBridge
        function onPrintOutputPathChanged() {
            if (sendConfirmation.visible)
                sendConfirmation.invalidated = true
        }
        function onCurrentToolpathPreviewChanged() {
            if (sendConfirmation.visible)
                sendConfirmation.invalidated = true
        }
    }

    Connections {
        target: SliceSettingsBridge
        function invalidateSendIntent() {
            if (sendConfirmation.visible)
                sendConfirmation.invalidated = true
        }
        function onSettingsChanged() { invalidateSendIntent() }
        function onSchemaChanged() { invalidateSendIntent() }
        function onPresetSelectionChanged() { invalidateSendIntent() }
        function onMachineSelectionChanged() { invalidateSendIntent() }
    }

    // Historical custom DockTab implementation (d2aab93 -> 40b992a):
    // the default Dock tab strip is replaced by a component loaded inside the
    // group's stackLayout, so these fixed tabs float over the 3D viewport.
    Component {
        id: printWorkspaceTabBar

        Item {
            id: customTabRoot
            anchors.fill: parent

            property QtObject groupCpp
            property var tabBarModel
            property int currentTabIndex: -1

            ToolbarSurface {
                id: tabShell
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 16
                anchors.topMargin: 64
                width: tabRow.implicitWidth + 8
                height: PrintWorkspaceStyle.tabShellHeight
                color: PrintWorkspaceStyle.secondarySurface(tabShell.SmoothUI.dark)

                Row {
                    id: tabRow
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 2

                    Repeater {
                        model: customTabRoot.tabBarModel || 0

                        Button {
                            id: workspaceTab
                            Component.onCompleted: root.guideTabs[root.workspacePageNames[index]] = workspaceTab
                            Component.onDestruction: {
                                if (root.guideTabs[root.workspacePageNames[tabIndex]] === workspaceTab)
                                    delete root.guideTabs[root.workspacePageNames[tabIndex]]
                            }
                            objectName: "print.workspace.tab."
                                        + root.workspacePageNames[index]
                            readonly property int tabIndex: index
                            readonly property bool selected:
                                index === customTabRoot.currentTabIndex
                            width: 84
                            height: PrintWorkspaceStyle.tabHeight
                            text: title
                            padding: 0
                            flat: true
                            checkable: false

                            background: Rectangle {
                                radius: tabShell.SmoothUI.radius
                                color: workspaceTab.selected
                                       ? PrintWorkspaceStyle.surface(tabShell.SmoothUI.dark)
                                       : (workspaceTab.hovered
                                          ? Theme.res.subtleFillColorSecondary
                                          : "transparent")
                                border.width: 0

                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 2
                                    width: 28
                                    height: 2
                                    radius: 1
                                    visible: workspaceTab.selected
                                    color: Theme.accentColor.defaultBrushFor()
                                }

                                Behavior on color {
                                    ColorAnimation { duration: 120 }
                                }
                            }

                            contentItem: Label {
                                text: workspaceTab.text
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font: workspaceTab.selected
                                      ? Typography.bodyStrong
                                      : Typography.body
                                color: workspaceTab.selected
                                       ? Theme.accentColor.defaultBrushFor()
                                       : Theme.res.textFillColorSecondary
                            }

                            onClicked: root.activatePage(
                                           root.workspacePageNames[index], true)
                        }
                    }
                }
            }
        }
    }

    QtObject {
        id: sharedWorkspaceContext
        readonly property bool sceneInitialized: StartupState.sceneReady
    }

    Connections {
        target: PrinterService

        function onConnectionChanged() {
            if (!root.pendingPrintAfterConnect)
                return
            if (PrinterService.connectionState === PrinterService.Connected) {
                Qt.callLater(function() {
                    if (root.pendingPrintAfterConnect)
                        root.openPrintConfirmation()
                })
            } else if (PrinterService.connectionState
                       === PrinterService.ConnectionError) {
                root.pendingPrintAfterConnect = false
            }
        }
    }

    function pageForEnvironment(environmentName) {
        return environmentName === "slicing" ||
               environmentName === "preview" ||
               environmentName === "support" ? "preview" : "prepare"
    }

    function rendererForPage(pageName) {
        if (pageName === "preview")
            return previewPage ? previewPage.renderer : null
        if (pageName === "realistic")
            return realisticPage ? realisticPage.renderer : null
        return preparePage.renderer
    }

    function ensureWorkspacePage(pageName) {
        if (pageName === "prepare") return preparePage
        const loader = workspaceLoader(pageName)
        if (!loader) return null
        // Export, reslice, and the guide need the page within this call.
        loader.asynchronous = false
        loader.active = true
        return loader.item
    }

    function workspaceLoader(pageName) {
        return pageName === "preview" ? previewLoader
             : pageName === "realistic" ? realisticLoader : null
    }

    function updateWorkspaceEnvironment(pageName, requested) {
        const environment = pageName === "preview" ? "slicing" : "normal"
        if (requested && MenuData.currentEnvironment !== environment)
            ActionManager.triggerAction("environment.switch", { environment: environment })
    }

    function activatePage(pageName, updateEnvironment, copyCamera = true) {
        if (!dockReady || workspacePageNames.indexOf(pageName) < 0
                || (!StartupState.ready && pageName !== "prepare"))
            return
        if (pendingWorkspace && pendingWorkspace.pageName === pageName) {
            pendingWorkspace.updateEnvironment = updateEnvironment
            return
        }

        const sourceRenderer = pendingWorkspace ? pendingWorkspace.sourceRenderer
                                                : rendererForPage(activePageName)
        const targetRenderer = rendererForPage(pageName)
        const alreadyActive = activePageName === pageName && targetRenderer && targetRenderer.renderActive
        const needsFirstFrame = StartupState.ready && !alreadyActive
                && (!targetRenderer || (!targetRenderer.hasPresentedFrame && !targetRenderer.renderingError))
        const dock = pageName === "preview" ? previewDock
                   : pageName === "realistic" ? realisticDock : prepareDock
        const viewport = pageName === "preview" ? previewViewport
                       : pageName === "realistic" ? realisticViewport : prepareViewport
        changingPage = true
        try {
            if (!needsFirstFrame && copyCamera && targetRenderer && sourceRenderer
                    && targetRenderer !== sourceRenderer)
                targetRenderer.copyCameraStateFrom(sourceRenderer)
            pendingWorkspace = needsFirstFrame && !viewport.loadFailed
                    ? { pageName, sourceRenderer, updateEnvironment, copyCamera, waitingForPaint: true } : null
            activePageName = pageName
            dock.setAsCurrentTab()
            if (!pendingWorkspace)
                updateWorkspaceEnvironment(pageName, updateEnvironment)
        } finally {
            changingPage = false
        }
    }

    function startWorkspaceAfterLoadingFrame(pageName) {
        const request = pendingWorkspace
        if (!request || request.pageName !== pageName || activePageName !== pageName
                || !request.waitingForPaint)
            return
        pendingWorkspace = { pageName, sourceRenderer: request.sourceRenderer,
            updateEnvironment: request.updateEnvironment, copyCamera: request.copyCamera,
            waitingForPaint: false }
        const loader = workspaceLoader(pageName)
        if (loader && loader.status !== Loader.Ready && loader.status !== Loader.Error) {
            loader.asynchronous = true
            loader.active = true
        }
        finishWorkspaceActivation(pageName)
    }

    function finishWorkspaceActivation(pageName) {
        const request = pendingWorkspace
        if (!request || request.pageName !== pageName || activePageName !== pageName
                || request.waitingForPaint)
            return
        const loader = workspaceLoader(pageName)
        if (loader && loader.status !== Loader.Ready && loader.status !== Loader.Error)
            return
        const targetRenderer = rendererForPage(pageName)
        changingPage = true
        try {
            if (request.copyCamera && targetRenderer && request.sourceRenderer
                    && targetRenderer !== request.sourceRenderer)
                targetRenderer.copyCameraStateFrom(request.sourceRenderer)
            pendingWorkspace = null
            updateWorkspaceEnvironment(pageName, request.updateEnvironment)
        } finally {
            if (pendingWorkspace === request)
                pendingWorkspace = null
            changingPage = false
        }
    }

    component WorkspaceViewport: Item {
        id: viewport
        SmoothUI.theme: Theme.of(viewport)
        required property Item host
        required property string pageName
        property var renderer: null
        property bool loadFailed: false
        readonly property bool selected: host.dockReady && host.activePageName === pageName
        readonly property bool pending: host.pendingWorkspace !== null
                                        && host.pendingWorkspace.pageName === pageName
        readonly property string renderError: loadFailed
                ? qsTr("3D rendering stopped. Restart the application to try again.")
                : renderer ? renderer.renderingError : ""
        readonly property bool hasFrame: !!renderer && renderer.hasPresentedFrame
        readonly property bool animateLoading: visible && !!Window.window
                && Window.window.visibility !== Window.Minimized
                && (Window.window.active || StartupState.backgroundReplay)
        readonly property string loadingText: pageName === "preview" ? qsTr("Opening toolpath preview…")
                : pageName === "realistic" ? qsTr("Opening realistic view…") : qsTr("Preparing 3D view…")
        readonly property string loadingState: !StartupState.ready || !selected ? ""
                : hasFrame ? "ready" : renderError.length > 0 ? "failed" : "waiting"

        WorkspaceLoadingFrame {
            id: loadingOverlay
            objectName: "print.workspace.loading." + viewport.pageName
            anchors.fill: parent
            z: 5
            color: Theme.res.solidBackgroundFillColorBase
            opacity: 0
            visible: state === "waiting" || state === "failed" || viewportReveal.running
            armed: viewport.pending && viewport.host.pendingWorkspace.waitingForPaint
                   && viewport.loadingState === "waiting"
            onFramePresented: viewport.host.startWorkspaceAfterLoadingFrame(viewport.pageName)

            state: viewport.loadingState
            states: [
                State {
                    name: "waiting"
                    PropertyChanges { target: loadingOverlay; opacity: 1 }
                },
                State {
                    name: "failed"
                    PropertyChanges { target: loadingOverlay; opacity: 1 }
                },
                State { name: "ready" }
            ]
            transitions: Transition {
                id: viewportReveal
                from: "waiting"
                to: "ready"
                enabled: viewport.animateLoading
                OpacityAnimator { target: loadingOverlay; duration: 120; easing.type: Easing.OutCubic }
            }

            MouseArea {
                anchors.fill: parent
                enabled: loadingOverlay.state === "waiting" || loadingOverlay.state === "failed"
                acceptedButtons: Qt.AllButtons
                onWheel: function(event) { event.accepted = true }
            }
            Rectangle {
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 32,
                    Math.max(248, Math.min(loadingOverlay.state === "failed" ? 380 : 320,
                                          message.implicitWidth + 76))))
                height: Math.max(68, statusRow.implicitHeight + 30)
                radius: 10
                border.width: 1
                border.color: Theme.res.cardStrokeColorDefault
                color: Theme.res.cardBackgroundColor
                RowLayout {
                    id: statusRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: parent.width < 248 ? 12 : 16
                    spacing: 12
                    LoadingGlyph {
                        visible: loadingOverlay.state !== "failed"
                        Layout.preferredWidth: width
                        Layout.preferredHeight: height
                        accentColor: Colors.blue.defaultBrushFor(viewport.SmoothUI.dark)
                        mutedColor: Theme.res.textFillColorSecondary
                        animating: (loadingOverlay.state === "waiting" || viewportReveal.running)
                                   && viewport.animateLoading
                    }
                    Label {
                        visible: loadingOverlay.state === "failed"
                        Layout.preferredWidth: 32
                        horizontalAlignment: Text.AlignHCenter
                        text: "!"
                        font.pixelSize: 22
                        color: Theme.res.systemFillColorCritical
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            id: message
                            Layout.fillWidth: true
                            text: loadingOverlay.state === "failed" ? qsTr("View could not be opened")
                                                                     : viewport.loadingText
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: Theme.res.textFillColorPrimary
                            wrapMode: Text.Wrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }
                        Label {
                            visible: loadingOverlay.state === "failed"
                            Layout.fillWidth: true
                            text: viewport.renderError
                            color: Theme.res.textFillColorSecondary
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 16
            width: Math.min(parent.width - 32, 560)
            height: renderErrorLabel.implicitHeight + 24
            radius: 6
            z: 60
            visible: viewport.hasFrame && viewport.renderError.length > 0
            color: Theme.res.solidBackgroundFillColorBase
            Label {
                id: renderErrorLabel
                anchors.fill: parent
                anchors.margins: 12
                text: viewport.renderError
                wrapMode: Text.Wrap
                color: Theme.res.textFillColorPrimary
            }
        }
    }

    DockingArea {
        id: dockArea
        anchors.fill: parent
        options: SmoothUIDock.MainWindowOption_HasCentralGroup
        uniqueName: "gplatform.print.workspace"
        contentsMargin: 0
        titleBarContentsMargin: 0

        DockWidget {
            id: prepareDock
            uniqueName: "gplatform.print.prepare"
            title: qsTr("Prepare")
            options: KDDW.KDDockWidgets.DockWidgetOption_NotClosable

            WorkspaceViewport {
                id: prepareViewport
                host: root
                pageName: "prepare"
                renderer: preparePage.renderer

                Prepare3DPrintPage {
                    id: preparePage
                    anchors.fill: parent
                    workspaceContext: sharedWorkspaceContext
                    renderActive: StartupState.sceneReady && prepareViewport.selected && !prepareViewport.pending
                    onWorkspaceRequested: function(workspaceName) {
                        root.activatePage(workspaceName, true)
                    }
                }
            }
        }

        DockWidget {
            id: previewDock
            uniqueName: "gplatform.print.preview"
            title: qsTr("Preview")
            options: KDDW.KDDockWidgets.DockWidgetOption_NotClosable

            WorkspaceViewport {
                id: previewViewport
                host: root
                pageName: "preview"
                renderer: root.previewPage ? root.previewPage.renderer : null
                loadFailed: previewLoader.status === Loader.Error

                Loader {
                    id: previewLoader
                    anchors.fill: parent
                    active: false
                    onStatusChanged: root.finishWorkspaceActivation("preview")
                    sourceComponent: Component {
                        Preview3DPrintPage {
                            workspaceContext: sharedWorkspaceContext
                            renderActive: StartupState.ready && previewViewport.selected && !previewViewport.pending
                            onWorkspaceRequested: function(workspaceName) {
                                root.activatePage(workspaceName, true)
                            }
                        }
                    }
                }
            }
        }

        DockWidget {
            id: realisticDock
            uniqueName: "gplatform.print.realistic"
            title: qsTr("Realistic")
            options: KDDW.KDDockWidgets.DockWidgetOption_NotClosable

            WorkspaceViewport {
                id: realisticViewport
                host: root
                pageName: "realistic"
                renderer: root.realisticPage ? root.realisticPage.renderer : null
                loadFailed: realisticLoader.status === Loader.Error

                Loader {
                    id: realisticLoader
                    anchors.fill: parent
                    active: false
                    onStatusChanged: root.finishWorkspaceActivation("realistic")
                    sourceComponent: Component {
                        Realistic3DPrintPage {
                            workspaceContext: sharedWorkspaceContext
                            renderActive: StartupState.ready && realisticViewport.selected && !realisticViewport.pending
                            onWorkspaceRequested: function(workspaceName) {
                                root.activatePage(workspaceName, true)
                            }
                        }
                    }
                }
            }
        }

        Component.onCompleted: {
            SmoothUIDock.setCustomTabBarComponent(printWorkspaceTabBar)
            addDockWidgetAsTab(prepareDock)
            prepareDock.addDockWidgetAsTab(previewDock)
            prepareDock.addDockWidgetAsTab(realisticDock)
            dockReady = true
            StartupState.attachWorkspace(preparePage.renderer)
            Qt.callLater(function() {
                root.activatePage(
                    StartupState.ready ? root.pageForEnvironment(MenuData.currentEnvironment) : "prepare",
                    false)
            })
        }
    }

    PanelSurface {
        objectName: "calibrationPlatePanel"
        visible: root.dockReady && root.activePageName === "preview" && CalibrationBridge.previewActive
        anchors.left: parent.left
        anchors.top: parent.top
        // Leave the existing preview toolbar clear, including failed or
        // partially completed tasks whose panel contains extra actions.
        anchors.leftMargin: 16 + PrintWorkspaceStyle.toolbarWidth + 12
        anchors.topMargin: 122
        width: 270
        height: calibrationPlateContent.implicitHeight + 32
        z: 1001
        ColumnLayout {
            id: calibrationPlateContent
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
            Label { text: CalibrationBridge.resultContext.title || ""; font: Typography.bodyStrong; Layout.fillWidth: true; elide: Text.ElideRight }
            RowLayout {
                Layout.fillWidth: true
                visible: CalibrationBridge.resultContext.mode !== 14 && CalibrationBridge.resultContext.mode !== 15
                Label { text: "打印盘" }
                ComboBox {
                    objectName: "calibrationPlateSelector"
                    Layout.fillWidth: true
                    model: CalibrationBridge.plates
                    textRole: "name"
                    currentIndex: CalibrationBridge.selectedPlate
                    enabled: !CalibrationBridge.busy && CalibrationBridge.configurationValid
                    onActivated: {
                        if (!CalibrationBridge.selectPlate(currentIndex))
                            currentIndex = Qt.binding(function() { return CalibrationBridge.selectedPlate })
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font: Typography.caption
                color: CalibrationBridge.configurationValid ? Theme.res.textFillColorSecondary : Theme.res.systemFillColorCaution
                text: CalibrationBridge.busy ? CalibrationBridge.message : CalibrationBridge.configurationValid ? "当前盘已切片" : "配置已改变，请重新准备校准。"
            }
            Button { objectName: "calibrationEditParameters"; text: "修改参数…"; Layout.fillWidth: true; enabled: !TaskStateNotifier.isBusy; onClicked: { if(root.calibrationPanel)root.calibrationPanel.edit() } }
            Button { visible: CalibrationBridge.busy; text: "取消任务"; Layout.fillWidth: true; enabled: CalibrationBridge.state !== "cancelling"; onClicked: CalibrationBridge.cancel() }
            Button { objectName: "calibrationRecordResult"; text: "记录校准结果…"; Layout.fillWidth: true; visible: CalibrationBridge.resultContext.mode !== 14 && CalibrationBridge.resultContext.mode !== 15; enabled: !TaskStateNotifier.isBusy && CalibrationBridge.configurationValid; onClicked: { if(root.calibrationPanel)root.calibrationPanel.record() } }
            Button { visible: CalibrationBridge.plates.some(function(p) { return !p.ready }); text: "继续未完成盘"; Layout.fillWidth: true; enabled: !TaskStateNotifier.isBusy && CalibrationBridge.configurationValid; onClicked: CalibrationBridge.retry() }
            Button { visible: CalibrationBridge.details.length > 0; text: "查看准备提示"; flat: true; Layout.fillWidth: true; onClicked: { if(root.calibrationPanel)root.calibrationPanel.showDetails() } }
        }
    }

    Button {
        id: accountWorkspaceButton
        objectName: "accountWorkspaceButton"
        readonly property bool signedIn:
            AIAccountBridge.signedIn || PrinterService.signedIn

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 16
        anchors.topMargin: 16
        implicitWidth: Math.max(signedIn ? 108 : 96,
                                implicitContentWidth
                                + leftPadding + rightPadding)
        implicitHeight: 40
        padding: 8
        horizontalPadding: 12
        spacing: 8
        visible: root.dockReady
        z: 1000
        onClicked: AIAccountBridge.openAccountCenter("overview")

        contentItem: RowLayout {
            id: accountIdentityContent
            spacing: accountWorkspaceButton.spacing

            Rectangle {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                radius: 12
                color: accountWorkspaceButton.signedIn
                       ? Colors.withOpacity(
                             accountWorkspaceButton.SmoothUI.theme.accentColor.defaultBrushFor(
                                 accountWorkspaceButton.SmoothUI.dark), 0.14)
                       : accountWorkspaceButton.SmoothUI.theme.res.subtleFillColorSecondary

                Icon {
                    anchors.centerIn: parent
                    width: 19
                    height: 19
                    source: accountWorkspaceButton.signedIn
                            ? FluentIcons.graph_ContactSolid
                            : FluentIcons.graph_Contact
                    color: accountWorkspaceButton.SmoothUI.textColor
                }
            }

            Label {
                text: accountWorkspaceButton.signedIn
                      ? qsTr("Account") : qsTr("Sign In")
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: accountWorkspaceButton.SmoothUI.textColor
            }

            Icon {
                visible: accountWorkspaceButton.signedIn
                Layout.preferredWidth: visible ? 12 : 0
                Layout.preferredHeight: visible ? 12 : 0
                source: FluentIcons.graph_ChevronDown
                color: accountWorkspaceButton.SmoothUI.textColor
            }
        }

        ToolTip {
            visible: accountWorkspaceButton.hovered
            delay: Theme.tooltipDelay
            text: qsTr("Accounts and Services")
        }
    }

    FloatingPrintConfiguration {
        id: printConfiguration
        boundsItem: root
        workspaceReady: root.dockReady && StartupState.ready && root.activePageName === "prepare"
        onAddModelRequested: Global.starter.chooseModel({})
        onPresetSaveRequested: (domain, slot, sourceName, duplicate) =>
            presetSaveDialog.showFor(domain, slot, sourceName, duplicate)
        onDetailedSettingsRequested: function(workspaceIndex, slotIndex, restoreFocus) {
            globalPrintBar.openMachineSettings({workspaceIndex: workspaceIndex, slotIndex: slotIndex,
                restoreFocus: restoreFocus})
        }
    }

    FloatingSettingsPanel {
        id: workspaceSceneSettings
        objectName: "sceneSettingsPopup"
        viewportItem: root
        title: qsTr("Scene Settings")
        panelWidth: 400
        panelHeight: 560
        integratedTitleBar: false
        lazyContent: true
        guideMode: Global.mainScreen && Global.mainScreen.userGuide.running
        panelContent: Component { SceneSettingsPanel {} }
    }

    GlobalPrintCommandBar {
        id: globalPrintBar
        objectName: "globalPrintCommandBar"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: Math.min(parent.width - PrintWorkspaceStyle.space16 * 2,
                        globalPrintBar.implicitWidth)
        z: 1000
        visible: root.dockReady
        workspaceName: root.activePageName === "preview" ? "slicing" : "normal"
        popupHost: root
        objectCount: preparePage.workspaceContent
                     ? preparePage.workspaceContent.objectCount : 0
        hasSelection: SelectionBridge.hasSelection
        selectedFileName: SelectionBridge.selectedModel
                          ? SelectionBridge.selectedModel.name : ""
        printerControlOpen: printerControlPopup ? printerControlPopup.opened : false

        onMachinePopupOpening: if (printerControlPopup) printerControlPopup.close()
        onPrinterControlRequested: root.togglePrinterControl()
        onAddModelRequested: Global.starter.chooseModel({})
        onSliceRequested: ActionManager.triggerAction("model.sliceAll", {})
        onExportRequested: function(formatId) {
            const page = root.ensureWorkspacePage("preview")
            if (page && page.workspaceContent)
                page.workspaceContent.requestPrintOutputExport(formatId)
        }
        onResliceRequested: {
            const page = root.ensureWorkspacePage("preview")
            if (page && page.workspaceContent)
                page.workspaceContent.triggerResliceCurrentPreview()
        }
        onPrintRequested: {
            if (!PrinterService.connected) {
                root.pendingPrintAfterConnect = true
                root.searchPrinters()
                return
            }
            root.openPrintConfirmation()
        }
        onPausePrintRequested: PrinterService.pausePrint()
        onResumePrintRequested: PrinterService.resumePrint()
        onCancelSubmissionRequested: PrinterService.cancelSubmission()
        onCheckStatusRequested: PrinterService.refreshPrinterStatus()
        onStopPrintRequested: PrinterService.cancelPrint()
        onDismissJobStatusRequested: PrinterService.dismissJobStatus()
    }

    Loader {
        id: printerControlLoader
        active: false
        sourceComponent: Component {
            PrinterControlPopup {
                popupHost: root
                popupAnchor: globalPrintBar.printerAnchor
                popupVerticalAnchor: globalPrintBar
                popupAbove: true

                onClosed: root.pendingPrintAfterConnect = false

                onFlashforgeAccountRequested: {
                    close()
                    AIAccountBridge.openAccountCenter("flashforge")
                }
            }
        }
    }

    Connections {
        target: MenuData

        function onEnvironmentChanged(newEnvironment) {
            if (!root.changingPage)
                root.activatePage(root.pageForEnvironment(newEnvironment), false)
        }
    }

    function searchPrinters() {
        root.openPrinterControl()
        if (PrinterService.discoveryState !== PrinterService.DiscoveryScanning)
            PrinterService.startDiscovery()
    }

}
