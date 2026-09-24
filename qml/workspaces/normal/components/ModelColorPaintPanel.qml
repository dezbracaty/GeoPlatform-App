import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import GPlatform 1.0

Item {
    id: root
    objectName: "modelColorPaintPanel"
    anchors.fill: parent

    readonly property string currentTool: ModelColorPaintBridge.toolType
    readonly property bool brushTool: currentTool === "circle" || currentTool === "sphere"
    property bool remapExpanded: false

    function selectTool(tool) {
        ModelColorPaintBridge.toolType = tool
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        leftPadding: 16
        rightPadding: 16
        topPadding: 14
        bottomPadding: 14
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: 12

            Label {
                text: qsTr("Filament slots")
                color: Theme.res.textFillColorSecondary
                font: Typography.caption
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: filamentContent.implicitHeight + 20
                radius: 8
                color: Theme.res.subtleFillColorSecondary

                ColumnLayout {
                    id: filamentContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 8

                    Flow {
                        id: paletteFlow
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            id: paletteRepeater
                            model: ModelColorPaintBridge.filamentSlots

                            delegate: Item {
                                id: slotChip
                                required property int index
                                required property var modelData
                                readonly property color displayColor: modelData.color || "#b0bec5"
                                readonly property bool selected: ModelColorPaintBridge.currentColorIndex === index + 1
                                width: 64
                                height: 52

                                Button {
                                    id: selectSlotButton
                                    anchors.fill: parent
                                    padding: 4
                                    highlighted: slotChip.selected
                                    Accessible.name: qsTr("Select slot %1, %2")
                                                     .arg(slotChip.modelData.number)
                                                     .arg(slotChip.modelData.materialType || qsTr("Unknown"))
                                    onClicked: ModelColorPaintBridge.currentColorIndex = slotChip.index + 1

                                    background: Rectangle {
                                        radius: 6
                                        color: slotChip.selected
                                               ? Theme.res.subtleFillColorSecondary
                                               : Theme.res.controlFillColorDefault
                                        border.width: slotChip.selected ? 2 : 1
                                        border.color: slotChip.selected
                                                      ? Theme.accentColor.defaultBrushFor()
                                                      : Theme.res.cardStrokeColorDefault
                                    }

                                    contentItem: ColumnLayout {
                                        spacing: 3

                                        Rectangle {
                                            Layout.preferredWidth: 20
                                            Layout.preferredHeight: 20
                                            Layout.alignment: Qt.AlignHCenter
                                            radius: 5
                                            color: slotChip.displayColor
                                            border.width: 1
                                            border.color: Theme.res.controlStrokeColorDefault
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: qsTr("%1 · %2")
                                                  .arg(slotChip.modelData.number)
                                                  .arg(slotChip.modelData.materialType || qsTr("Unknown"))
                                            horizontalAlignment: Text.AlignHCenter
                                            color: Theme.res.textFillColorPrimary
                                            font: Typography.caption
                                            elide: Text.ElideRight
                                        }
                                    }

                                    ToolTip {
                                        delay: Theme.tooltipDelay
                                        visible: selectSlotButton.hovered
                                        text: qsTr("Slot %1 · %2\n%3")
                                              .arg(slotChip.modelData.number)
                                              .arg(slotChip.modelData.materialType || qsTr("Unknown"))
                                              .arg(slotChip.modelData.presetName || slotChip.modelData.presetId)
                                    }
                                }

                            }
                        }

                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: qsTr("Unpainted area")
                            color: Theme.res.textFillColorSecondary
                            font: Typography.caption
                        }
                        ComboBox {
                            Layout.fillWidth: true
                            model: ModelColorPaintBridge.filamentSlots.map(
                                       function(slot) {
                                           return qsTr("Slot %1 · %2")
                                               .arg(slot.number)
                                               .arg(slot.materialType || qsTr("Unknown"))
                                       })
                            currentIndex: Math.max(0,
                                                   ModelColorPaintBridge.defaultFilamentSlot - 1)
                            onActivated: function(slotIndex) {
                                ModelColorPaintBridge.defaultFilamentSlot = slotIndex + 1
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Model labels correspond to filament slots. Press 1–%1 to select a slot; Shift + left mouse erases.")
                              .arg(Math.min(9, ModelColorPaintBridge.filamentSlots.length))
                        color: Theme.res.textFillColorTertiary
                        font: Typography.caption
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Label {
                text: qsTr("Tool type")
                color: Theme.res.textFillColorSecondary
                font: Typography.caption
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Repeater {
                    model: [
                        { key: "circle", tip: qsTr("Circle brush — drag to paint a circular area projected from the current view."), icon: FluentIcons.graph_CircleRing },
                        { key: "sphere", tip: qsTr("Sphere brush — drag to paint every surface triangle inside a 3D sphere."), icon: FluentIcons.graph_CircleShapeSolid },
                        { key: "triangle", tip: qsTr("Triangle — click to paint the single triangle under the pointer."), icon: FluentIcons.graph_IncidentTriangle },
                        { key: "height_range", tip: qsTr("Height Range — click to paint surfaces within the configured vertical height band."), icon: FluentIcons.graph_Ruler },
                        { key: "fill", tip: qsTr("Fill — click a connected color region; Edge detection limits filling across sharp angles."), icon: FluentIcons.graph_InkingToolFill },
                        { key: "gap_fill", tip: qsTr("Gap Fill — preview small color regions below the area threshold, then use Perform gap fill to merge them into a neighboring color."), icon: FluentIcons.graph_HolePunchOff }
                    ]

                    IconButton {
                        objectName: "model.color.paint.tool." + modelData.key
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 36
                        highlighted: root.currentTool === modelData.key
                        icon.name: modelData.icon
                        icon.width: 17
                        icon.height: 17
                        radius: 6
                        onClicked: root.selectTool(modelData.key)

                        ToolTip.delay: Theme.tooltipDelay
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.tip
                    }
                }

                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.brushTool

                Label {
                    text: qsTr("Pen size  %1 mm").arg(ModelColorPaintBridge.brushSize.toFixed(1))
                    color: Theme.res.textFillColorSecondary
                    font: Typography.caption
                }
                Slider {
                    Layout.fillWidth: true
                    from: 0.5
                    to: 100
                    stepSize: 0.5
                    value: ModelColorPaintBridge.brushSize
                    onMoved: ModelColorPaintBridge.brushSize = value
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.currentTool === "fill"

                CheckBox {
                    text: qsTr("Edge detection")
                    checked: ModelColorPaintBridge.edgeDetection
                    onToggled: ModelColorPaintBridge.edgeDetection = checked
                }

                Label {
                    visible: ModelColorPaintBridge.edgeDetection
                    text: qsTr("Smart fill angle  %1°").arg(
                              ModelColorPaintBridge.smartFillAngle.toFixed(0))
                    color: Theme.res.textFillColorSecondary
                    font: Typography.caption
                }
                Slider {
                    Layout.fillWidth: true
                    visible: ModelColorPaintBridge.edgeDetection
                    from: 0
                    to: 90
                    stepSize: 1
                    value: ModelColorPaintBridge.smartFillAngle
                    onMoved: ModelColorPaintBridge.smartFillAngle = value
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.currentTool === "height_range"

                Label {
                    text: qsTr("Height range  %1 mm").arg(
                              ModelColorPaintBridge.heightRange.toFixed(2))
                    color: Theme.res.textFillColorSecondary
                    font: Typography.caption
                }
                Slider {
                    Layout.fillWidth: true
                    from: 0.1
                    to: 8
                    stepSize: 0.1
                    value: ModelColorPaintBridge.heightRange
                    onMoved: ModelColorPaintBridge.heightRange = value
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.currentTool === "gap_fill"

                Label {
                    text: qsTr("Gap area  %1 mm²").arg(ModelColorPaintBridge.gapArea.toFixed(2))
                    color: Theme.res.textFillColorSecondary
                    font: Typography.caption
                }
                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: 5
                    stepSize: 0.05
                    value: ModelColorPaintBridge.gapArea
                    onMoved: ModelColorPaintBridge.gapArea = value
                }
                Label {
                    Layout.fillWidth: true
                    text: ModelColorPaintBridge.gapPreviewPending
                          ? qsTr("Finding small color regions…")
                          : ModelColorPaintBridge.gapCandidateCount > 0
                            ? qsTr("%1 candidate triangles are previewed. Gap Fill is global; apply them with the button below.").arg(ModelColorPaintBridge.gapCandidateCount)
                            : qsTr("No color regions smaller than the current area threshold were found.")
                    color: ModelColorPaintBridge.gapCandidateCount > 0
                           ? Theme.res.textFillColorSecondary
                           : Theme.res.textFillColorTertiary
                    font: Typography.caption
                    wrapMode: Text.WordWrap
                }
                Button {
                    text: qsTr("Perform gap fill")
                    highlighted: true
                    enabled: !ModelColorPaintBridge.gapPreviewPending
                             && ModelColorPaintBridge.gapCandidateCount > 0
                    onClicked: ModelColorPaintBridge.performGapFill()
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Commit all currently previewed gap candidates to the model's surface colors.")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                CheckBox {
                    text: qsTr("Vertical")
                    checked: ModelColorPaintBridge.verticalOnly
                    onToggled: ModelColorPaintBridge.verticalOnly = checked
                }
                CheckBox {
                    text: qsTr("Horizontal")
                    checked: ModelColorPaintBridge.horizontalOnly
                    onToggled: ModelColorPaintBridge.horizontalOnly = checked
                }
                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            Button {
                text: root.remapExpanded ? qsTr("Hide filament remapping")
                                         : qsTr("Remap filaments")
                onClicked: root.remapExpanded = !root.remapExpanded
            }

            ColumnLayout {
                id: remapPanel
                Layout.fillWidth: true
                spacing: 6
                visible: root.remapExpanded

                Repeater {
                    id: remapRepeater
                    model: ModelColorPaintBridge.palette

                    RowLayout {
                        required property int index
                        readonly property int currentTarget: remapTarget.currentIndex
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            radius: 4
                            color: remapRepeater.model[index]
                            border.width: 1
                            border.color: Theme.res.dividerStrokeColorDefault
                        }
                        Label {
                            text: qsTr("Filament %1").arg(index + 1)
                            Layout.fillWidth: true
                        }
                        ComboBox {
                            id: remapTarget
                            property int sourceIndex: index
                            Layout.preferredWidth: 120
                            model: ModelColorPaintBridge.palette.map(
                                       function(_, targetIndex) {
                                           return qsTr("Filament %1").arg(targetIndex + 1)
                                       })
                            currentIndex: index
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: qsTr("Remap")
                        highlighted: true
                        onClicked: {
                            var mapping = []
                            for (var i = 0; i < remapRepeater.count; ++i)
                                mapping.push(remapRepeater.itemAt(i).currentTarget)
                            ModelColorPaintBridge.remapFilaments(mapping)
                        }
                    }
                    Button {
                        text: qsTr("Cancel")
                        onClicked: root.remapExpanded = false
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Left mouse: paint  ·  Shift + left mouse: erase\nCtrl + wheel: tool value")
                color: Theme.res.textFillColorTertiary
                font: Typography.caption
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: qsTr("Erase")
                    highlighted: ModelColorPaintBridge.currentColorIndex === 0
                    onClicked: ModelColorPaintBridge.currentColorIndex = 0
                }
                Button {
                    text: qsTr("Erase all painting")
                    onClicked: ModelColorPaintBridge.clearPainting()
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "model.color.paint.done"
                    text: ModelColorPaintBridge.finishing
                          ? qsTr("Saving…") : qsTr("Done")
                    highlighted: true
                    enabled: !ModelColorPaintBridge.finishing
                    onClicked: ModelColorPaintBridge.leavePainting()
                }
            }
        }
    }
}
