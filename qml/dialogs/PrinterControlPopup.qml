import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

AppPopup {
    id: control
    windowEscapeEnabled: true
    SmoothUI.theme: Theme.of(control)

    property Item popupHost: null
    property string accessCredential: ""
    property bool selectionEnabled: false
    signal flashforgeAccountRequested()

    readonly property bool scanning:
        PrinterService.discoveryState === PrinterService.DiscoveryScanning
    readonly property bool hasSelection: PrinterService.selectedDeviceId.length > 0
    readonly property bool preparing:
        PrinterService.connectionState === PrinterService.Connecting
    readonly property bool activeJob:
        PrinterService.jobState === PrinterService.Printing
        || PrinterService.jobState === PrinterService.Pausing
        || PrinterService.jobState === PrinterService.Paused
        || PrinterService.jobState === PrinterService.Resuming
        || PrinterService.jobState === PrinterService.Stopping

    readonly property bool hasSendFormat: {
        const ready = SlicingPreviewBridge.printOutputReady
        const packageReady = SlicingPreviewBridge.printPackageExportAvailable
        const formats = PrinterService.supportedPrintFormats
        for (let i = 0; i < formats.length; ++i) {
            if (ready && (formats[i] === "gcode" || formats[i] === "gcode-3mf")
                    && SlicingPreviewBridge.canExportPrintOutput(formats[i]))
                return true
        }
        return false
    }

    parent: popupHost
    preferredWidth: PrintWorkspaceStyle.printerDevicePopoverWidth
    preferredHeight: PrintWorkspaceStyle.printerDevicePopoverHeight
    title: qsTr("Printing Devices")

    // This popup is parented to the workspace, so clamp in workspace coordinates.
    // Ancestor layout changes are not dependencies of mapToItem(). Refresh after resize.
    x: {
        const revision = positionRevision
        if (!parent) return 0
        const margin = PrintWorkspaceStyle.space16
        const anchorX = !centerHorizontally && popupAnchor
            ? popupAnchor.mapToItem(parent, popupAnchor.width / 2, 0).x
            : parent.width / 2
        return Math.round(Math.max(margin, Math.min(anchorX - width / 2, parent.width - width - margin)))
    }
    y: {
        const revision = positionRevision
        if (!parent) return 0
        const margin = PrintWorkspaceStyle.space16
        const anchor = popupVerticalAnchor ? popupVerticalAnchor : popupAnchor
        let preferredY = (parent.height - height) / 2
        if (anchor) {
            const position = anchor.mapToItem(parent, 0, popupAbove ? 0 : anchor.height)
            preferredY = popupAbove
                ? position.y - height - PrintWorkspaceStyle.machinePopoverOffset
                : position.y + PrintWorkspaceStyle.machinePopoverOffset
        }
        return Math.round(Math.max(margin, Math.min(preferredY, parent.height - height - margin)))
    }
    Connections {
        target: control.popupHost
        function onWidthChanged() { Qt.callLater(control.reposition) }
        function onHeightChanged() { Qt.callLater(control.reposition) }
        function onXChanged() { Qt.callLater(control.reposition) }
        function onYChanged() { Qt.callLater(control.reposition) }
    }
    Connections {
        target: control.popupAnchor
        function onXChanged() { Qt.callLater(control.reposition) }
        function onYChanged() { Qt.callLater(control.reposition) }
        function onWidthChanged() { Qt.callLater(control.reposition) }
        function onHeightChanged() { Qt.callLater(control.reposition) }
    }
    Connections {
        target: control.popupVerticalAnchor
        function onXChanged() { Qt.callLater(control.reposition) }
        function onYChanged() { Qt.callLater(control.reposition) }
        function onWidthChanged() { Qt.callLater(control.reposition) }
        function onHeightChanged() { Qt.callLater(control.reposition) }
    }

    onOpened: {
        accessCredential = ""
        selectionEnabled = false
        selectionGuard.restart()
        if (PrinterService.discoveryState === PrinterService.DiscoveryIdle)
            PrinterService.startDiscovery()
        Qt.callLater(control.reposition)
    }
    onClosed: {
        selectionEnabled = false
        PrinterService.stopCameraStream()
    }

    ContentDialog {
        id: moonrakerConnection
        objectName: "moonrakerConnectionDialog"
        title: qsTr("Connect to Moonraker")
        width: Math.min(460, Math.max(0, parent ? parent.width - 24 : 460))
        onOpened: moonrakerAddress.forceActiveFocus()
        onClosed: moonrakerKey.text = ""
        ColumnLayout {
            width: moonrakerConnection.availableWidth
            spacing: 12
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: qsTr("Enter a printer IP address or HTTP(S) URL, with an optional port. HTTP is added if omitted. Use HTTPS on untrusted networks.")
                wrapMode: Text.Wrap
            }
            TextField {
                id: moonrakerAddress
                objectName: "moonrakerEndpoint"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                placeholderText: "http://printer.local:7125"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            }
            TextField {
                id: moonrakerKey
                objectName: "moonrakerApiKey"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                placeholderText: qsTr("API key (optional on trusted networks)")
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: qsTr("The address and API key are kept only for this session. They are not saved to disk.")
                wrapMode: Text.Wrap
            }
            Label {
                id: moonrakerConfigError
                color: moonrakerConnection.SmoothUI.theme.res.systemFillColorCritical
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: text.length > 0
                wrapMode: Text.Wrap
            }
        }
        footer: DialogButtonBox {
            alignment: Qt.AlignRight
            Button {
                implicitWidth: Math.max(96, implicitContentWidth + leftPadding + rightPadding)
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                text: qsTr("Cancel")
                onClicked: moonrakerConnection.close()
            }
            Button {
                implicitWidth: Math.max(96, implicitContentWidth + leftPadding + rightPadding)
                objectName: "moonrakerConnectButton"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                text: qsTr("Connect")
                highlighted: true
                enabled: PrinterService.canConfigureBackend && moonrakerAddress.text.trim().length > 0
                onClicked: {
                    if (PrinterService.connectMoonraker(moonrakerAddress.text, moonrakerKey.text))
                        moonrakerConnection.close()
                    else
                        moonrakerConfigError.text = PrinterService.connectionError
                }
            }
        }
    }

    Timer {
        id: selectionGuard
        interval: 180
        onTriggered: control.selectionEnabled = true
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 252
            Layout.fillHeight: true
            color: PrintWorkspaceStyle.secondarySurface(control.SmoothUI.dark)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            text: qsTr("Devices")
                            font: Typography.bodyStrong
                            color: control.SmoothUI.theme.res.textFillColorPrimary
                        }
                        Label {
                            text: qsTr("Online and offline")
                            font: Typography.caption
                            color: control.SmoothUI.theme.res.textFillColorSecondary
                        }
                    }

                    Button {
                        text: control.scanning ? qsTr("Scanning") : qsTr("Refresh")
                        icon.name: FluentIcons.graph_Refresh
                        icon.width: 16
                        icon.height: 16
                        visible: PrinterService.supportsDiscovery
                        enabled: PrinterService.agentAvailable && !control.scanning
                                 && !control.preparing
                        onClicked: PrinterService.startDiscovery()
                    }
                }

                ConstrainedInfoBar {
                    Layout.fillWidth: true
                    visible: !PrinterService.agentAvailable
                             || PrinterService.discoveryState === PrinterService.DiscoveryError
                             || PrinterService.discoveryState === PrinterService.DiscoveryWarning
                    severity: PrinterService.discoveryState === PrinterService.DiscoveryWarning
                              ? InfoBarType.Warning : InfoBarType.Error
                    title: !PrinterService.agentAvailable
                           ? qsTr("Device service unavailable")
                           : PrinterService.discoveryState === PrinterService.DiscoveryWarning
                             ? qsTr("Refresh incomplete") : qsTr("Scan failed")
                    message: !PrinterService.agentAvailable
                             ? PrinterService.agentError : PrinterService.discoveryError
                    closable: false
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: deviceList
                        anchors.fill: parent
                        clip: true
                        spacing: 4
                        cacheBuffer: 2048
                        model: PrinterService.devices
                        visible: count > 0
                        section.property: "availabilitySection"
                        section.criteria: ViewSection.FullString
                        section.delegate: Item {
                            required property string section
                            width: deviceList.width
                            height: 30

                            Label {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: parent.section === "online"
                                      ? qsTr("Online") : qsTr("Offline")
                                font: Typography.bodyStrong
                                color: control.SmoothUI.theme.res.textFillColorSecondary
                            }
                        }

                        delegate: ItemDelegate {
                            id: deviceDelegate
                            objectName: "printerDevice." + deviceId
                            required property string deviceId
                            required property string name
                            required property string model
                            required property string ip
                            required property bool online
                            required property string state
                            required property int route

                            width: ListView.view ? ListView.view.width : 0
                            height: 58
                            highlighted: PrinterService.selectedDeviceId === deviceId
                            enabled: control.selectionEnabled && !control.preparing
                                     && !PrinterService.transferBusy && !control.activeJob
                            onClicked: {
                                control.accessCredential = ""
                                PrinterService.selectDevice(deviceId)
                            }

                            contentItem: RowLayout {
                                spacing: 9

                                Rectangle {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 34
                                    radius: PrintWorkspaceStyle.controlRadius
                                    color: deviceDelegate.highlighted
                                           ? Colors.withOpacity(control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark), 0.14)
                                           : control.SmoothUI.theme.res.cardBackgroundFillColorDefault

                                    Icon {
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        source: FluentIcons.graph_Printer3D
                                        color: deviceDelegate.online
                                               ? control.SmoothUI.theme.res.textFillColorPrimary
                                               : control.SmoothUI.theme.res.textFillColorDisabled
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Label {
                                        Layout.fillWidth: true
                                        text: deviceDelegate.name.length > 0
                                              ? deviceDelegate.name : deviceDelegate.deviceId
                                        elide: Text.ElideRight
                                        font: Typography.bodyStrong
                                        color: deviceDelegate.online
                                               ? control.SmoothUI.theme.res.textFillColorPrimary
                                               : control.SmoothUI.theme.res.textFillColorDisabled
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: [deviceDelegate.model,
                                               control.routeText(deviceDelegate.route)]
                                              .filter(function(value) {
                                                  return value.length > 0
                                              }).join(" · ")
                                        elide: Text.ElideRight
                                        font: Typography.caption
                                        color: control.SmoothUI.theme.res.textFillColorSecondary
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    radius: 4
                                    color: deviceDelegate.online
                                           ? control.SmoothUI.theme.res.systemFillColorSuccess
                                           : control.SmoothUI.theme.res.textFillColorDisabled
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width, 210)
                        spacing: 8
                        visible: deviceList.count === 0

                        ProgressRing {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            strokeWidth: 3
                            indeterminate: true
                            visible: control.scanning
                        }
                        Icon {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            visible: !control.scanning
                            source: FluentIcons.graph_Printer3D
                            color: control.SmoothUI.theme.res.textFillColorSecondary
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: control.scanning
                                  ? qsTr("Searching for devices") : qsTr("No devices found")
                            font: Typography.bodyStrong
                            color: control.SmoothUI.theme.res.textFillColorPrimary
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("Make sure the printer is powered on and on the same network")
                            font: Typography.caption
                            color: control.SmoothUI.theme.res.textFillColorSecondary
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Connection: %1").arg(PrinterService.backendName)
                    font: Typography.caption
                }
                Button {
                    objectName: "printerConfigureMoonrakerButton"
                    text: qsTr("Connect to Moonraker")
                    enabled: PrinterService.canConfigureBackend
                    onClicked: {
                        moonrakerConfigError.text = ""
                        moonrakerConnection.open()
                    }
                }
                Button {
                    objectName: "printerUseFlashforgeButton"
                    text: qsTr("Use Flashforge")
                    visible: PrinterService.backendName !== "Flashforge"
                    enabled: PrinterService.canConfigureBackend
                    onClicked: PrinterService.useFlashforge()
                }
                Button {
                    objectName: "printerControlFlashforgeAccountButton"
                    visible: PrinterService.supportsAccount
                    text: PrinterService.signedIn
                          ? qsTr("Flashforge account") : qsTr("Sign in to Flashforge")
                    icon.name: FluentIcons.graph_Contact
                    icon.width: 16
                    icon.height: 16
                    flat: true
                    onClicked: control.flashforgeAccountRequested()
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: control.SmoothUI.theme.res.dividerStrokeColorDefault
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 320)
                spacing: 10
                visible: !control.hasSelection

                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    source: FluentIcons.graph_Printer3D
                    color: control.SmoothUI.theme.res.textFillColorSecondary
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Select a printer")
                    font: Typography.subtitle
                    color: control.SmoothUI.theme.res.textFillColorPrimary
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Online printers open directly. Offline printers show their last known information.")
                    font: Typography.body
                    color: control.SmoothUI.theme.res.textFillColorSecondary
                }
            }

            ScrollView {
                id: printerDetailScroll
                objectName: "printerDetailScroll"
                clip: true
                rightPadding: 12
                anchors.fill: parent
                anchors.margins: 18
                visible: control.hasSelection
                contentWidth: availableWidth
                contentHeight: Math.max(availableHeight, printerDetailContent.implicitHeight)
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    id: printerDetailContent
                    width: printerDetailScroll.availableWidth
                    height: printerDetailScroll.contentHeight
                    spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: PrinterService.selectedDeviceName
                            elide: Text.ElideRight
                            font: Typography.subtitle
                            color: control.SmoothUI.theme.res.textFillColorPrimary
                        }
                        Label {
                            Layout.fillWidth: true
                            text: [PrinterService.selectedDeviceModel,
                                   control.routeText(PrinterService.selectedDeviceRoute),
                                   PrinterService.selectedDeviceIp].filter(function(value) {
                                       return value.length > 0
                                   }).join(" · ")
                            elide: Text.ElideRight
                            font: Typography.caption
                            color: control.SmoothUI.theme.res.textFillColorSecondary
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: stateLabel.implicitWidth + 18
                        Layout.preferredHeight: 26
                        radius: 13
                        color: PrinterService.connectionState === PrinterService.ConnectionError
                               ? control.SmoothUI.theme.res.systemFillColorCautionBackground
                               : (PrinterService.selectedDeviceOnline
                                  ? control.SmoothUI.theme.res.systemFillColorSuccessBackground
                                  : PrintWorkspaceStyle.secondarySurface(control.SmoothUI.dark))
                        Label {
                            id: stateLabel
                            anchors.centerIn: parent
                            text: control.deviceStateText()
                            font: Typography.caption
                            color: PrinterService.connectionState === PrinterService.ConnectionError
                                   ? control.SmoothUI.theme.res.systemFillColorCaution
                                   : (PrinterService.selectedDeviceOnline
                                      ? control.SmoothUI.theme.res.systemFillColorSuccess
                                      : control.SmoothUI.theme.res.textFillColorSecondary)
                        }
                    }
                }

                Rectangle {
                    id: videoFrame
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.round(width * 9 / 16)
                    radius: PrintWorkspaceStyle.menuRadius
                    color: control.SmoothUI.dark ? "#FF111214" : "#FF22262B"
                    clip: true

                    Loader {
                        id: cameraPlayerLoader
                        anchors.fill: parent
                        source: "PrinterVideoPlayer.qml"
                    }
                }

                ConstrainedInfoBar {
                    Layout.fillWidth: true
                    visible: PrinterService.connectionState === PrinterService.ConnectionError
                    severity: InfoBarType.Warning
                    title: PrinterService.deviceAccessRequired
                           ? qsTr("Printer access required")
                           : qsTr("Unable to load printer")
                    message: PrinterService.connectionError
                    closable: false
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: PrinterService.deviceAccessRequired
                             && PrinterService.selectedDeviceOnline

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: PrinterService.backendName === "Moonraker"
                                         ? qsTr("Moonraker API key") : qsTr("Printer access code")
                        echoMode: TextInput.Password
                        text: control.accessCredential
                        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                        onTextChanged: control.accessCredential = text
                        onAccepted: accessButton.clicked()
                    }
                    Button {
                        id: accessButton
                        text: qsTr("Confirm")
                        highlighted: true
                        enabled: control.accessCredential.trim().length > 0
                        onClicked: PrinterService.provideDeviceAccess(
                                       control.accessCredential.trim())
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 10
                    visible: PrinterService.connected

                    StatusMetric {
                        Layout.fillWidth: true
                        title: qsTr("Nozzle")
                        value: control.temperatureText(PrinterService.nozzleTemperature,
                                                       PrinterService.targetNozzleTemperature)
                        iconName: FluentIcons.graph_Processing
                    }
                    StatusMetric {
                        Layout.fillWidth: true
                        title: qsTr("Bed")
                        value: control.temperatureText(PrinterService.bedTemperature,
                                                       PrinterService.targetBedTemperature)
                        iconName: FluentIcons.graph_Tiles
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: PrinterService.connected
                             && PrinterService.jobState === PrinterService.JobIdle
                             && PrinterService.transferState === PrinterService.TransferIdle
                    text: qsTr("Printer is idle and ready to receive a print job")
                    horizontalAlignment: Text.AlignHCenter
                    font: Typography.body
                    color: control.SmoothUI.theme.res.textFillColorSecondary
                }

                Label {
                    Layout.fillWidth: true
                    visible: PrinterService.connected && SlicingPreviewBridge.printOutputReady
                             && !control.hasSendFormat
                    text: qsTr("The selected printer does not support an available preview format.")
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    visible: PrinterService.transferState !== PrinterService.TransferIdle
                             || PrinterService.commandOutcomeUnknown
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: PrinterService.commandOutcomeUnknown || PrinterService.transferState === PrinterService.TransferUnknown
                              ? qsTr("The operation may already have executed. Check the printer before acknowledging. Do not retry automatically.")
                              : PrinterService.transferState === PrinterService.TransferUploading
                                ? qsTr("Uploading: %1%").arg(Math.round(PrinterService.transferProgress * 100))
                              : PrinterService.transferState === PrinterService.TransferStarting
                                ? qsTr("Waiting for the printer to begin")
                              : PrinterService.transferState === PrinterService.TransferUploaded
                                ? qsTr("Upload completed")
                                : qsTr("Transfer failed")
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: PrinterService.jobError.length > 0
                        text: PrinterService.jobError
                        wrapMode: Text.Wrap
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        visible: PrinterService.transferState === PrinterService.TransferUploading
                                 || PrinterService.transferState === PrinterService.TransferStarting
                        from: 0
                        to: 1
                        value: Math.max(0, Math.min(1, PrinterService.transferProgress))
                        indeterminate: PrinterService.transferState === PrinterService.TransferStarting
                    }
                    Button {
                        objectName: "printerTransferAcknowledgeButton"
                        visible: PrinterService.commandOutcomeUnknown || PrinterService.transferState === PrinterService.TransferUnknown
                                 || PrinterService.transferState === PrinterService.TransferUploaded
                                 || PrinterService.transferState === PrinterService.TransferFailed
                        text: PrinterService.commandOutcomeUnknown || PrinterService.transferState === PrinterService.TransferUnknown
                              ? qsTr("I checked the printer. Acknowledge.") : qsTr("Dismiss")
                        onClicked: PrinterService.dismissJobStatus()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    visible: control.activeJob

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            objectName: "printerControlJobFileName"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: PrinterService.jobFileName
                            elide: Text.ElideRight
                            font: Typography.bodyStrong
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: "%1% · %2".arg(Math.round(PrinterService.printProgress))
                                                  .arg(PrinterService.remainingTime)
                            font: Typography.caption
                            color: control.SmoothUI.theme.res.textFillColorSecondary
                        }
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: PrinterService.printProgress
                    }
                }

                RowLayout {
                    id: jobActions
                    Layout.fillWidth: true
                    readonly property real equalButtonWidth:
                        Math.max(popupStopButton.implicitWidth,
                                 popupPauseButton.implicitWidth)
                    spacing: 4
                    visible: control.activeJob

                    Item { Layout.fillWidth: true }

                    Button {
                        id: popupStopButton
                        objectName: "printerControlCancelButton"
                        Layout.preferredWidth: jobActions.equalButtonWidth
                        text: qsTr("Stop")
                        icon.name: FluentIcons.graph_Stop
                        icon.width: 16
                        icon.height: 16
                        enabled: PrinterService.supportsCancel && PrinterService.jobControllable
                                 && PrinterService.jobState !== PrinterService.Stopping
                        onClicked: PrinterService.cancelPrint()
                    }
                    Button {
                        id: popupPauseButton
                        objectName: "printerControlPauseResumeButton"
                        Layout.preferredWidth: jobActions.equalButtonWidth
                        text: PrinterService.jobState === PrinterService.Paused
                              ? qsTr("Continue") : qsTr("Pause")
                        icon.name: PrinterService.jobState === PrinterService.Paused
                                   ? FluentIcons.graph_Play : FluentIcons.graph_Pause
                        icon.width: 16
                        icon.height: 16
                        enabled: PrinterService.supportsPauseResume && PrinterService.jobControllable
                        onClicked: {
                            if (PrinterService.jobState === PrinterService.Paused)
                                PrinterService.resumePrint()
                            else
                                PrinterService.pausePrint()
                        }
                    }
                }

                Item { Layout.fillHeight: true }
                }
            }
        }
    }

    function temperatureText(current, target) {
        if (target > 0)
            return qsTr("%1°C / %2°C").arg(Math.round(current)).arg(Math.round(target))
        return qsTr("%1°C").arg(Math.round(current))
    }

    function routeText(route) {
        if (route === PrinterService.LocalNetwork)
            return qsTr("LAN")
        if (route === PrinterService.Cloud)
            return qsTr("Cloud")
        return ""
    }

    function deviceStateText() {
        if (!PrinterService.selectedDeviceOnline)
            return qsTr("Offline")
        if (PrinterService.connectionState === PrinterService.Connecting)
            return qsTr("Loading")
        if (PrinterService.connectionState === PrinterService.ConnectionError)
            return qsTr("Attention")
        const state = PrinterService.printerStateText.length > 0
                    ? PrinterService.printerStateText
                    : PrinterService.selectedDeviceState
        switch (state.toLowerCase()) {
        case "ready": return qsTr("Ready")
        case "busy": return qsTr("Busy")
        case "calibrate_doing": return qsTr("Calibrating")
        case "error": return qsTr("Error")
        case "heating": return qsTr("Heating")
        case "printing": return qsTr("Printing")
        case "pausing": return qsTr("Pausing")
        case "pause": return qsTr("Paused")
        case "canceling": return qsTr("Canceling")
        case "completed": return qsTr("Completed")
        default: return qsTr("Online")
        }
    }

    component ConstrainedInfoBar: InfoBar {
        id: banner
        Layout.minimumWidth: 0
        // Layout owns width. Do not feed the loaded labels' width back into it.
        implicitWidth: 0
        messageMaximumWidth: Math.max(1, Math.floor(width - leftPadding - rightPadding - 34))
        titleItem: Label {
            width: banner.messageMaximumWidth
            text: banner.title
            font: Typography.bodyStrong
            color: banner.SmoothUI.theme.res.textFillColorPrimary
            wrapMode: Text.Wrap
        }
        // Equal message width selects InfoBar's stacked layout, even for short text.
        messageItem: Label {
            width: banner.messageMaximumWidth
            text: banner.message
            font: Typography.body
            color: banner.SmoothUI.theme.res.textFillColorPrimary
            wrapMode: Text.Wrap
        }
    }

    component StatusMetric: Rectangle {
        property string title: ""
        property string value: ""
        property var iconName: FluentIcons.graph_Info

        implicitHeight: 64
        radius: PrintWorkspaceStyle.controlRadius
        color: control.SmoothUI.theme.res.cardBackgroundFillColorDefault
        border.width: 1
        border.color: control.SmoothUI.theme.res.cardStrokeColorDefault

        RowLayout {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 12
            spacing: 8
            Icon {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                source: parent.parent.iconName
                color: control.SmoothUI.theme.res.textFillColorSecondary
            }
            ColumnLayout {
                spacing: 1
                Label {
                    text: parent.parent.parent.title
                    font: Typography.caption
                    color: control.SmoothUI.theme.res.textFillColorSecondary
                }
                Label {
                    text: parent.parent.parent.value
                    font: Typography.bodyStrong
                    color: control.SmoothUI.theme.res.textFillColorPrimary
                }
            }
        }
    }
}
