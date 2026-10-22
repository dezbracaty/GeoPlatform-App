import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import SmoothUI
import SmoothUI.Controls
import GPlatform

Item {
    id: root
    property var panelData: ({})
    property string viewType: "speed"
    readonly property var rows: panelData.rangeRows || []
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

    Flickable {
        id: viewport
        objectName: "slicing.preview.rangeViewport"
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: rangeColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        QQC.ScrollBar.vertical: QQC.ScrollBar {}

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
        }
    }
}
