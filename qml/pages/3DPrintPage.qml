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

    // NavigationView only inspects the routed page for these settings. Keep
    // them on the Dock host rather than on a page nested inside DockWidget.
    property var appBarBackground: ({
        type: "gradient",
        colors: ["#30FFFFFF", "#5087CEEB"],
        direction: "vertical"
    })
    property var sideBarBackground: ({
        type: "gradient",
        colors: ["#CCFFFFFF", "#D087CEEB"],
        direction: "vertical"
    })

    property bool dockReady: false
    property bool changingPage: false
    property bool pendingPrintAfterConnect: false
    property string activePageName: "prepare"
    readonly property var activeWorkspaceContent:
        activePageName === "preview" ? previewPage.workspaceContent
        : (activePageName === "realistic" ? realisticPage.workspaceContent
                                           : preparePage.workspaceContent)
    readonly property var workspacePageNames: ["prepare", "preview", "realistic"]

    function openPrinterControl() {
        globalPrintBar.closeMachinePopup()
        printerControlPopup.open()
    }

    function togglePrinterControl() {
        if (printerControlPopup.opened) {
            printerControlPopup.close()
            return
        }
        openPrinterControl()
    }

    function openSliceSettings(params) {
        return globalPrintBar.openMachineSettings(params || {})
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
        printerControlPopup.close()
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
                                radius: 8
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
        property bool sceneInitialized: false
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
            return previewPage.renderer
        if (pageName === "realistic")
            return realisticPage.renderer
        return preparePage.renderer
    }

    function activatePage(pageName, updateEnvironment) {
        if (!dockReady || workspacePageNames.indexOf(pageName) < 0) {
            return
        }

        var sourceRenderer = rendererForPage(activePageName)
        var targetRenderer = rendererForPage(pageName)
        changingPage = true
        activePageName = pageName
        if (sourceRenderer && targetRenderer && sourceRenderer !== targetRenderer)
            targetRenderer.copyCameraStateFrom(sourceRenderer)
        if (pageName === "preview") {
            previewDock.setAsCurrentTab()
            previewPage.renderer.activateView()
            if (updateEnvironment && MenuData.currentEnvironment !== "slicing") {
                ActionManager.triggerAction("environment.switch", {
                    environment: "slicing"
                })
            }
        } else if (pageName === "realistic") {
            realisticDock.setAsCurrentTab()
            realisticPage.renderer.activateView()
            if (updateEnvironment && MenuData.currentEnvironment !== "normal") {
                ActionManager.triggerAction("environment.switch", {
                    environment: "normal"
                })
            }
        } else {
            prepareDock.setAsCurrentTab()
            preparePage.renderer.activateView()
            if (updateEnvironment && MenuData.currentEnvironment !== "normal") {
                ActionManager.triggerAction("environment.switch", {
                    environment: "normal"
                })
            }
        }
        changingPage = false
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

            Prepare3DPrintPage {
                id: preparePage
                workspaceContext: sharedWorkspaceContext
                renderActive: root.dockReady && root.activePageName === "prepare"
                onWorkspaceRequested: function(workspaceName) {
                    root.activatePage(workspaceName, true)
                }
            }
        }

        DockWidget {
            id: previewDock
            uniqueName: "gplatform.print.preview"
            title: qsTr("Preview")
            options: KDDW.KDDockWidgets.DockWidgetOption_NotClosable

            Preview3DPrintPage {
                id: previewPage
                workspaceContext: sharedWorkspaceContext
                renderActive: root.dockReady && root.activePageName === "preview"
                onWorkspaceRequested: function(workspaceName) {
                    root.activatePage(workspaceName, true)
                }
            }
        }

        DockWidget {
            id: realisticDock
            uniqueName: "gplatform.print.realistic"
            title: qsTr("Realistic")
            options: KDDW.KDDockWidgets.DockWidgetOption_NotClosable

            Realistic3DPrintPage {
                id: realisticPage
                workspaceContext: sharedWorkspaceContext
                renderActive: root.dockReady &&
                              root.activePageName === "realistic"
                onWorkspaceRequested: function(workspaceName) {
                    root.activatePage(workspaceName, true)
                }
            }
        }

        Component.onCompleted: {
            SmoothUIDock.setCustomTabBarComponent(printWorkspaceTabBar)
            addDockWidgetAsTab(prepareDock)
            prepareDock.addDockWidgetAsTab(previewDock)
            prepareDock.addDockWidgetAsTab(realisticDock)
            dockReady = true
            Qt.callLater(function() {
                root.activatePage(
                    root.pageForEnvironment(MenuData.currentEnvironment),
                    false)
            })
        }
    }

    PillButton {
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
                             Theme.accentColor.defaultBrushFor(), 0.14)
                       : Theme.res.subtleFillColorSecondary

                Icon {
                    anchors.centerIn: parent
                    width: 19
                    height: 19
                    source: accountWorkspaceButton.signedIn
                            ? FluentIcons.graph_ContactSolid
                            : FluentIcons.graph_Contact
                    color: accountWorkspaceButton.signedIn
                           ? Theme.accentColor.defaultBrushFor()
                           : Theme.res.textFillColorPrimary
                }
            }

            Label {
                text: accountWorkspaceButton.signedIn
                      ? qsTr("Account") : qsTr("Sign In")
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.res.textFillColorPrimary
            }

            Icon {
                visible: accountWorkspaceButton.signedIn
                Layout.preferredWidth: visible ? 12 : 0
                Layout.preferredHeight: visible ? 12 : 0
                source: FluentIcons.graph_ChevronDown
                color: Theme.res.textFillColorSecondary
            }
        }

        ToolTip {
            visible: accountWorkspaceButton.hovered
            delay: Theme.tooltipDelay
            text: qsTr("Accounts and Services")
        }
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
        printerControlOpen: printerControlPopup.opened

        onMachinePopupOpening: printerControlPopup.close()
        onPrinterControlRequested: root.togglePrinterControl()
        onAddModelRequested: Global.starter.chooseModel({})
        onSliceRequested: ActionManager.triggerAction("model.sliceAll", {})
        onExportRequested: function(formatId) {
            if (previewPage.workspaceContent)
                previewPage.workspaceContent.requestPrintOutputExport(formatId)
        }
        onResliceRequested: {
            if (previewPage.workspaceContent)
                previewPage.workspaceContent.triggerResliceCurrentPreview()
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
        onStopPrintRequested: PrinterService.cancelPrint()
        onDismissJobStatusRequested: PrinterService.dismissJobStatus()
    }

    PrinterControlPopup {
        id: printerControlPopup
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
