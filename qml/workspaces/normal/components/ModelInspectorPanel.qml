import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0
import "../../../components"

PanelSurface {
    id: root

    property bool expanded: false
    property real maximumHeight: 600
    readonly property color authoredColor: Qt.rgba(
                                                ModelPropertyBridge.diffuseColor.x,
                                                ModelPropertyBridge.diffuseColor.y,
                                                ModelPropertyBridge.diffuseColor.z,
                                                1.0)
    readonly property int filamentSlot: ModelPropertyBridge.filamentSlot

    signal expandRequested()
    signal collapseRequested()

    width: expanded ? 336 : 42
    height: expanded
            ? Math.min(maximumHeight, header.height + inspectorColumn.implicitHeight + 20)
            : 42
    clip: true
    surfaceRadius: 10

    Behavior on width {
        NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
    }
    Behavior on height {
        NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
    }

    ColorDialog {
        id: appearanceColorDialog
        title: qsTr("Model Appearance Color")
        selectedColor: root.authoredColor
        onAccepted: {
            ModelPropertyBridge.diffuseColor = Qt.vector3d(
                        selectedColor.r, selectedColor.g, selectedColor.b)
        }
    }

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 42
        color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)
        topLeftRadius: root.radius
        topRightRadius: root.radius
        bottomLeftRadius: root.expanded ? 0 : root.radius
        bottomRightRadius: root.expanded ? 0 : root.radius

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: root.expanded ? 12 : 5
            anchors.rightMargin: 5
            spacing: 8

            Icon {
                visible: root.expanded
                Layout.preferredWidth: 17
                Layout.preferredHeight: 17
                source: FluentIcons.graph_CubeShape
                color: Theme.res.textFillColorPrimary
            }

            ColumnLayout {
                visible: root.expanded
                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: ModelPropertyBridge.displayName || qsTr("Selected Model")
                    font: Typography.bodyStrong
                    color: Theme.res.textFillColorPrimary
                    elide: Text.ElideMiddle
                }
                Label {
                    Layout.fillWidth: true
                    text: ModelPropertyBridge.modelType
                    font: Typography.caption
                    color: Theme.res.textFillColorTertiary
                    elide: Text.ElideRight
                }
            }

            IconButton {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                flat: true
                radius: 6
                enabled: root.expanded || ModelPropertyBridge.hasSelectedModel
                icon.name: root.expanded
                           ? FluentIcons.graph_ChevronRightSmall
                           : FluentIcons.graph_Info
                icon.width: 15
                icon.height: 15
                onClicked: root.expanded
                           ? root.collapseRequested()
                           : root.expandRequested()

                ToolTip {
                    visible: parent.hovered
                    text: root.expanded
                          ? qsTr("Collapse model inspector")
                          : ModelPropertyBridge.hasSelectedModel
                            ? qsTr("Expand model inspector")
                            : qsTr("Select a model to inspect it")
                }
            }
        }

        Rectangle {
            visible: root.expanded
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.res.dividerStrokeColorDefault
        }
    }

    ScrollView {
        id: scrollView
        visible: root.expanded
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        clip: true
        leftPadding: 12
        rightPadding: 12
        topPadding: 10
        bottomPadding: 10
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            id: inspectorColumn
            width: scrollView.availableWidth
            spacing: 10

            Label {
                text: qsTr("Dimensions")
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 6
                rowSpacing: 6

                MetricCard {
                    label: qsTr("X")
                    value: root.millimeterText(ModelPropertyBridge.boundsSize.x)
                }
                MetricCard {
                    label: qsTr("Y")
                    value: root.millimeterText(ModelPropertyBridge.boundsSize.y)
                }
                MetricCard {
                    label: qsTr("Z")
                    value: root.millimeterText(ModelPropertyBridge.boundsSize.z)
                }
            }

            CompactValueRow {
                label: qsTr("Position")
                value: root.vectorText(ModelPropertyBridge.position, qsTr("mm"))
            }
            CompactValueRow {
                label: qsTr("Rotation")
                value: root.vectorText(ModelPropertyBridge.rotation, qsTr("°"))
            }
            CompactValueRow {
                label: qsTr("Scale")
                value: root.vectorText(ModelPropertyBridge.scale, "")
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            Label {
                text: qsTr("Geometry")
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 6
                rowSpacing: 6

                MetricCard {
                    label: qsTr("Parts")
                    value: root.countText(ModelPropertyBridge.partCount)
                }
                MetricCard {
                    label: qsTr("Vertices")
                    value: root.countText(ModelPropertyBridge.vertexCount)
                }
                MetricCard {
                    label: qsTr("Triangles")
                    value: root.countText(ModelPropertyBridge.triangleCount)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            Label {
                text: qsTr("Appearance")
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 34
                    padding: 0
                    Accessible.name: qsTr("Change model appearance color")
                    onClicked: appearanceColorDialog.open()
                    background: Rectangle {
                        radius: 6
                        color: root.authoredColor
                        border.width: 1
                        border.color: Theme.res.controlStrokeColorDefault
                    }
                    contentItem: Item {}
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Model color")
                        font: Typography.bodyStrong
                        color: Theme.res.textFillColorPrimary
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.filamentSlot > 0
                              ? qsTr("Overridden by slot %1").arg(root.filamentSlot)
                              : root.filamentSlot < 0
                                ? qsTr("Mixed filament assignments")
                                : qsTr("Independent from filament")
                        font: Typography.caption
                        color: Theme.res.textFillColorSecondary
                        elide: Text.ElideRight
                    }
                }

                Button {
                    text: qsTr("Change")
                    onClicked: appearanceColorDialog.open()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Printing filament")
                    font: Typography.caption
                    color: Theme.res.textFillColorSecondary
                }
                Button {
                    visible: root.filamentSlot !== 0
                    text: qsTr("Unbind")
                    flat: true
                    onClicked: ModelPropertyBridge.useProjectDefaultFilament()
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.filamentSlot === 0
                      ? qsTr("Not bound · slicing uses the project default slot")
                      : root.filamentSlot < 0
                        ? qsTr("Parts use mixed filament assignments")
                        : qsTr("Bound to slot %1 · display and slicing follow this slot")
                          .arg(root.filamentSlot)
                font: Typography.caption
                color: Theme.res.textFillColorTertiary
                wrapMode: Text.WordWrap
            }

            Flow {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: ModelPropertyBridge.filamentSlots

                    delegate: Button {
                        id: slotButton
                        required property int index
                        required property var modelData
                        readonly property bool selected: root.filamentSlot === index + 1
                        width: 70
                        height: 48
                        padding: 3
                        highlighted: selected
                        Accessible.name: qsTr("Bind model to slot %1, %2")
                                         .arg(modelData.number)
                                         .arg(modelData.materialType || qsTr("Unknown"))
                        onClicked: ModelPropertyBridge.assignFilamentSlot(index + 1)

                        background: Rectangle {
                            radius: 6
                            color: slotButton.selected
                                   ? Theme.res.subtleFillColorSecondary
                                   : Theme.res.controlFillColorDefault
                            border.width: slotButton.selected ? 2 : 1
                            border.color: slotButton.selected
                                          ? Theme.accentColor.defaultBrushFor()
                                          : Theme.res.cardStrokeColorDefault
                        }
                        contentItem: RowLayout {
                            spacing: 4
                            Rectangle {
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                                radius: 4
                                color: slotButton.modelData.color || "#b0bec5"
                                border.width: 1
                                border.color: Theme.res.controlStrokeColorDefault
                            }
                            Label {
                                Layout.fillWidth: true
                                text: slotButton.modelData.number
                                horizontalAlignment: Text.AlignHCenter
                                font: Typography.bodyStrong
                                color: Theme.res.textFillColorPrimary
                            }
                        }
                        ToolTip {
                            delay: Theme.tooltipDelay
                            visible: slotButton.hovered
                            text: qsTr("Slot %1 · %2\n%3")
                                  .arg(slotButton.modelData.number)
                                  .arg(slotButton.modelData.materialType || qsTr("Unknown"))
                                  .arg(slotButton.modelData.presetName || slotButton.modelData.presetId)
                        }
                    }
                }
            }
        }
    }

    function millimeterText(value) {
        return Number(value).toFixed(2) + " " + qsTr("mm")
    }

    function vectorText(vector, unit) {
        var suffix = unit.length > 0 ? " " + unit : ""
        return Number(vector.x).toFixed(2) + " / "
                + Number(vector.y).toFixed(2) + " / "
                + Number(vector.z).toFixed(2) + suffix
    }

    function countText(value) {
        return Number(value).toLocaleString(Qt.locale(), "f", 0)
    }

    component MetricCard: Rectangle {
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        Layout.preferredHeight: 48
        radius: 6
        color: Theme.res.subtleFillColorSecondary

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 1
            Label {
                Layout.fillWidth: true
                text: parent.parent.label
                horizontalAlignment: Text.AlignHCenter
                font: Typography.caption
                color: Theme.res.textFillColorTertiary
            }
            Label {
                Layout.fillWidth: true
                text: parent.parent.value
                horizontalAlignment: Text.AlignHCenter
                font: Typography.bodyStrong
                color: Theme.res.textFillColorPrimary
                elide: Text.ElideRight
            }
        }
    }

    component CompactValueRow: RowLayout {
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: 8
        Label {
            Layout.preferredWidth: 62
            text: parent.label
            font: Typography.caption
            color: Theme.res.textFillColorTertiary
        }
        Label {
            Layout.fillWidth: true
            text: parent.value
            horizontalAlignment: Text.AlignRight
            font: Typography.caption
            color: Theme.res.textFillColorPrimary
            elide: Text.ElideRight
        }
    }
}
