import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls

Item {
    id: root
    property var panelData: ({})
    property string viewType: "summary"
    readonly property var rows: panelData.summaryRows || []
    implicitHeight: summaryColumn.implicitHeight

    function localizedLabel(label) {
        var labels = {
            "Total": qsTr("Total"),
            "Cost": qsTr("Cost"),
            "Total time": qsTr("Total Time")
        }
        return labels[label] || label
    }

    // Each row is one fixed two-column grid row.
    ColumnLayout {
        id: summaryColumn
        anchors.fill: parent
        spacing: 0
        Repeater {
            model: root.rows
            delegate: GridLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.minimumHeight: 28
                columns: 2
                columnSpacing: 16
                Label {
                    Layout.preferredWidth: 110
                    text: root.localizedLabel(modelData.label)
                    color: Theme.of(root).res.textFillColorSecondary
                    font: Typography.caption
                }
                Label {
                    Layout.fillWidth: true
                    Layout.rightMargin: 10
                    horizontalAlignment: Text.AlignRight
                    text: modelData.value
                    color: Theme.of(root).res.textFillColorPrimary
                    font: Typography.bodyStrong
                }
            }
        }
    }
}
