import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import GPlatform

Item {
    id: root
    property var panelData: ({})
    property string viewType: "speed"
    readonly property var rows: panelData.rangeRows || []
    readonly property var optionRows: panelData.optionRows || []
    implicitHeight: rangeColumn.implicitHeight

    function localizedTitle() {
        var titles = {
            speed: qsTr("Speed (mm/s)"), actualSpeed: qsTr("Actual Speed (mm/s)"),
            acceleration: qsTr("Acceleration (mm/s²)"), jerk: qsTr("Jerk (mm/s)"),
            layerHeight: qsTr("Layer Height (mm)"), lineWidth: qsTr("Line Width (mm)"),
            volumetricFlow: qsTr("Volumetric Flow (mm³/s)"),
            actualVolumetricFlow: qsTr("Actual Volumetric Flow (mm³/s)"),
            layerTime: qsTr("Layer Time (s)"), layerTimeLog: qsTr("Layer Time (log)"),
            fanSpeed: qsTr("Fan Speed (%)"), temperature: qsTr("Temperature (°C)"),
            pressureAdvance: qsTr("Pressure Advance")
        }
        return titles[root.viewType] || root.panelData.rangeTitle || ""
    }

    function localizedOptionLabel(label) {
        return label === "Travel" ? qsTr("Travel") : label
    }

    ColumnLayout {
        id: rangeColumn
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 4

        Label {
            Layout.fillWidth: true
            text: root.localizedTitle()
            font: Typography.caption
            color: Theme.of(root).res.textFillColorSecondary
        }
        Repeater {
            model: root.rows
            delegate: GridLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.minimumHeight: 24
                columns: 2
                columnSpacing: 10
                Rectangle {
                    Layout.preferredWidth: 82
                    Layout.preferredHeight: 18
                    color: modelData.color
                }
                Label {
                    Layout.fillWidth: true
                    Layout.rightMargin: 10
                    text: root.localizedOptionLabel(modelData.label)
                    horizontalAlignment: Text.AlignRight
                    font: Typography.caption
                    color: Theme.of(root).res.textFillColorPrimary
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.topMargin: 5
            color: Theme.of(root).res.dividerStrokeColorDefault
            visible: root.optionRows.length > 0
        }
        Label {
            text: qsTr("Display Options")
            visible: root.optionRows.length > 0
            font: Typography.caption
            color: Theme.of(root).res.textFillColorSecondary
        }
        Repeater {
            model: root.optionRows
            delegate: GridLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.minimumHeight: 28
                columns: 3
                columnSpacing: 8
                Rectangle {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    radius: 2
                    color: modelData.color
                    opacity: modelData.visible ? 1.0 : 0.35
                }
                Label {
                    Layout.fillWidth: true
                    text: modelData.label
                    font: Typography.caption
                    color: Theme.of(root).res.textFillColorPrimary
                }
                CheckBox {
                    Layout.preferredWidth: 32
                    text: ""
                    checked: modelData.visible
                    enabled: modelData.displayEnabled
                    onToggled: SlicingPreviewBridge.setPreviewPanelRowVisible(
                                   root.viewType, modelData.id, checked)
                }
            }
        }
    }
}
