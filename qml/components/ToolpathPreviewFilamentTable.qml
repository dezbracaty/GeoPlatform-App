import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import GPlatform

Item {
    id: root
    property var panelData: ({})
    property string viewType: "filament"
    readonly property var rows: panelData.filamentRows || []
    readonly property var usageColumns: panelData.usageColumns || []
    readonly property var footerRows: panelData.footerRows || []
    implicitHeight: Math.min(480, header.implicitHeight + filamentList.contentHeight + footer.implicitHeight + 14)

    readonly property int filamentWidth: 112
    readonly property int displayWidth: 56
    readonly property int usageWidth: usageColumns.length > 0
                                      ? Math.max(42, Math.min(72,
                                          (width - filamentWidth - displayWidth
                                           - (usageColumns.length + 1) * 4)
                                          / usageColumns.length))
                                      : 0

    readonly property int selectableRowCount: {
        var count = 0
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].displayEnabled !== false)
                ++count
        }
        return count
    }
    readonly property int visibleSelectableRowCount: {
        var count = 0
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].displayEnabled !== false && rows[i].visible)
                ++count
        }
        return count
    }
    readonly property int allRowsCheckState: selectableRowCount === 0
                                                 ? Qt.Unchecked
                                                 : visibleSelectableRowCount === 0
                                                   ? Qt.Unchecked
                                                   : visibleSelectableRowCount === selectableRowCount
                                                     ? Qt.Checked
                                                     : Qt.PartiallyChecked

    function localizedColumn(label) {
        var labels = {
            "Model": qsTr("Model"), "Support": qsTr("Support"),
            "Flushed": qsTr("Flush"), "Tower": qsTr("Prime Tower"), "Total": qsTr("Total")
        }
        return labels[label] || label
    }

    function localizedFilament(label) {
        var match = /^Filament (.+)$/.exec(String(label || ""))
        return match ? qsTr("Filament %1").arg(match[1]) : label
    }

    function localizedFooter(label) {
        var labels = {
            "Filament changes": qsTr("Filament Change"),
            "Tool changes": qsTr("Tool Change"),
            "Cost": qsTr("Cost")
        }
        return labels[label] || label
    }

    function restoreScrollPosition(position) {
        Qt.callLater(function() {
            var maximum = Math.max(0, filamentList.contentHeight - filamentList.height)
            filamentList.contentY = Math.max(0, Math.min(position, maximum))
        })
    }

    function setRowVisible(rowId, visible) {
        var position = filamentList.contentY
        SlicingPreviewBridge.setPreviewPanelRowVisible(viewType, rowId, visible)
        restoreScrollPosition(position)
    }

    function setAllRowsVisible(visible) {
        var position = filamentList.contentY
        SlicingPreviewBridge.setPreviewLegendGroupVisible(viewType, visible)
        restoreScrollPosition(position)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        GridLayout {
            id: header
            Layout.fillWidth: true
            columns: root.usageColumns.length + 2
            columnSpacing: 4
            Label { Layout.preferredWidth: root.filamentWidth; text: qsTr("Filament"); font: Typography.caption; color: Theme.of(root).res.textFillColorSecondary }
            Repeater {
                model: root.usageColumns
                delegate: Label {
                    required property var modelData
                    Layout.preferredWidth: root.usageWidth
                    text: root.localizedColumn(modelData.label)
                    horizontalAlignment: Text.AlignRight
                    font: Typography.caption
                    color: Theme.of(root).res.textFillColorSecondary
                }
            }
            CheckBox {
                Layout.preferredWidth: root.displayWidth
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("All")
                font: Typography.caption
                tristate: true
                enabled: root.selectableRowCount > 0
                checkState: root.allRowsCheckState
                nextCheckState: function() {
                    return checkState === Qt.Checked ? Qt.Unchecked : Qt.Checked
                }
                onClicked: root.setAllRowsVisible(checkState === Qt.Checked)
            }
        }

        ListView {
            id: filamentList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rows
            delegate: GridLayout {
                id: filamentRow
                required property var modelData
                width: filamentList.width
                height: Math.max(40, implicitHeight)
                columns: root.usageColumns.length + 2
                columnSpacing: 4
                RowLayout {
                    Layout.preferredWidth: root.filamentWidth
                    spacing: 6
                    Rectangle {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        radius: 2
                        color: modelData.color
                        opacity: modelData.visible ? 1.0 : 0.35
                        border.width: 1
                        border.color: Theme.of(root).res.controlStrokeColorDefault
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.localizedFilament(modelData.label)
                        elide: Text.ElideRight
                        font: Typography.caption
                        color: Theme.of(root).res.textFillColorPrimary
                    }
                }
                Repeater {
                    model: root.usageColumns
                    delegate: Label {
                        required property var modelData
                        Layout.preferredWidth: root.usageWidth
                        text: filamentRow.modelData[modelData.id]
                        horizontalAlignment: Text.AlignRight
                        font: Typography.caption
                        color: Theme.of(root).res.textFillColorPrimary
                    }
                }
                CheckBox {
                    Layout.preferredWidth: root.displayWidth
                    Layout.alignment: Qt.AlignHCenter
                    text: ""
                    checked: modelData.visible
                    enabled: modelData.displayEnabled
                    onClicked: root.setRowVisible(modelData.id, checked)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.of(root).res.dividerStrokeColorDefault
        }
        ColumnLayout {
            id: footer
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: root.footerRows
                delegate: GridLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.minimumHeight: 28
                    columns: 2
                    Label { Layout.fillWidth: true; text: root.localizedFooter(modelData.label); font: Typography.caption; color: Theme.of(root).res.textFillColorSecondary }
                    Label { Layout.preferredWidth: 100; text: modelData.value; horizontalAlignment: Text.AlignRight; font: Typography.caption; color: Theme.of(root).res.textFillColorPrimary }
                }
            }
        }
    }
}
