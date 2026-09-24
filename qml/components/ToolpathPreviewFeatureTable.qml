import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import GPlatform

Item {
    id: root
    property var panelData: ({})
    property string viewType: "lineType"
    readonly property var rows: panelData.featureRows || []
    implicitHeight: Math.min(480, header.implicitHeight + featureList.contentHeight + 6)

    readonly property int colorWidth: 18
    readonly property int labelWidth: 76
    readonly property int timeWidth: 50
    readonly property int percentWidth: 32
    readonly property int usageWidth: 125
    readonly property int displayWidth: 50

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

    function localizedLabel(label) {
        var labels = {
            "Undefined": qsTr("Undefined"), "Inner wall": qsTr("Inner Wall"),
            "Outer wall": qsTr("Outer Wall"), "Overhang wall": qsTr("Overhang Wall"),
            "Sparse infill": qsTr("Sparse Infill"), "Internal solid infill": qsTr("Internal Solid Infill"),
            "Top surface": qsTr("Top Surface"), "Bottom surface": qsTr("Bottom Surface"),
            "Ironing": qsTr("Ironing"), "Bridge": qsTr("Bridge"),
            "Internal Bridge": qsTr("Internal Bridge"), "Gap infill": qsTr("Gap Infill"),
            "Skirt": qsTr("Skirt"), "Brim": qsTr("Raft"), "Support": qsTr("Support"),
            "Support interface": qsTr("Support Interface"), "Support transition": qsTr("Support Transition"),
            "Prime tower": qsTr("Prime Tower"), "Custom": qsTr("Custom"),
            "Multiple": qsTr("Multiple Types"),
            "Continuous fiber contour": qsTr("Continuous Fiber Contour"),
            "Continuous fiber infill": qsTr("Continuous Fiber Infill"),
            "Travel": qsTr("Travel"),
            "Wipe": qsTr("Wipe"), "Seam": qsTr("Seam")
        }
        return labels[label] || label
    }

    function localizedUsage(text) {
        return String(text || "").replace(" occurrences", qsTr(" times"))
    }

    function restoreScrollPosition(position) {
        Qt.callLater(function() {
            var maximum = Math.max(0, featureList.contentHeight - featureList.height)
            featureList.contentY = Math.max(0, Math.min(position, maximum))
        })
    }

    function setRowVisible(rowId, visible) {
        var position = featureList.contentY
        SlicingPreviewBridge.setPreviewPanelRowVisible(viewType, rowId, visible)
        restoreScrollPosition(position)
    }

    function setAllRowsVisible(visible) {
        var position = featureList.contentY
        SlicingPreviewBridge.setPreviewLegendGroupVisible(viewType, visible)
        restoreScrollPosition(position)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 3

        GridLayout {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            columns: 6
            columnSpacing: 5
            Label { Layout.preferredWidth: root.colorWidth; text: qsTr("Color"); font: Typography.caption; color: Theme.of(root).res.textFillColorSecondary }
            Label { Layout.preferredWidth: root.labelWidth; text: qsTr("Type"); font: Typography.caption; color: Theme.of(root).res.textFillColorSecondary }
            Label { Layout.preferredWidth: root.timeWidth; text: qsTr("Time"); horizontalAlignment: Text.AlignRight; font: Typography.caption; color: Theme.of(root).res.textFillColorSecondary }
            Label { Layout.preferredWidth: root.percentWidth; text: "%"; horizontalAlignment: Text.AlignRight; font: Typography.caption; color: Theme.of(root).res.textFillColorSecondary }
            Label { Layout.preferredWidth: root.usageWidth; text: qsTr("Usage"); horizontalAlignment: Text.AlignRight; font: Typography.caption; color: Theme.of(root).res.textFillColorSecondary }
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
            id: featureList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rows
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: featureList.width
                height: 34
                radius: PrintWorkspaceStyle.controlRadius
                color: rowHover.hovered
                       ? Theme.of(root).res.subtleFillColorSecondary
                       : "transparent"

                HoverHandler { id: rowHover }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.of(root).res.dividerStrokeColorDefault
                }

                GridLayout {
                    anchors.fill: parent
                    columns: 6
                    columnSpacing: 5
                    Item {
                        Layout.preferredWidth: root.colorWidth
                        Layout.fillHeight: true
                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14
                            height: 14
                            radius: 3
                            color: modelData.color
                            opacity: modelData.visible ? 1.0 : 0.35
                            border.width: 1
                            border.color: Theme.of(root).res.controlStrokeColorDefault
                        }
                    }
                    Label {
                        Layout.preferredWidth: root.labelWidth
                        text: root.localizedLabel(modelData.label)
                        elide: Text.ElideRight
                        font: Typography.caption
                        color: modelData.visible
                               ? Theme.of(root).res.textFillColorPrimary
                               : Theme.of(root).res.textFillColorTertiary
                    }
                    Label {
                        Layout.preferredWidth: root.timeWidth
                        text: modelData.time
                        horizontalAlignment: Text.AlignRight
                        font: Typography.caption
                        color: Theme.of(root).res.textFillColorPrimary
                    }
                    Label {
                        Layout.preferredWidth: root.percentWidth
                        text: modelData.percent
                        horizontalAlignment: Text.AlignRight
                        font: Typography.caption
                        color: Theme.of(root).res.textFillColorPrimary
                    }
                    Label {
                        Layout.preferredWidth: root.usageWidth
                        text: modelData.usageSecondary
                              ? modelData.usagePrimary + " · " + root.localizedUsage(modelData.usageSecondary)
                              : modelData.usagePrimary
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        font: Typography.caption
                        color: Theme.of(root).res.textFillColorPrimary
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
        }
    }
}
