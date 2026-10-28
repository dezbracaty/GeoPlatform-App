pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

ColumnLayout {
    id: root
    objectName: "printerDeviceControls"
    SmoothUI.theme: Theme.of(root)
    readonly property string nozzleTitle: {
        const name = PrinterService.deviceCapabilities.nozzleName || "";
        if (name === "extruder")
            return qsTr("Nozzle %1").arg(1);
        if (/^extruder[0-9]+$/.test(name))
            return qsTr("Nozzle %1").arg(Number(name.slice(8)) + 1);
        return qsTr("Nozzle");
    }
    spacing: 10
    property int section: 0
    property real step: 1
    readonly property var caps: PrinterService.deviceCapabilities
    readonly property var details: PrinterService.deviceDetails
    readonly property bool ready: PrinterService.deviceControlsReady
    readonly property bool movable: PrinterService.movementReady

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 33
        radius: 5
        color: root.SmoothUI.theme.res.subtleFillColorSecondary
        border.color: root.SmoothUI.theme.res.cardStrokeColorDefault
        RowLayout {
            anchors.fill: parent
            anchors.margins: 3
            spacing: 3
            Repeater {
                model: [qsTr("Temperature control"), qsTr("Motion"), qsTr("Filament controls"), qsTr("Auxiliary")]
                delegate: Button {
                    id: sectionButton
                    required property int index
                    required property string modelData
                    objectName: "printerControlSection." + index
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    padding: 3
                    contentItem: Label {
                        text: sectionButton.modelData
                        font.pixelSize: 11
                        font.weight: root.section === sectionButton.index ? Font.DemiBold : Font.Normal
                        color: root.section === sectionButton.index ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark) : root.SmoothUI.theme.res.textFillColorSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 3
                        color: root.section === sectionButton.index ? root.SmoothUI.theme.res.cardBackgroundFillColorDefault : sectionButton.hovered ? root.SmoothUI.theme.res.subtleFillColorSecondary : "transparent"
                        border.width: root.section === sectionButton.index ? 1 : 0
                        border.color: root.SmoothUI.theme.res.dividerStrokeColorDefault
                    }
                    onClicked: root.section = index
                }
            }
        }
    }
    ColumnLayout {
        visible: root.section === 0
        Layout.fillWidth: true
        spacing: 10
        TemperatureRow {
            label: root.nozzleTitle
            action: "nozzle"
            current: PrinterService.nozzleTemperature
            target: PrinterService.targetNozzleTemperature
            limit: root.caps.maxNozzleTemperature
            visible: root.caps.nozzle
        }
        TemperatureRow {
            label: qsTr("Bed")
            action: "bed"
            current: PrinterService.bedTemperature
            target: PrinterService.targetBedTemperature
            limit: root.caps.maxBedTemperature
            visible: root.caps.bed
        }
        Label {
            Layout.fillWidth: true
            visible: !root.caps.nozzle && !root.caps.bed
            text: qsTr("Temperature control is not supported by this device.")
            wrapMode: Text.Wrap
            font: Typography.caption
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            visible: root.caps.nozzle && root.caps.bed
            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
        }
        Label {
            text: qsTr("Material preheat")
            visible: root.caps.nozzle && root.caps.bed
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.caps.nozzle && root.caps.bed
            Button {
                objectName: "printerPreheatPlaButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: 27
                leftPadding: 4
                rightPadding: 4
                text: "PLA · 210 / 60°C"
                font: Typography.caption
                enabled: root.ready && root.caps.maxNozzleTemperature >= 210 && root.caps.maxBedTemperature >= 60
                onClicked: PrinterService.controlDevice("preheatPLA")
            }
            Button {
                objectName: "printerPreheatPetgButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: 27
                leftPadding: 4
                rightPadding: 4
                text: "PETG · 240 / 80°C"
                font: Typography.caption
                enabled: root.ready && root.caps.maxNozzleTemperature >= 240 && root.caps.maxBedTemperature >= 80
                onClicked: PrinterService.controlDevice("preheatPETG")
            }
        }
        Button {
            objectName: "printerHeatersOffButton"
            Layout.fillWidth: true
            implicitHeight: 27
            font.pixelSize: 12
            text: qsTr("Turn off heating")
            visible: root.caps.nozzle || root.caps.bed
            enabled: root.ready
            onClicked: PrinterService.controlDevice("heatersOff")
        }
        Label {
            Layout.fillWidth: true
            visible: root.caps.nozzle || root.caps.bed
            text: qsTr("Temperature limits: nozzle %1°C, bed %2°C. Values update from the device.").arg(root.caps.maxNozzleTemperature).arg(root.caps.maxBedTemperature)
            wrapMode: Text.Wrap
            font.pixelSize: 11
            color: root.SmoothUI.theme.res.textFillColorSecondary
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.section === 1
        spacing: 10
        RowLayout {
            visible: root.caps.motion || root.caps.homing
            Layout.fillWidth: true
            Label {
                text: qsTr("Step")
                Layout.fillWidth: true
                font: Typography.caption
            }
            Rectangle {
                implicitWidth: 162
                implicitHeight: 26
                radius: 4
                color: root.SmoothUI.theme.res.cardBackgroundFillColorDefault
                border.color: root.SmoothUI.theme.res.cardStrokeColorDefault
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    Repeater {
                        model: [0.1, 1, 10]
                        delegate: Button {
                            id: stepButton
                            required property real modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            padding: 3
                            leftPadding: 3
                            rightPadding: 3
                            text: qsTr("%1 mm").arg(modelData)
                            contentItem: Label {
                                text: stepButton.text
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: !stepButton.enabled ? root.SmoothUI.theme.res.textFillColorDisabled : root.step === stepButton.modelData ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark) : root.SmoothUI.theme.res.textFillColorSecondary
                            }
                            font.pixelSize: 11
                            enabled: root.movable
                            background: Rectangle {
                                color: root.step === stepButton.modelData ? Colors.withOpacity(root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark), 0.09) : "transparent"
                            }
                            onClicked: root.step = modelData
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            visible: root.caps.motion || root.caps.homing
            spacing: 25
            GridLayout {
                columns: 3
                rowSpacing: 5
                columnSpacing: 5
                Item {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 36
                }
                MoveButton {
                    text: "↑"
                    axis: "y"
                    direction: 1
                }
                Item {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 36
                }
                MoveButton {
                    text: "←"
                    axis: "x"
                    direction: -1
                }
                Button {
                    id: homeButton
                    objectName: "printerHomeButton"
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 36
                    padding: 9
                    leftPadding: 9
                    rightPadding: 9
                    contentItem: Icon {
                        source: FluentIcons.graph_Home
                        color: homeButton.enabled ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark) : root.SmoothUI.theme.res.textFillColorDisabled
                    }
                    background: Rectangle {
                        radius: 4
                        color: Colors.withOpacity(root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark), homeButton.down ? 0.18 : 0.09)
                        border.color: root.SmoothUI.theme.res.cardStrokeColorDefault
                    }
                    Accessible.name: qsTr("Home")
                    font: Typography.caption
                    enabled: root.movable && root.caps.homing
                    onClicked: PrinterService.controlDevice("home")
                }
                MoveButton {
                    text: "→"
                    axis: "x"
                    direction: 1
                }
                Item {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 36
                }
                MoveButton {
                    text: "↓"
                    axis: "y"
                    direction: -1
                }
            }
            ColumnLayout {
                MoveButton {
                    text: "↑"
                    axis: "z"
                    direction: 1
                }
                Label {
                    text: "Z"
                    Layout.alignment: Qt.AlignHCenter
                    font: Typography.caption
                }
                MoveButton {
                    text: "↓"
                    axis: "z"
                    direction: -1
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 30
            radius: 4
            color: root.SmoothUI.theme.res.subtleFillColorSecondary
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                Repeater {
                    model: ["x", "y", "z"]
                    delegate: Label {
                        required property string modelData
                        Layout.fillWidth: true
                        horizontalAlignment: modelData === "z" ? Text.AlignRight : modelData === "y" ? Text.AlignHCenter : Text.AlignLeft
                        text: modelData.toUpperCase() + " " + (root.details.positionAvailable && PrinterService.statusFresh ? Number(root.details[modelData]).toFixed(1) + " mm" : "—")
                        font.pixelSize: 11
                        color: root.SmoothUI.theme.res.textFillColorSecondary
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: !root.caps.motion && !root.caps.homing ? qsTr("Motion control is not supported by this device.") : qsTr("Motion and extrusion are disabled while printing or when device status is uncertain.")
            wrapMode: Text.Wrap
            font.pixelSize: 11
            color: root.SmoothUI.theme.res.textFillColorSecondary
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.section === 2
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            FilamentMetric {
                title: root.nozzleTitle
                current: PrinterService.nozzleTemperature
                target: PrinterService.targetNozzleTemperature
            }
            FilamentMetric {
                bedIcon: true
                title: qsTr("Bed")
                current: PrinterService.bedTemperature
                target: PrinterService.targetBedTemperature
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: warmup.implicitHeight + 16
            visible: root.caps.extrusion && PrinterService.statusFresh && PrinterService.nozzleTemperature < root.caps.minExtrudeTemperature
            radius: 5
            color: root.SmoothUI.dark ? root.SmoothUI.theme.res.systemFillColorCautionBackground : "#fff8e8"
            border.color: root.SmoothUI.dark ? Colors.withOpacity(root.SmoothUI.theme.res.systemFillColorCaution, 0.25) : "#eddaad"
            ColumnLayout {
                id: warmup
                x: 10
                y: 8
                width: parent.width - 20
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Heat the nozzle to at least %1°C before loading or unloading filament.").arg(root.caps.minExtrudeTemperature)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }
                Button {
                    objectName: "printerFilamentHeatButton"
                    text: qsTr("Heat to 210°C")
                    implicitHeight: 27
                    font.pixelSize: 12
                    enabled: root.ready && root.caps.nozzle && root.caps.maxNozzleTemperature >= 210
                    onClicked: PrinterService.controlDevice("nozzle", 210)
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: !root.caps.extrusion
            text: qsTr("Filament control is not supported by this device.")
            wrapMode: Text.Wrap
            font.pixelSize: 11
            color: root.SmoothUI.theme.res.textFillColorSecondary
        }
        RowLayout {
            visible: root.caps.extrusion
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("Filament length")
                font: Typography.caption
            }
            TextField {
                id: length
                objectName: "printerFilamentLength"
                Layout.preferredWidth: 65
                implicitHeight: 30
                font.pixelSize: 12
                text: "10"
                validator: DoubleValidator {
                    bottom: 0.1
                    top: 100
                    notation: DoubleValidator.StandardNotation
                }
                inputMethodHints: Qt.ImhFormattedNumbersOnly
            }
            Label {
                text: "mm"
                font.pixelSize: 11
                color: root.SmoothUI.theme.res.textFillColorSecondary
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.caps.extrusion
            spacing: 6
            Button {
                objectName: "printerExtrudeButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: 27
                font.pixelSize: 12
                text: qsTr("Extrude")
                enabled: root.movable && length.acceptableInput && PrinterService.nozzleTemperature >= root.caps.minExtrudeTemperature
                onClicked: PrinterService.controlDevice("extrude", Number(length.text))
            }
            Button {
                objectName: "printerRetractButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: 27
                font.pixelSize: 12
                text: qsTr("Retract")
                enabled: root.movable && length.acceptableInput && PrinterService.nozzleTemperature >= root.caps.minExtrudeTemperature
                onClicked: PrinterService.controlDevice("extrude", -Number(length.text))
            }
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.section === 3
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            visible: root.caps.light
            Label {
                text: qsTr("Chamber light")
                font: Typography.caption
                Layout.fillWidth: true
            }
            Switch {
                objectName: "printerLightSwitch"
                checked: root.details.lightOn
                enabled: root.ready
                onToggled: PrinterService.controlDevice("light", checked ? 1 : 0)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.caps.fan && !root.caps.fanPercentage
            Label {
                text: qsTr("Cooling fan")
                font: Typography.caption
                Layout.fillWidth: true
            }
            Switch {
                objectName: "printerFanSwitch"
                checked: root.details.fanSpeed > 0
                enabled: root.ready
                onToggled: PrinterService.controlDevice("fan", checked ? 100 : 0)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.caps.fan && root.caps.fanPercentage
            Label {
                text: qsTr("Cooling fan")
                font: Typography.caption
                Layout.fillWidth: true
            }
            Slider {
                id: fan
                property real requestedValue: 0
                objectName: "printerFanTarget"
                Layout.preferredWidth: 130
                from: 0
                to: 100
                stepSize: 1
                enabled: root.ready
                Binding {
                    target: fan
                    property: "value"
                    value: root.details.fanSpeed
                    when: !fan.pressed
                    restoreMode: Binding.RestoreNone
                }
                Accessible.name: qsTr("Cooling fan")
                onPressedChanged: {
                    if (pressed)
                        requestedValue = value;
                    else if (enabled)
                        PrinterService.controlDevice("fan", requestedValue);
                }
                onMoved: {
                    requestedValue = value;
                    if (!pressed && enabled)
                        PrinterService.controlDevice("fan", requestedValue);
                }
            }
            Label {
                text: Math.round(fan.value) + "%"
                font.pixelSize: 12
                Layout.minimumWidth: 32
                horizontalAlignment: Text.AlignRight
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: PrinterService.cameraAvailable
            Label {
                Layout.fillWidth: true
                text: qsTr("Camera")
                font.pixelSize: 12
            }
            Switch {
                objectName: "printerCameraSwitch"
                checked: PrinterService.cameraSessionActive
                enabled: PrinterService.connected && PrinterService.cameraAvailable
                Accessible.name: qsTr("Camera")
                onToggled: checked ? PrinterService.startCameraStream() : PrinterService.stopCameraStream()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
        }
        Label {
            Layout.fillWidth: true
            visible: !root.caps.light && !root.caps.fan && !PrinterService.cameraAvailable
            text: qsTr("No auxiliary controls are available for this device.")
            wrapMode: Text.Wrap
            font: Typography.caption
        }
    }
    Label {
        Layout.fillWidth: true
        visible: root.section === 2 || root.section === 3
        text: qsTr("Controls are shown only when supported by the connected device.")
        font.pixelSize: 11
        color: root.SmoothUI.theme.res.textFillColorSecondary
        wrapMode: Text.Wrap
    }
    Label {
        Layout.fillWidth: true
        visible: PrinterService.deviceControlBusy || PrinterService.deviceControlError.length > 0 || PrinterService.deviceControlMessage.length > 0
        text: PrinterService.deviceControlBusy ? qsTr("Sending command…") : PrinterService.deviceControlError || PrinterService.deviceControlMessage
        font: Typography.caption
        wrapMode: Text.Wrap
        color: PrinterService.deviceControlError.length > 0 ? root.SmoothUI.theme.res.systemFillColorCaution : root.SmoothUI.theme.res.textFillColorSecondary
    }
    Label {
        Layout.fillWidth: true
        visible: !PrinterService.statusFresh
        text: qsTr("Connect to enable device controls.")
        wrapMode: Text.Wrap
        font: Typography.caption
    }

    component FilamentMetric: Rectangle {
        id: metric
        property string title
        property real current
        property real target
        readonly property string value: PrinterService.statusFresh ? qsTr("%1°C / %2°C").arg(Math.round(current)).arg(Math.round(target)) : "— / —"
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
    component TemperatureRow: Item {
        id: row
        property string label
        property string action
        property real current
        property real target
        property real limit
        Layout.fillWidth: true
        implicitHeight: 52
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: targetControls.left
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Label {
                text: row.label
                font.pixelSize: 12
            }
            Label {
                text: PrinterService.statusFresh ? qsTr("%1°C").arg(Math.round(row.current)) : "—"
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
        }
        RowLayout {
            id: targetControls
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7
            Label {
                text: qsTr("Target")
                font.pixelSize: 11
                color: root.SmoothUI.theme.res.textFillColorSecondary
            }
            TextField {
                id: input
                objectName: "printerTemperatureTarget." + row.action
                Layout.preferredWidth: 65
                implicitHeight: 30
                font.pixelSize: 12
                Binding {
                    target: input
                    property: "text"
                    value: row.target.toString()
                    when: !input.activeFocus
                    restoreMode: Binding.RestoreNone
                }
                enabled: root.ready
                validator: DoubleValidator {
                    bottom: 0
                    top: row.limit
                    notation: DoubleValidator.StandardNotation
                }
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                Accessible.name: row.label + " " + qsTr("Target temperature")
            }
            Label {
                text: "°C"
                font.pixelSize: 11
                color: root.SmoothUI.theme.res.textFillColorSecondary
            }
            Button {
                objectName: "printerTemperatureSet." + row.action
                text: qsTr("Set")
                implicitHeight: 27
                font.pixelSize: 12
                enabled: root.ready && input.acceptableInput
                onClicked: PrinterService.controlDevice(row.action, Number(input.text))
            }
        }
    }
    component MoveButton: Button {
        id: moveButton
        property string axis
        property int direction
        objectName: "printerMove." + axis + (direction > 0 ? "+" : "-")
        Layout.preferredWidth: 38
        Layout.preferredHeight: 36
        icon.width: 14
        icon.height: 14
        Accessible.name: axis.toUpperCase() + (direction > 0 ? "+" : "−")
        padding: 0
        leftPadding: 0
        rightPadding: 0
        contentItem: Label {
            text: moveButton.text
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: moveButton.enabled ? root.SmoothUI.theme.res.textFillColorPrimary : root.SmoothUI.theme.res.textFillColorDisabled
        }
        background: Rectangle {
            radius: 4
            color: root.SmoothUI.theme.res.subtleFillColorSecondary
            border.color: moveButton.hovered && moveButton.enabled ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark) : root.SmoothUI.theme.res.cardStrokeColorDefault
        }
        font: Typography.caption
        enabled: root.movable && root.caps.motion
        onClicked: PrinterService.controlDevice("move", direction * root.step, axis)
    }
}
