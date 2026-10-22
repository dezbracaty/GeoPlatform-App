import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import SmoothUI
import SmoothUI.Controls
import GPlatform

Item {
    id: root
    property var panelData: ({})
    property string viewType: "lineType"
    property var rows: panelData.featureRows || []
    implicitHeight: Math.min(480, header.implicitHeight + featureList.contentHeight + 6)

    readonly property int colorWidth: Math.ceil(Math.max(18, colorHeading.implicitWidth))
    readonly property int timeWidth: 50
    readonly property int percentWidth: 32
    readonly property int displayWidth: allToggle.implicitWidth
    readonly property real availableTextWidth: Math.max(
                                                  0, width - colorWidth - timeWidth
                                                  - percentWidth - displayWidth - 25)
    readonly property real usageWidth: Math.min(125, availableTextWidth * 0.5)
    readonly property real labelWidth: availableTextWidth - usageWidth

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
        // Snapshot the rows: each setter refreshes the bridge model synchronously.
        var items = rows.slice()
        for (var i = 0; i < items.length; ++i) {
            if (items[i].displayEnabled !== false)
                SlicingPreviewBridge.setPreviewPanelRowVisible(viewType, items[i].id, visible)
        }
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
            Label {
                id: colorHeading
                Layout.preferredWidth: root.colorWidth
                Layout.minimumWidth: root.colorWidth
                Layout.maximumWidth: root.colorWidth
                text: qsTr("Color")
                font: Typography.caption
                color: Theme.of(root).res.textFillColorSecondary
            }
            Label {
                Layout.preferredWidth: root.labelWidth
                Layout.minimumWidth: root.labelWidth
                Layout.maximumWidth: root.labelWidth
                text: qsTr("Type")
                elide: Text.ElideRight
                font: Typography.caption
                color: Theme.of(root).res.textFillColorSecondary
            }
            Label {
                Layout.preferredWidth: root.timeWidth
                Layout.minimumWidth: root.timeWidth
                Layout.maximumWidth: root.timeWidth
                text: qsTr("Time")
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                font: Typography.caption
                color: Theme.of(root).res.textFillColorSecondary
            }
            Label {
                Layout.preferredWidth: root.percentWidth
                Layout.minimumWidth: root.percentWidth
                Layout.maximumWidth: root.percentWidth
                text: "%"
                horizontalAlignment: Text.AlignRight
                font: Typography.caption
                color: Theme.of(root).res.textFillColorSecondary
            }
            Label {
                Layout.preferredWidth: root.usageWidth
                Layout.minimumWidth: root.usageWidth
                Layout.maximumWidth: root.usageWidth
                text: qsTr("Usage")
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                font: Typography.caption
                color: Theme.of(root).res.textFillColorSecondary
            }
            CheckBox {
                id: allToggle
                objectName: "slicing.preview.legendAll"
                Layout.preferredWidth: root.displayWidth
                Layout.minimumWidth: root.displayWidth
                Layout.maximumWidth: root.displayWidth
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
            objectName: "slicing.preview.legendList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            QQC.ScrollBar.vertical: QQC.ScrollBar {}
            model: root.rows
            delegate: Rectangle {
                required property var modelData
                required property int index
                objectName: "slicing.preview.legendRow." + modelData.id
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
                        Layout.minimumWidth: root.colorWidth
                        Layout.maximumWidth: root.colorWidth
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
                        Layout.minimumWidth: root.labelWidth
                        Layout.maximumWidth: root.labelWidth
                        text: root.localizedLabel(modelData.label)
                        elide: Text.ElideRight
                        QQC.ToolTip.visible: truncated && typeHover.hovered
                        QQC.ToolTip.text: text
                        HoverHandler { id: typeHover }
                        font: Typography.caption
                        color: modelData.visible
                               ? Theme.of(root).res.textFillColorPrimary
                               : Theme.of(root).res.textFillColorTertiary
                    }
                    Label {
                        Layout.preferredWidth: root.timeWidth
                        Layout.minimumWidth: root.timeWidth
                        Layout.maximumWidth: root.timeWidth
                        text: modelData.time
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignRight
                        font: Typography.caption
                        color: Theme.of(root).res.textFillColorPrimary
                    }
                    Label {
                        Layout.preferredWidth: root.percentWidth
                        Layout.minimumWidth: root.percentWidth
                        Layout.maximumWidth: root.percentWidth
                        text: modelData.percent
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignRight
                        font: Typography.caption
                        color: Theme.of(root).res.textFillColorPrimary
                    }
                    Label {
                        Layout.preferredWidth: root.usageWidth
                        Layout.minimumWidth: root.usageWidth
                        Layout.maximumWidth: root.usageWidth
                        text: modelData.usageSecondary
                              ? modelData.usagePrimary + " · " + root.localizedUsage(modelData.usageSecondary)
                              : modelData.usagePrimary
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        QQC.ToolTip.visible: truncated && usageHover.hovered
                        QQC.ToolTip.text: text
                        HoverHandler { id: usageHover }
                        font: Typography.caption
                        color: Theme.of(root).res.textFillColorPrimary
                    }
                    CheckBox {
                        objectName: "slicing.preview.legendToggle." + modelData.id
                        Layout.preferredWidth: root.displayWidth
                        Layout.minimumWidth: root.displayWidth
                        Layout.maximumWidth: root.displayWidth
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
