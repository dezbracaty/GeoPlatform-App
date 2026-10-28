pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

ColumnLayout {
    id: root
    objectName: "printerDeviceDetails"
    SmoothUI.theme: Theme.of(root)
    readonly property string nozzleTitle: {
        const name = PrinterService.deviceCapabilities.nozzleName || "";
        if (name === "extruder")
            return qsTr("Nozzle %1").arg(1);
        if (/^extruder[0-9]+$/.test(name))
            return qsTr("Nozzle %1").arg(Number(name.slice(8)) + 1);
        return qsTr("Nozzle");
    }
    spacing: 8
    readonly property bool dialogOpen: disconnectDialog.opened || stopDialog.opened || accessDialog.opened || verifyDialog.opened || videoDialog.opened
    property int currentTab: 0
    property bool expanded: false
    property bool activeJob: PrinterService.jobState === PrinterService.Printing || PrinterService.jobState === PrinterService.Paused || PrinterService.jobState === PrinterService.Pausing || PrinterService.jobState === PrinterService.Resuming || PrinterService.jobState === PrinterService.Stopping
    readonly property string connectionDescription: [PrinterService.backendName, PrinterService.selectedDeviceRoute === PrinterService.Cloud ? qsTr("Cloud") : PrinterService.selectedDeviceRoute === PrinterService.LocalNetwork ? qsTr("LAN") : ""].filter(v => v.length > 0).join(" · ")
    readonly property string availableControls: {
        if (!PrinterService.statusFresh)
            return "—";
        const caps = PrinterService.deviceCapabilities;
        const controls = [];
        if (caps.nozzle || caps.bed)
            controls.push(qsTr("Temperature control"));
        if (caps.motion || caps.homing)
            controls.push(qsTr("Motion"));
        if (caps.extrusion)
            controls.push(qsTr("Filament controls"));
        if (caps.light)
            controls.push(qsTr("Chamber light"));
        if (caps.fan)
            controls.push(qsTr("Cooling fan"));
        if (PrinterService.cameraAvailable)
            controls.push(qsTr("Camera"));
        return controls.length ? controls.join(qsTr(", ")) : qsTr("None");
    }
    signal diagnosticsRequested
    readonly property string stateLabel: {
        if (PrinterService.commandOutcomeUnknown || PrinterService.transferState === PrinterService.TransferUnknown)
            return qsTr("Result unknown");
        if (!PrinterService.selectedDeviceOnline)
            return qsTr("Offline");
        if (PrinterService.deviceAccessRequired)
            return qsTr("Access required");
        if (PrinterService.connectionState === PrinterService.Connecting)
            return qsTr("Connecting");
        if (PrinterService.connectionState === PrinterService.ConnectionError)
            return qsTr("Connection interrupted");
        if (!PrinterService.connected)
            return qsTr("Disconnected");
        if (PrinterService.jobState === PrinterService.Paused)
            return qsTr("Paused");
        if (PrinterService.jobState === PrinterService.Cancelled)
            return qsTr("Cancelled");
        if (activeJob)
            return qsTr("Printing");
        return qsTr("Idle");
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Layout.minimumHeight: 43
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: PrinterService.selectedDeviceName
                elide: Text.ElideRight
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: [PrinterService.selectedDeviceModel, PrinterService.selectedDeviceRoute === PrinterService.Cloud ? qsTr("Cloud") : qsTr("LAN"), PrinterService.selectedDeviceIp].filter(v => v.length > 0).join(" · ")
                elide: Text.ElideRight
                font: Typography.caption
                color: root.SmoothUI.theme.res.textFillColorSecondary
            }
        }
        Rectangle {
            readonly property bool warning: PrinterService.commandOutcomeUnknown || PrinterService.connectionState === PrinterService.ConnectionError || PrinterService.deviceAccessRequired || PrinterService.jobState === PrinterService.Paused
            readonly property color tone: warning ? root.SmoothUI.theme.res.systemFillColorCaution : PrinterService.connected ? (root.activeJob ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark) : root.SmoothUI.theme.res.systemFillColorSuccess) : root.SmoothUI.theme.res.textFillColorSecondary
            implicitWidth: stateText.implicitWidth + 16
            implicitHeight: stateText.implicitHeight + 8
            radius: 14
            color: Colors.withOpacity(tone, 0.08)
            Label {
                id: stateText
                anchors.centerIn: parent
                text: root.stateLabel
                font.pixelSize: 11
                color: parent.tone
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Button {
            objectName: "printerDisconnectButton"
            icon.name: FluentIcons.graph_DisconnectDisplay
            icon.width: 14
            icon.height: 14
            text: PrinterService.connectionState === PrinterService.Connecting ? qsTr("Cancel") : qsTr("Disconnect device")
            implicitHeight: 28
            font: Typography.caption
            visible: PrinterService.connected || PrinterService.connectionState === PrinterService.Connecting
            enabled: PrinterService.canDisconnect
            onClicked: {
                if (PrinterService.connectionState === PrinterService.Connecting)
                    PrinterService.disconnectDevice();
                else
                    disconnectDialog.open();
            }
        }
        Button {
            objectName: "printerReconnectButton"
            icon.name: FluentIcons.graph_Refresh
            icon.width: 14
            icon.height: 14
            text: PrinterService.deviceAccessRequired ? qsTr("Enter access code") : PrinterService.connected || PrinterService.connectionState === PrinterService.ConnectionError ? qsTr("Reconnect") : qsTr("Connect")
            implicitHeight: 28
            font: Typography.caption
            visible: PrinterService.connectionState !== PrinterService.Connecting
            enabled: PrinterService.canReconnect
            onClicked: {
                if (PrinterService.deviceAccessRequired)
                    accessDialog.open();
                else
                    PrinterService.reconnectDevice();
            }
        }
        Item {
            Layout.fillWidth: true
        }
        IconButton {
            objectName: "printerConnectionDiagnosticsButton"
            icon.name: FluentIcons.graph_Info
            flat: true
            implicitWidth: 28
            implicitHeight: 28
            Accessible.name: qsTr("Connection diagnostics")
            onClicked: root.diagnosticsRequested()
        }
    }
    Item {
        objectName: "printerDetailsTabs"
        Layout.fillWidth: true
        implicitHeight: 32
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
        }
        RowLayout {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: 18
            Repeater {
                id: mainTabs
                model: [qsTr("Overview"), qsTr("Device controls"), qsTr("Device information")]
                delegate: Button {
                    id: mainTab
                    required property int index
                    required property string modelData
                    objectName: ["printerOverviewTab", "printerControlsTab", "printerInformationTab"][index]
                    Layout.fillHeight: true
                    padding: 0
                    leftPadding: 0
                    rightPadding: 0
                    implicitWidth: contentItem.implicitWidth
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData
                    Accessible.focusable: true
                    Accessible.onPressAction: clicked()
                    contentItem: Label {
                        text: mainTab.modelData
                        font.pixelSize: 12
                        font.weight: root.currentTab === mainTab.index ? Font.DemiBold : Font.Normal
                        color: root.currentTab === mainTab.index ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark) : root.SmoothUI.theme.res.textFillColorSecondary
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Item {
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            radius: 1
                            color: root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
                            visible: root.currentTab === mainTab.index
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.width: mainTab.visualFocus ? 1 : 0
                            border.color: root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
                        }
                    }
                    onClicked: root.currentTab = index
                    Keys.onRightPressed: {
                        root.currentTab = (index + 1) % 3;
                        mainTabs.itemAt(root.currentTab).forceActiveFocus();
                    }
                    Keys.onLeftPressed: {
                        root.currentTab = (index + 2) % 3;
                        mainTabs.itemAt(root.currentTab).forceActiveFocus();
                    }
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                            root.currentTab = event.key === Qt.Key_Home ? 0 : 2;
                            mainTabs.itemAt(root.currentTab).forceActiveFocus();
                            event.accepted = true;
                        }
                    }
                }
            }
        }
    }
    ScrollView {
        id: scroll
        objectName: "printerDetailScroll"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 0
        clip: true
        contentWidth: availableWidth
        contentHeight: content.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: content
            width: scroll.availableWidth
            spacing: 8
            Label {
                Layout.fillWidth: true
                visible: !PrinterService.statusFresh && root.currentTab !== 2
                text: !PrinterService.agentAvailable ? qsTr("This connection type is unavailable. See connection settings or choose another connection type.") : !PrinterService.selectedDeviceOnline ? qsTr("Check the printer power and network, then refresh the device list.") : PrinterService.deviceAccessRequired ? qsTr("Enter the access code shown on your printer.") : qsTr("Connect to read the current device status. Any print job on the printer continues while disconnected.")
                wrapMode: Text.Wrap
                font: Typography.caption
                color: root.SmoothUI.theme.res.textFillColorSecondary
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.currentTab === 0
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: PrinterService.cameraAvailable ? (root.expanded ? 170 : 96) : 78
                    visible: PrinterService.statusFresh
                    color: PrinterService.cameraAvailable ? "#22262b" : root.SmoothUI.theme.res.subtleFillColorSecondary
                    radius: 6
                    border.width: PrinterService.cameraAvailable ? 0 : 1
                    border.color: root.SmoothUI.theme.res.dividerStrokeColorDefault
                    Loader {
                        anchors.fill: parent
                        active: PrinterService.cameraAvailable && !videoDialog.opened
                        sourceComponent: Component {
                            PrinterVideoPlayer {
                                compact: true
                            }
                        }
                    }
                    IconButton {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 4
                        icon.name: FluentIcons.graph_FullScreen
                        icon.width: 16
                        icon.height: 16
                        contentItem: Icon {
                            source: FluentIcons.graph_FullScreen
                            color: "#d7dce3"
                            width: 16
                            height: 16
                        }
                        flat: true
                        implicitWidth: 28
                        implicitHeight: 28
                        Accessible.name: qsTr("Expand")
                        visible: PrinterService.cameraAvailable
                        onClicked: videoDialog.open()
                    }
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width - 20
                        spacing: 5
                        visible: !PrinterService.cameraAvailable
                        Icon {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            source: FluentIcons.graph_Camera
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Live video is unavailable with this connection.")
                            font.pixelSize: 12
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Metric {
                        bedIcon: false
                        title: root.nozzleTitle
                        value: PrinterService.statusFresh ? qsTr("%1°C / %2°C").arg(Math.round(PrinterService.nozzleTemperature)).arg(Math.round(PrinterService.targetNozzleTemperature)) : "— / —"
                    }
                    Metric {
                        bedIcon: true
                        title: qsTr("Bed")
                        value: PrinterService.statusFresh ? qsTr("%1°C / %2°C").arg(Math.round(PrinterService.bedTemperature)).arg(Math.round(PrinterService.targetBedTemperature)) : "— / —"
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: job.implicitHeight + 16
                    radius: 6
                    color: root.SmoothUI.theme.res.subtleFillColorSecondary
                    border.color: root.SmoothUI.theme.res.dividerStrokeColorDefault
                    ColumnLayout {
                        id: job
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Icon {
                                visible: !root.activeJob && PrinterService.transferState === PrinterService.TransferIdle
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                source: FluentIcons.graph_StatusCircleCheckmark
                                color: root.SmoothUI.theme.res.systemFillColorSuccess
                            }
                            Label {
                                objectName: "printerControlJobFileName"
                                Layout.fillWidth: true
                                text: root.activeJob || PrinterService.transferState !== PrinterService.TransferIdle ? (PrinterService.jobFileName || qsTr("Printing")) : qsTr("No active print job")
                                elide: Text.ElideRight
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            visible: root.activeJob || PrinterService.transferBusy
                            value: PrinterService.transferState === PrinterService.TransferUploading ? PrinterService.transferProgress : PrinterService.printProgress / 100
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.activeJob
                            Label {
                                text: PrinterService.statusFresh ? qsTr("%1% · %2").arg(Math.round(PrinterService.printProgress)).arg(root.stateLabel) : qsTr("Last known job · Status is stale")
                                font: Typography.caption
                                Layout.fillWidth: true
                            }
                            Label {
                                text: PrinterService.statusFresh ? qsTr("Remaining %1").arg(PrinterService.remainingTime) : "—"
                                font: Typography.caption
                            }
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignRight
                            visible: root.activeJob
                            Button {
                                objectName: "printerControlCancelButton"
                                text: qsTr("Stop")
                                implicitHeight: 28
                                font: Typography.caption
                                visible: PrinterService.supportsCancel
                                enabled: PrinterService.jobControllable
                                onClicked: stopDialog.open()
                            }
                            Button {
                                objectName: "printerControlPauseResumeButton"
                                text: PrinterService.jobState === PrinterService.Paused ? qsTr("Continue") : qsTr("Pause")
                                highlighted: true
                                implicitHeight: 28
                                font: Typography.caption
                                visible: PrinterService.supportsPauseResume
                                enabled: PrinterService.jobControllable
                                onClicked: PrinterService.paused ? PrinterService.resumePrint() : PrinterService.pausePrint()
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !root.activeJob
                            text: qsTr("Send a sliced file from the bottom command bar.")
                            font.pixelSize: 11
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                            wrapMode: Text.Wrap
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: !PrinterService.jobNoticeDismissed && PrinterService.jobError.length > 0
                    text: PrinterService.jobError
                    wrapMode: Text.Wrap
                    font: Typography.caption
                    color: root.SmoothUI.theme.res.systemFillColorCaution
                }
                Button {
                    objectName: "printerDeviceCancelSubmissionButton"
                    visible: PrinterService.canCancelSubmission
                    text: qsTr("Cancel upload")
                    onClicked: PrinterService.cancelSubmission()
                }
                Button {
                    objectName: "printerTransferAcknowledgeButton"
                    visible: PrinterService.deviceControlResultUnknown || (!PrinterService.jobNoticeDismissed && (PrinterService.commandOutcomeUnknown || PrinterService.transferState === PrinterService.TransferUnknown || PrinterService.transferState === PrinterService.TransferUploaded || PrinterService.transferState === PrinterService.TransferFailed || PrinterService.transferState === PrinterService.TransferCancelled))
                    text: PrinterService.deviceControlResultUnknown ? qsTr("Verify printer status") : PrinterService.commandOutcomeUnknown || PrinterService.transferState === PrinterService.TransferUnknown ? qsTr("Retry status") : qsTr("Dismiss")
                    enabled: PrinterService.deviceControlResultUnknown || !(PrinterService.commandOutcomeUnknown || PrinterService.transferState === PrinterService.TransferUnknown) || PrinterService.canRefreshStatus
                    onClicked: {
                        if (PrinterService.deviceControlResultUnknown)
                            verifyDialog.open();
                        else if (PrinterService.commandOutcomeUnknown || PrinterService.transferState === PrinterService.TransferUnknown)
                            PrinterService.refreshPrinterStatus();
                        else
                            PrinterService.dismissJobStatus();
                    }
                }
            }
            PrinterDeviceControls {
                Layout.fillWidth: true
                visible: root.currentTab === 1
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.currentTab === 2
                spacing: 9
                Repeater {
                    model: [[qsTr("Device name"), PrinterService.selectedDeviceName], [qsTr("Model"), PrinterService.selectedDeviceModel], [qsTr("Connection"), root.connectionDescription], [qsTr("IP address"), PrinterService.selectedDeviceIp], [qsTr("Firmware"), PrinterService.deviceDetails.firmware || "—"], [qsTr("Available controls"), root.availableControls]]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label {
                            Layout.preferredWidth: 80
                            text: modelData[0]
                            font: Typography.caption
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: modelData[1]
                            font: Typography.caption
                            wrapMode: Text.Wrap
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: root.SmoothUI.theme.res.dividerStrokeColorDefault
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Controls are shown only when supported by the connected device.")
                    wrapMode: Text.Wrap
                    font.pixelSize: 11
                    color: root.SmoothUI.theme.res.textFillColorSecondary
                }
                Button {
                    objectName: "printerInfoDiagnosticsButton"
                    Layout.fillWidth: true
                    implicitHeight: 27
                    font.pixelSize: 12
                    icon.name: FluentIcons.graph_Info
                    icon.width: 16
                    icon.height: 16
                    text: qsTr("Connection diagnostics")
                    onClicked: root.diagnosticsRequested()
                }
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: root.SmoothUI.theme.res.dividerStrokeColorDefault
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Icon {
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            source: FluentIcons.graph_Info
            color: root.SmoothUI.theme.res.textFillColorTertiary
        }
        Label {
            Layout.fillWidth: true
            text: PrinterService.statusFresh ? qsTr("Live device status") : qsTr("Status is stale")
            font.pixelSize: 10
            color: root.SmoothUI.theme.res.textFillColorTertiary
        }
    }

    ContentDialog {
        id: videoDialog
        title: qsTr("Live video")
        width: Math.min(840, parent ? parent.width - 32 : 840)
        Rectangle {
            width: videoDialog.availableWidth
            height: Math.min(360, width * 9 / 16)
            color: "#22262b"
            Loader {
                anchors.fill: parent
                active: videoDialog.opened
                sourceComponent: Component {
                    PrinterVideoPlayer {}
                }
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Close")
                onClicked: videoDialog.close()
            }
        }
    }
    ContentDialog {
        id: disconnectDialog
        objectName: "printerDisconnectDialog"
        title: qsTr("Disconnect from this device?")
        Label {
            width: disconnectDialog.availableWidth
            text: qsTr("Only GPlatform will disconnect. Printing and heating on the printer will continue.")
            wrapMode: Text.Wrap
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                onClicked: disconnectDialog.close()
            }
            Button {
                objectName: "printerConfirmDisconnectButton"
                text: qsTr("Disconnect")
                highlighted: true
                enabled: PrinterService.canDisconnect
                onClicked: {
                    PrinterService.disconnectDevice();
                    disconnectDialog.close();
                }
            }
        }
    }
    ContentDialog {
        id: stopDialog
        objectName: "printerStopDialog"
        title: qsTr("Stop this print job?")
        Label {
            width: stopDialog.availableWidth
            text: qsTr("The current job will end and cannot be resumed after stopping.")
            wrapMode: Text.Wrap
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                onClicked: stopDialog.close()
            }
            Button {
                objectName: "printerConfirmStopButton"
                text: qsTr("Stop printing")
                enabled: PrinterService.jobControllable
                highlighted: true
                onClicked: {
                    PrinterService.cancelPrint();
                    stopDialog.close();
                }
            }
        }
    }
    ContentDialog {
        id: accessDialog
        title: qsTr("Device access code")
        onClosed: accessCode.text = ""
        ColumnLayout {
            width: accessDialog.availableWidth
            Label {
                Layout.fillWidth: true
                text: qsTr("Enter the access code shown on your printer.")
                wrapMode: Text.Wrap
            }
            TextField {
                id: accessCode
                objectName: "printerAccessCode"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                onClicked: accessDialog.close()
            }
            Button {
                objectName: "printerConfirmAccessButton"
                text: qsTr("Connect")
                enabled: accessCode.text.trim().length > 0
                onClicked: {
                    PrinterService.provideDeviceAccess(accessCode.text);
                    accessDialog.close();
                }
            }
        }
    }
    ContentDialog {
        id: verifyDialog
        title: qsTr("Verify printer status")
        onOpened: checkedPrinter.checked = false
        ColumnLayout {
            width: verifyDialog.availableWidth
            Label {
                Layout.fillWidth: true
                text: qsTr("Check the printer before acknowledging an unknown operation outcome. Acknowledging will not resend the operation.")
                wrapMode: Text.Wrap
            }
            CheckBox {
                id: checkedPrinter
                objectName: "printerVerifiedCheck"
                text: qsTr("I checked the printer status")
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                onClicked: verifyDialog.close()
            }
            Button {
                objectName: "printerConfirmVerifiedButton"
                text: qsTr("Acknowledge")
                enabled: checkedPrinter.checked
                onClicked: {
                    PrinterService.acknowledgeDeviceControlOutcome();
                    verifyDialog.close();
                }
            }
        }
    }
    component Metric: Rectangle {
        id: metric
        property string title
        property string value
        property bool bedIcon: false
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: 52
        radius: 6
        color: root.SmoothUI.theme.res.cardBackgroundFillColorDefault
        border.color: root.SmoothUI.theme.res.cardStrokeColorDefault
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8
            Canvas {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                property color stroke: root.SmoothUI.theme.res.textFillColorSecondary
                onStrokeChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = stroke;
                    ctx.lineWidth = 1.3;
                    ctx.lineCap = "round";
                    ctx.lineJoin = "round";
                    if (metric.bedIcon) {
                        ctx.beginPath();
                        ctx.moveTo(2, 12);
                        ctx.lineTo(16, 12);
                        ctx.lineTo(16, 16);
                        ctx.lineTo(2, 16);
                        ctx.closePath();
                        ctx.stroke();
                        for (let x = 4; x <= 14; x += 5) {
                            ctx.beginPath();
                            ctx.moveTo(x, 9);
                            ctx.bezierCurveTo(x - 3, 7, x + 2, 5, x, 2);
                            ctx.stroke();
                        }
                    } else {
                        ctx.beginPath();
                        ctx.moveTo(7, 10);
                        ctx.lineTo(7, 4);
                        ctx.bezierCurveTo(7, 1, 11, 1, 11, 4);
                        ctx.lineTo(11, 10);
                        ctx.arc(9, 13, 3.6, -0.98, 4.12);
                        ctx.closePath();
                        ctx.stroke();
                        ctx.beginPath();
                        ctx.moveTo(9, 6);
                        ctx.lineTo(9, 13);
                        ctx.stroke();
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    text: metric.title
                    font.pixelSize: 11
                    color: root.SmoothUI.theme.res.textFillColorSecondary
                }
                Label {
                    text: metric.value
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
