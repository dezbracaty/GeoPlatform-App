import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

PanelSurface {
    id: root

    SmoothUI.theme: Theme.of(root)

    property string workspaceName: "normal"
    property Item popupHost: null
    property int objectCount: 0
    property string selectedFileName: ""
    property bool hasSelection: false
    property bool printerControlOpen: false

    property alias printerAnchor: deviceStatus

    readonly property int jobState: PrinterService.jobState
    readonly property bool previewMode: workspaceName === "slicing"
    readonly property bool printingMode: PrinterService.printing
    readonly property bool transferringMode: PrinterService.transferState === PrinterService.TransferUploading
    readonly property bool startingMode: PrinterService.transferState === PrinterService.TransferStarting
    readonly property bool pausingMode: jobState === PrinterService.Pausing
    readonly property bool pausedMode: jobState === PrinterService.Paused
    readonly property bool resumingMode: jobState === PrinterService.Resuming
    readonly property bool stoppingMode: jobState === PrinterService.Stopping
    readonly property bool jobCompleted: jobState === PrinterService.Completed
    readonly property bool jobFailed: jobState === PrinterService.JobError
    readonly property bool transferUnknown: PrinterService.commandOutcomeUnknown
        || PrinterService.transferState === PrinterService.TransferUnknown
    readonly property bool transferUploaded: PrinterService.transferState === PrinterService.TransferUploaded
    readonly property bool transferFailed: PrinterService.transferState === PrinterService.TransferFailed
    readonly property bool terminalJobVisible:
        transferUnknown || (!printingMode && !transferringMode && !startingMode
                            && (transferFailed || transferUploaded || jobCompleted || jobFailed))
    readonly property bool printControlsVisible:
        jobState === PrinterService.Printing || jobState === PrinterService.Paused
    readonly property real workflowProgress: transferringMode
        ? PrinterService.transferProgress : PrinterService.printProgress / 100
    readonly property real availableWidth: popupHost ? popupHost.width : width
    readonly property bool narrow: availableWidth < 1280
    readonly property real statusTextMaximumWidth: narrow ? 176 : 300
    readonly property real progressContentMinimumWidth: narrow ? 220 : 300
    readonly property int preferredWidth: Math.ceil(commandLayout.implicitWidth + 20)
    readonly property int effectiveObjectCount: objectCount
    readonly property bool previewReady: SlicingPreviewBridge.printOutputReady
    readonly property bool previewDirty:
        previewReady && SliceSettingsBridge.hasPendingReslice
    readonly property bool previewSendReady:
        previewReady && !previewDirty
        && !PrinterService.transferBusy && !PrinterService.printing
        && !taskBusy && !SlicingPreviewBridge.isLoading
        && (!deviceConnected || (PrinterService.supportsUpload && supportedPreviewFormat.length > 0))
    readonly property string supportedPreviewFormat: {
        // Read notify-backed readiness as canExportPrintOutput is an invokable.
        const ready = SlicingPreviewBridge.printOutputReady
        const packageReady = SlicingPreviewBridge.printPackageExportAvailable
        const formats = PrinterService.supportedPrintFormats
        for (let i = 0; i < formats.length; ++i) {
            if ((formats[i] === "gcode" || formats[i] === "gcode-3mf")
                    && ready && SlicingPreviewBridge.canExportPrintOutput(formats[i]))
                return formats[i]
        }
        return ""
    }
    readonly property bool canReslice: SlicingPreviewBridge.resliceAvailable
    readonly property string configurationError: SliceSettingsBridge.configurationError
    readonly property bool configurationInvalid: configurationError.length > 0
    readonly property bool taskBusy: TaskStateNotifier.isBusy
    readonly property real taskProgress: TaskStateNotifier.currentProgress
    readonly property int activeTaskCount: TaskStateNotifier.taskCount
    readonly property string taskDescription: TaskStateNotifier.currentDescription
    readonly property int connectionState: PrinterService.connectionState
    readonly property bool deviceConnected: PrinterService.connected
    readonly property bool jobControllable: PrinterService.jobControllable
    readonly property string currentJobName:
        PrinterService.jobFileName
        || SlicingPreviewBridge.defaultPrintOutputFileName(supportedPreviewFormat || "gcode")
        || qsTr("Print Job")
    signal machinePopupOpening()
    signal printerControlRequested()
    signal addModelRequested()
    signal sliceRequested()
    signal exportRequested(string formatId)
    signal resliceRequested()
    signal printRequested()
    signal pausePrintRequested()
    signal resumePrintRequested()
    signal stopPrintRequested()
    signal dismissJobStatusRequested()

    function closeMachinePopup() {
        machineSelector.closePopup()
    }

    function openMachineSettings(params) {
        return machineSelector.openSliceSettings(params || {})
    }

    function workflowTitle() {
        if (transferringMode)
            return qsTr("Transferring print job")
        if (startingMode)
            return qsTr("Starting print")
        if (pausingMode)
            return qsTr("Pausing print")
        if (pausedMode)
            return qsTr("Print paused")
        if (resumingMode)
            return qsTr("Resuming print")
        if (stoppingMode)
            return qsTr("Stopping print")
        return qsTr("Printing")
    }

    function workflowDetail() {
        if (transferringMode) {
            return qsTr("%1% transferred").arg(
                        Math.round(workflowProgress * 100))
        }
        if (startingMode)
            return qsTr("Waiting for the printer to begin")
        if (pausingMode)
            return qsTr("Waiting for the printer to pause")
        if (resumingMode)
            return qsTr("Waiting for the printer to resume")
        if (stoppingMode)
            return qsTr("Waiting for the printer to stop")
        return qsTr("%1% · %2 remaining")
                .arg(Math.round(workflowProgress * 100))
                .arg(PrinterService.remainingTime)
    }

    height: PrintWorkspaceStyle.globalCommandBarHeight
    implicitWidth: preferredWidth
    surfaceRadius: PrintWorkspaceStyle.toolbarRadius
    elevation: 2

    RowLayout {
        id: commandLayout
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 6

        MachineSelectionBar {
            id: machineSelector
            Layout.fillWidth: true
            Layout.preferredWidth: implicitWidth
            Layout.minimumWidth: minimumContentWidth
            Layout.maximumWidth: implicitWidth
            condensedPresetSummary: root.narrow
            Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
            compact: true
            embedded: true
            popupAbove: true
            popupHost: root.popupHost
            popupAnchor: machineSelector
            popupVerticalAnchor: root
            onPopupOpening: root.machinePopupOpening()
        }

        PrinterStatusBar {
            id: deviceStatus
            Layout.fillWidth: true
            Layout.preferredWidth: implicitWidth
            Layout.minimumWidth: minimumContentWidth
            Layout.maximumWidth: implicitWidth
            Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
            compact: true
            embedded: true
            agentAvailable: PrinterService.agentAvailable
            hasSelectedDevice: PrinterService.selectedDeviceId.length > 0
            selectedDeviceOnline: PrinterService.selectedDeviceOnline
            loading: root.connectionState === PrinterService.Connecting
            serviceError: root.connectionState === PrinterService.ConnectionError
            accessRequired: PrinterService.deviceAccessRequired
            errorText: PrinterService.connectionError || PrinterService.agentError
            printerName: PrinterService.selectedDeviceName
            printerIP: PrinterService.selectedDeviceIp
            nozzleTemp: PrinterService.nozzleTemperature
            nozzleTargetTemp: PrinterService.targetNozzleTemperature
            bedTemp: PrinterService.bedTemperature
            bedTargetTemp: PrinterService.targetBedTemperature
            isPrinting: PrinterService.printing
            printProgress: PrinterService.printProgress
            controlPanelOpen: root.printerControlOpen
            onControlPanelRequested: root.printerControlRequested()
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 30
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            color: PrintWorkspaceStyle.divider(root.SmoothUI.dark)
        }

        Loader {
            id: statusContentLoader
            Layout.fillWidth: true
            Layout.preferredWidth: item ? item.implicitWidth : 0
            Layout.minimumWidth: item ? item.minimumContentWidth : 0
            Layout.maximumWidth: Layout.preferredWidth
            Layout.fillHeight: true
            sourceComponent: root.terminalJobVisible
                             ? terminalJobContent
                             : (root.printingMode || root.transferringMode || root.startingMode
                                ? printingContent
                                : (root.taskBusy
                                   ? taskContent
                                   : (root.previewMode
                                      ? previewContent : normalContent)))
        }
    }

    Component {
        id: normalContent

        Item {
            implicitWidth: normalLayout.implicitWidth
            readonly property real minimumContentWidth: normalLayout.Layout.minimumWidth

            RowLayout {
                id: normalLayout
                anchors.fill: parent
                spacing: 8

                Icon {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: root.configurationInvalid
                            ? FluentIcons.graph_ErrorBadge
                            : (root.effectiveObjectCount > 0
                               ? FluentIcons.graph_Completed
                               : FluentIcons.graph_Page)
                    color: root.configurationInvalid
                           ? root.SmoothUI.theme.res.systemFillColorCritical
                           : (root.effectiveObjectCount > 0
                              ? root.SmoothUI.theme.res.systemFillColorSuccess
                              : root.SmoothUI.theme.res.textFillColorTertiary)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: Math.min(
                                               root.statusTextMaximumWidth,
                                               Math.max(normalTitle.implicitWidth,
                                                        root.narrow ? 0 : normalDetail.implicitWidth))
                    Layout.maximumWidth: Layout.preferredWidth
                    spacing: 0

                    Label {
                        id: normalTitle
                        Layout.fillWidth: true
                        text: root.configurationInvalid
                              ? qsTr("Slicing configuration needs attention")
                              : (root.effectiveObjectCount > 0
                                 ? qsTr("%1 model(s) prepared").arg(root.effectiveObjectCount)
                                 : qsTr("No models on the build plate"))
                        elide: Text.ElideRight
                        color: root.configurationInvalid
                               ? root.SmoothUI.theme.res.systemFillColorCritical
                               : root.SmoothUI.theme.res.textFillColorPrimary
                        font: Typography.bodyStrong
                    }

                    Label {
                        id: normalDetail
                        Layout.fillWidth: true
                        visible: !root.narrow
                        text: root.configurationInvalid
                              ? root.configurationError
                              : (root.hasSelection && root.selectedFileName.length > 0
                                 ? root.selectedFileName
                                 : (root.effectiveObjectCount > 0
                                    ? qsTr("Ready for slicing")
                                    : qsTr("Add a model to start the print workflow")))
                        elide: Text.ElideMiddle
                        color: root.SmoothUI.theme.res.textFillColorTertiary
                        font: Typography.caption
                    }
                }

                RowLayout {
                    id: normalActions
                    Layout.fillWidth: false
                    Layout.minimumWidth: implicitWidth
                    Layout.maximumWidth: implicitWidth
                    spacing: PrintWorkspaceStyle.space4

                    Button {
                        id: parameterButton
                        visible: root.effectiveObjectCount > 0
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.narrow ? "" : qsTr("Parameters")
                        icon.name: root.narrow ? FluentIcons.graph_Settings : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        onClicked: root.openMachineSettings()

                        ToolTip {
                            text: qsTr("Slicing Parameters")
                            visible: parent.hovered && root.narrow
                            delay: Theme.tooltipDelay
                        }
                    }

                    Button {
                        id: normalPrimaryButton
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.narrow
                              ? ""
                              : (root.effectiveObjectCount > 0
                                 ? qsTr("Start Slicing") : qsTr("Add Model"))
                        icon.name: root.narrow
                                   ? (root.effectiveObjectCount > 0
                                      ? FluentIcons.graph_MapLayers
                                      : FluentIcons.graph_Add)
                                   : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        highlighted: true
                        enabled: !root.configurationInvalid || root.effectiveObjectCount === 0
                        onClicked: root.effectiveObjectCount > 0
                                   ? root.sliceRequested()
                                   : root.addModelRequested()

                        ToolTip {
                            text: root.effectiveObjectCount > 0
                                  ? qsTr("Start Slicing") : qsTr("Add Model")
                            visible: parent.hovered && root.narrow
                            delay: Theme.tooltipDelay
                        }
                    }
                }
            }
        }
    }

    Component {
        id: taskContent

        Item {
            implicitWidth: taskLayout.implicitWidth
            readonly property real minimumContentWidth: taskLayout.Layout.minimumWidth

            RowLayout {
                id: taskLayout
                anchors.fill: parent
                spacing: 10

                Icon {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: FluentIcons.graph_Processing
                    color: Theme.accentColor.defaultBrushFor(root.SmoothUI.dark)

                    RotationAnimator on rotation {
                        running: root.taskBusy
                        from: 0
                        to: 360
                        duration: 1000
                        loops: Animation.Infinite
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: root.progressContentMinimumWidth
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: root.progressContentMinimumWidth
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        text: root.taskDescription || qsTr("Processing…")
                        elide: Text.ElideRight
                        color: root.SmoothUI.theme.res.textFillColorPrimary
                        font: Typography.bodyStrong
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 5
                        from: 0
                        to: 1
                        value: Math.max(0, Math.min(1, root.taskProgress))
                        indeterminate: root.taskProgress <= 0
                    }
                }

                Label {
                    visible: root.taskProgress > 0
                    text: Math.round(root.taskProgress * 100) + "%"
                    horizontalAlignment: Text.AlignRight
                    color: root.SmoothUI.theme.res.textFillColorSecondary
                    font: Typography.caption
                }

                Label {
                    visible: root.activeTaskCount > 1 && !root.narrow
                    text: qsTr("%1 tasks").arg(root.activeTaskCount)
                    color: root.SmoothUI.theme.res.textFillColorTertiary
                    font: Typography.caption
                }
            }
        }
    }

    Component {
        id: previewContent

        Item {
            implicitWidth: previewLayout.implicitWidth
            readonly property real minimumContentWidth: previewLayout.Layout.minimumWidth

            RowLayout {
                id: previewLayout
                anchors.fill: parent
                spacing: 8

                Icon {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: root.previewDirty || !root.previewReady
                            ? FluentIcons.graph_Warning
                            : FluentIcons.graph_Completed
                    color: root.previewDirty
                           ? root.SmoothUI.theme.res.systemFillColorCaution
                           : (root.previewReady
                              ? root.SmoothUI.theme.res.systemFillColorSuccess
                              : root.SmoothUI.theme.res.textFillColorTertiary)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: Math.min(
                                               root.statusTextMaximumWidth,
                                               Math.max(previewTitle.implicitWidth,
                                                        previewHint.visible ? previewHint.implicitWidth
                                                                            : (root.narrow ? 0 : previewDetail.implicitWidth)))
                    Layout.maximumWidth: Layout.preferredWidth
                    spacing: 0

                    Label {
                        id: previewTitle
                        Layout.fillWidth: true
                        text: root.previewDirty
                              ? qsTr("Settings changed · Reslice required")
                              : (root.previewReady
                                 ? qsTr("G-code is ready")
                                 : qsTr("No G-code generated"))
                        elide: Text.ElideRight
                        color: root.SmoothUI.theme.res.textFillColorPrimary
                        font: Typography.bodyStrong
                    }

                    PreviewInspectionHint {
                        id: previewHint
                        Layout.fillWidth: true
                        visible: root.previewReady
                        compact: root.narrow
                    }

                    Label {
                        id: previewDetail
                        Layout.fillWidth: true
                        visible: !root.previewReady && !root.narrow
                        text: root.previewDirty
                              ? qsTr("The current output no longer matches the print settings")
                              : (root.previewReady
                                 ? SlicingPreviewBridge.defaultPrintOutputFileName(supportedPreviewFormat || "gcode")
                                 : qsTr("Slice a model before exporting or printing"))
                        elide: Text.ElideMiddle
                        color: root.SmoothUI.theme.res.textFillColorTertiary
                        font: Typography.caption
                    }
                }

                RowLayout {
                    id: previewActions
                    Layout.fillWidth: false
                    Layout.minimumWidth: implicitWidth
                    Layout.maximumWidth: implicitWidth
                    spacing: PrintWorkspaceStyle.space4

                    Button {
                        id: resliceButton
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.narrow ? "" : qsTr("Reslice")
                        icon.name: root.narrow ? FluentIcons.graph_Refresh : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        highlighted: root.previewDirty || !root.previewReady
                        enabled: root.canReslice
                        onClicked: root.resliceRequested()

                        ToolTip {
                            text: qsTr("Reslice")
                            visible: parent.hovered && root.narrow
                            delay: Theme.tooltipDelay
                        }
                    }

                    DropDownButton {
                        id: exportButton
                        objectName: "printOutputExportButton"
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.narrow ? "" : qsTr("Export")
                        icon.name: root.narrow ? FluentIcons.graph_Export : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        visible: root.previewReady
                        enabled: root.previewReady && !root.previewDirty
                        menu.width: 224
                        menu.objectName: "printOutputExportMenu"

                        ToolTip {
                            text: qsTr("Export")
                            visible: parent.hovered && root.narrow
                            delay: Theme.tooltipDelay
                        }

                        MenuItem {
                            text: qsTr("G-code Files")
                            icon.name: FluentIcons.graph_Document
                            enabled: !root.previewDirty
                                     && SlicingPreviewBridge.canExportPrintOutput("gcode")
                            onTriggered: root.exportRequested("gcode")
                        }

                        MenuItem {
                            text: qsTr("Slicing Job (Gcode.3MF)")
                            icon.name: FluentIcons.graph_ZipFolder
                            enabled: !root.previewDirty
                                     && SlicingPreviewBridge.canExportPrintOutput("gcode-3mf")
                            onTriggered: root.exportRequested("gcode-3mf")
                        }
                    }

                    Button {
                        id: printButton
                        objectName: "printerSendButton"
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.narrow
                              ? ""
                              : (root.deviceConnected
                                 ? qsTr("Send to Printer") : qsTr("Connect to Send"))
                        icon.name: root.narrow ? FluentIcons.graph_Printer3D : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        visible: root.previewReady
                        highlighted: root.previewSendReady
                        enabled: root.previewSendReady
                        onClicked: root.printRequested()

                        ToolTip {
                            text: root.deviceConnected && !root.supportedPreviewFormat
                                  ? qsTr("The selected printer does not support an available preview format.")
                                  : root.deviceConnected
                                    ? qsTr("Send to Printer") : qsTr("Connect to Send")
                            visible: parent.hovered && (root.narrow || !root.supportedPreviewFormat)
                            delay: Theme.tooltipDelay
                        }
                    }
                }
            }
        }
    }

    Component {
        id: printingContent

        Item {
            implicitWidth: printingLayout.implicitWidth
            readonly property real minimumContentWidth: printingLayout.Layout.minimumWidth

            RowLayout {
                id: printingLayout
                anchors.fill: parent
                spacing: 10

                Icon {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: root.transferringMode
                            ? FluentIcons.graph_Upload
                            : (root.pausedMode
                               ? FluentIcons.graph_Pause
                               : FluentIcons.graph_Print)
                    color: root.pausedMode
                           ? root.SmoothUI.theme.res.systemFillColorCaution
                           : Theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: root.progressContentMinimumWidth
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: root.progressContentMinimumWidth
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            elide: Text.ElideRight
                            text: root.workflowTitle()
                            color: root.SmoothUI.theme.res.textFillColorPrimary
                            font: Typography.bodyStrong
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.currentJobName
                            elide: Text.ElideMiddle
                            color: root.SmoothUI.theme.res.textFillColorTertiary
                            font: Typography.caption
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            elide: Text.ElideRight
                            text: root.workflowDetail()
                            horizontalAlignment: Text.AlignRight
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                            font: Typography.caption
                        }
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 5
                        from: 0
                        to: 1
                        value: Math.max(0, Math.min(1, root.workflowProgress))
                        indeterminate: root.startingMode
                    }
                }

                RowLayout {
                    id: printingActions
                    visible: root.printControlsVisible
                    Layout.fillWidth: false
                    Layout.minimumWidth: implicitWidth
                    Layout.maximumWidth: implicitWidth
                    spacing: PrintWorkspaceStyle.space4

                    Button {
                        id: stopPrintButton
                        objectName: "printerCancelButton"
                        visible: root.printControlsVisible
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.narrow ? "" : qsTr("Stop")
                        icon.name: root.narrow ? FluentIcons.graph_Stop : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        enabled: PrinterService.supportsCancel && root.jobControllable
                        onClicked: root.stopPrintRequested()

                        ToolTip {
                            text: qsTr("Stop")
                            visible: parent.hovered && root.narrow
                            delay: Theme.tooltipDelay
                        }
                    }

                    Button {
                        id: pausePrintButton
                        objectName: "printerPauseResumeButton"
                        visible: root.printControlsVisible
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.narrow
                              ? ""
                              : (root.pausedMode ? qsTr("Continue") : qsTr("Pause"))
                        icon.name: root.narrow
                                   ? (root.pausedMode
                                      ? FluentIcons.graph_Play : FluentIcons.graph_Pause)
                                   : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        highlighted: true
                        enabled: PrinterService.supportsPauseResume && root.jobControllable
                        onClicked: root.pausedMode
                                   ? root.resumePrintRequested()
                                   : root.pausePrintRequested()

                        ToolTip {
                            text: root.pausedMode ? qsTr("Continue") : qsTr("Pause")
                            visible: parent.hovered && root.narrow
                            delay: Theme.tooltipDelay
                        }
                    }
                }
            }
        }
    }

    Component {
        id: terminalJobContent

        Item {
            implicitWidth: terminalLayout.implicitWidth
            readonly property real minimumContentWidth: terminalLayout.Layout.minimumWidth

            RowLayout {
                id: terminalLayout
                anchors.fill: parent
                spacing: 10

                Icon {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    source: (root.jobFailed || root.transferFailed || root.transferUnknown)
                            ? FluentIcons.graph_ErrorBadge
                            : FluentIcons.graph_CompletedSolid
                    color: (root.jobFailed || root.transferFailed || root.transferUnknown)
                           ? root.SmoothUI.theme.res.systemFillColorCritical
                           : root.SmoothUI.theme.res.systemFillColorSuccess
                }

                ColumnLayout {
                    id: terminalTextColumn
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: Math.min(
                                               Math.max(terminalTitle.implicitWidth,
                                                        terminalDetail.implicitWidth),
                                               root.narrow ? 200 : 300)
                    Layout.maximumWidth: Layout.preferredWidth
                    spacing: 0

                    Label {
                        id: terminalTitle
                        Layout.fillWidth: true
                        text: root.transferUnknown ? qsTr("Printer operation unconfirmed")
                              : root.transferFailed ? qsTr("Transfer failed")
                              : root.transferUploaded ? qsTr("Upload completed")
                              : root.jobFailed ? qsTr("Print failed") : qsTr("Print completed")
                        elide: Text.ElideRight
                        color: (root.jobFailed || root.transferFailed || root.transferUnknown)
                               ? root.SmoothUI.theme.res.systemFillColorCritical
                               : root.SmoothUI.theme.res.textFillColorPrimary
                        font: Typography.bodyStrong
                    }

                    Label {
                        id: terminalDetail
                        Layout.fillWidth: true
                        text: root.transferUnknown
                              ? qsTr("The operation may already have executed. Check the printer before acknowledging. Do not retry automatically.")
                              : (root.jobFailed || root.transferFailed)
                              ? (PrinterService.jobError
                                 || qsTr("Open printer details for more information"))
                              : root.currentJobName
                        elide: Text.ElideMiddle
                        color: root.SmoothUI.theme.res.textFillColorTertiary
                        font: Typography.caption
                    }
                }

                RowLayout {
                    id: terminalActions
                    Layout.fillWidth: false
                    Layout.minimumWidth: implicitWidth
                    Layout.maximumWidth: implicitWidth
                    spacing: PrintWorkspaceStyle.space4

                    Button {
                        id: viewPrinterDetailsButton
                        visible: root.jobFailed || root.transferFailed || root.transferUnknown
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.narrow ? "" : qsTr("View Details")
                        icon.name: root.narrow ? FluentIcons.graph_Info : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        onClicked: root.printerControlRequested()

                        ToolTip {
                            text: qsTr("View Details")
                            visible: parent.hovered && root.narrow
                            delay: Theme.tooltipDelay
                        }
                    }

                    Button {
                        id: retryPrintButton
                        visible: (root.jobFailed || root.transferFailed) && !root.transferUnknown && root.previewSendReady
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.narrow ? "" : qsTr("Retry")
                        icon.name: root.narrow ? FluentIcons.graph_Refresh : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        highlighted: true
                        onClicked: root.printRequested()

                        ToolTip {
                            text: qsTr("Retry")
                            visible: parent.hovered && root.narrow
                            delay: Theme.tooltipDelay
                        }
                    }

                    Button {
                        id: printAgainButton
                        visible: root.jobCompleted && !root.transferUnknown && root.previewSendReady
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.narrow ? "" : qsTr("Print Again")
                        icon.name: root.narrow ? FluentIcons.graph_Print : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        highlighted: true
                        onClicked: root.printRequested()

                        ToolTip {
                            text: qsTr("Print Again")
                            visible: parent.hovered && root.narrow
                            delay: Theme.tooltipDelay
                        }
                    }

                    Button {
                        id: dismissJobButton
                        Layout.fillWidth: false
                        Layout.minimumWidth: implicitWidth
                        Layout.preferredWidth: implicitWidth
                        Layout.maximumWidth: implicitWidth
                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                        text: root.transferUnknown ? qsTr("Acknowledge") : (root.narrow ? "" : qsTr("Dismiss"))
                        icon.name: root.narrow ? FluentIcons.graph_Cancel : ""
                        icon.width: 14
                        icon.height: 14
                        font: Typography.caption
                        onClicked: root.transferUnknown
                                   ? root.printerControlRequested()
                                   : root.dismissJobStatusRequested()

                        ToolTip {
                            text: qsTr("Dismiss")
                            visible: parent.hovered && root.narrow
                            delay: Theme.tooltipDelay
                        }
                    }
                }

            }
        }
    }

}
