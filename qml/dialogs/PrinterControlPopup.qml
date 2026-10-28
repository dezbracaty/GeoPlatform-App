import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

AppPopup {
    id: control
    windowEscapeEnabled: !diagnostics.opened && !moonrakerConnection.opened && !detailPane.dialogOpen
    expandable: true
    SmoothUI.theme: Theme.of(control)

    property Item popupHost: null
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
    preferredWidth: expanded ? 860 : PrintWorkspaceStyle.printerDevicePopoverWidth
    preferredHeight: expanded ? 620 : PrintWorkspaceStyle.printerDevicePopoverHeight
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
            bottomLeftRadius: control.SmoothUI.radius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Item {
                    Layout.fillWidth: true
                    implicitHeight: 37

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: refreshButton.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
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
                        id: refreshButton
                        objectName: "printerRefreshButton"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: control.scanning ? qsTr("Scanning") : qsTr("Refresh")
                        implicitHeight: 27
                        font.pixelSize: 12
                        icon.name: FluentIcons.graph_Refresh
                        icon.width: 16
                        icon.height: 16
                        enabled: PrinterService.agentAvailable && !control.scanning
                                 && !control.preparing
                        onClicked: PrinterService.refreshDevices()
                    }
                }

                ConstrainedInfoBar {
                    Layout.fillWidth: true
                    visible: !PrinterService.agentAvailable
                             || PrinterService.discoveryState === PrinterService.DiscoveryError
                             || PrinterService.discoveryState === PrinterService.DiscoveryWarning
                    severity: InfoBarType.Warning
                    title: !PrinterService.agentAvailable
                           ? qsTr("Flashforge connection unavailable")
                           : PrinterService.discoveryState === PrinterService.DiscoveryWarning
                             ? qsTr("Refresh incomplete") : qsTr("Scan failed")
                    message: !PrinterService.agentAvailable
                             ? qsTr("Install the connection component, or choose another connection type.") : qsTr("Refresh could not retrieve all devices. Check connection diagnostics.")
                    closable: false
                }

                Button {
                    objectName: "printerServiceDiagnosticsButton"
                    text: qsTr("Connection diagnostics")
                    flat: true
                    visible: !PrinterService.agentAvailable || PrinterService.discoveryError.length > 0
                    onClicked: diagnostics.open()
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
                                     && (!PrinterService.transferBusy || (!PrinterService.connected && PrinterService.canConfigureBackend))
                                && !PrinterService.printing
                            onClicked: {
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
                                        source: FluentIcons.graph_Print
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
                            source: FluentIcons.graph_Print
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

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: control.SmoothUI.theme.res.dividerStrokeColorDefault }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Icon {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        source: FluentIcons.graph_Devices3
                        color: control.SmoothUI.theme.res.textFillColorSecondary
                    }
                    Label {
                        objectName: "printerConnectionAvailability"
                        Layout.fillWidth: true
                        text: qsTr("%1 · %2").arg(PrinterService.backendName).arg(PrinterService.agentAvailable ? qsTr("Available") : qsTr("Unavailable"))
                        font.pixelSize: 11
                        color: control.SmoothUI.theme.res.textFillColorSecondary
                    }
                }
                Button {
                    objectName: "printerConfigureMoonrakerButton"
                    implicitHeight: 27
                    font.pixelSize: 12
                    text: qsTr("Connect to Moonraker")
                    enabled: PrinterService.canConfigureBackend
                    onClicked: {
                        moonrakerConfigError.text = ""
                        moonrakerConnection.open()
                    }
                }
                Button {
                    objectName: "printerUseFlashforgeButton"
                    implicitHeight: 27
                    font.pixelSize: 12
                    text: qsTr("Use Flashforge")
                    visible: PrinterService.backendName !== "Flashforge"
                    enabled: PrinterService.canConfigureBackend
                    onClicked: PrinterService.useFlashforge()
                }
                Button {
                    objectName: "printerControlFlashforgeAccountButton"
                    implicitHeight: 27
                    font.pixelSize: 12
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
                visible: !control.hasSelection
                spacing: 10
                Label { Layout.fillWidth: true; text: qsTr("Select a printer"); font: Typography.subtitle; horizontalAlignment: Text.AlignHCenter }
                Label { Layout.fillWidth: true; text: qsTr("Select an online printer to connect, or an offline printer to view its information."); wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            }
            PrinterDeviceDetails {
                id: detailPane
                expanded: control.expanded
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 14
                anchors.bottomMargin: 12
                visible: control.hasSelection
                onDiagnosticsRequested: diagnostics.open()
            }
        }
    }

    ContentDialog {
        id: diagnostics
        objectName: "printerDiagnosticsDialog"
        title: qsTr("Connection diagnostics")
        ColumnLayout {
            width: diagnostics.availableWidth
            spacing: 10
            Label { Layout.fillWidth: true; text: qsTr("Connection: %1").arg(PrinterService.backendName); wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: PrinterService.agentAvailable ? qsTr("Connection component available") : qsTr("Connection component unavailable"); wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: [PrinterService.agentError, PrinterService.discoveryError, PrinterService.connectionError, PrinterService.deviceControlError].filter(v => v.length > 0).join("\n"); wrapMode: Text.Wrap; font: Typography.caption }
            Label { Layout.fillWidth: true; visible: !PrinterService.agentAvailable; text: qsTr("Install the Flashforge connection component matching this application, then restart GPlatform. Other connection types remain available."); wrapMode: Text.Wrap }
        }
        footer: DialogButtonBox { Button { objectName: "printerDiagnosticsCloseButton"; text: qsTr("Close"); onClicked: diagnostics.close() } }
    }

    function routeText(route) {
        if (route === PrinterService.LocalNetwork)
            return qsTr("LAN")
        if (route === PrinterService.Cloud)
            return qsTr("Cloud")
        return ""
    }

    component ConstrainedInfoBar: Rectangle {
        id: banner
        property string title: ""
        property string message: ""
        property int severity: InfoBarType.Warning
        property bool closable: false
        Layout.minimumWidth: 0
        implicitWidth: 0
        implicitHeight: notice.implicitHeight + 20
        radius: PrintWorkspaceStyle.controlRadius
        color: control.SmoothUI.theme.res.systemFillColorCautionBackground
        border.color: Colors.withOpacity(control.SmoothUI.theme.res.systemFillColorCaution, 0.25)
        ColumnLayout {
            id: notice
            x: 10
            y: 10
            width: Math.max(0, banner.width - 20)
            spacing: 5
            Label { Layout.fillWidth: true; text: banner.title; font: Typography.caption; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: banner.message; font: Typography.caption; wrapMode: Text.Wrap; color: control.SmoothUI.theme.res.textFillColorSecondary }
        }
    }
}
