import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform
import Render 1.0

Item {
    id: root
    objectName: "sceneSettingsPanelContent"

    anchors.fill: parent
    SmoothUI.theme: Theme.of(root)

    Flickable {
        id: scrollView
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        contentWidth: width
        contentHeight: contentColumn.implicitHeight

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }

        ColumnLayout {
            id: contentColumn
            width: scrollView.width
            spacing: 8

            InspectorSection {
                title: qsTr("View Elements")

                InspectorRow {
                    label: qsTr("Show Grid")
                    Switch {
                        checked: PrintBedPropertyBridge.showGrid
                        onCheckedChanged: PrintBedPropertyBridge.showGrid = checked
                    }
                }

                InspectorRow {
                    label: qsTr("Grid Spacing")
                    enabled: PrintBedPropertyBridge.showGrid
                    InspectorNumberField {
                        value: PrintBedPropertyBridge.gridSpacing
                        minimumValue: 1
                        maximumValue: 50
                        precision: 1
                        unit: "mm"
                        onValueEdited: function(newValue) {
                            PrintBedPropertyBridge.gridSpacing = newValue
                        }
                    }
                }

                InspectorRow {
                    label: qsTr("Show Bounding Box")
                    Switch {
                        checked: PrintBedPropertyBridge.showBounds
                        onCheckedChanged: PrintBedPropertyBridge.showBounds = checked
                    }
                }

            }

            InspectorSection {
                title: qsTr("Background")

                InspectorRow {
                    label: qsTr("Skybox Preset")
                    ComboBox {
                        Layout.preferredWidth: 224
                        Layout.minimumWidth: 224
                        implicitHeight: PrintWorkspaceStyle.controlHeight
                        model: [qsTr("Standard"), qsTr("3D Printing Studio"), qsTr("Sunset"), qsTr("Night"), qsTr("Solid Color")]
                        onActivated: function(index) {
                            var presets = ["normal", "print-studio", "editing", "technical", "minimal"]
                            if (index >= 0 && index < presets.length) {
                                ActionManager.triggerAction("scene.skybox.preset", {
                                    "preset": presets[index]
                                })
                            }
                        }
                    }
                }
            }

            InspectorSection {
                title: qsTr("Build Plate Appearance")

                InspectorRow {
                    label: qsTr("Rendering Mode")
                    SegmentedControl {
                        Layout.preferredWidth: 224
                        Layout.minimumWidth: 224
                        height: PrintWorkspaceStyle.controlHeight
                        currentIndex: PrintBedPropertyBridge.renderingMode === 1 ? 1 : 0

                        SegmentedButton {
                            width: 112
                            height: PrintWorkspaceStyle.controlHeight
                            text: qsTr("Phong")
                            onClicked: PrintBedPropertyBridge.renderingMode = 0
                        }
                        SegmentedButton {
                            width: 112
                            height: PrintWorkspaceStyle.controlHeight
                            text: qsTr("PBR")
                            onClicked: PrintBedPropertyBridge.renderingMode = 1
                        }
                    }
                }

                InspectorRow {
                    label: qsTr("Build Plate Color")
                    ColorPickerButton {
                        selectedColor: Qt.rgba(
                            PrintBedPropertyBridge.platformColor.x,
                            PrintBedPropertyBridge.platformColor.y,
                            PrintBedPropertyBridge.platformColor.z,
                            1)
                        onPicked: function(newColor) {
                            PrintBedPropertyBridge.platformColor = Qt.vector3d(
                                newColor.r, newColor.g, newColor.b)
                        }
                    }
                }

                InspectorRow {
                    label: qsTr("Opacity")
                    StandardSlider {
                        value: PrintBedPropertyBridge.opacity
                        valueFormat: "percent"
                        onMoved: function(newValue) {
                            PrintBedPropertyBridge.opacity = newValue
                        }
                    }
                }

                InspectorRow {
                    label: qsTr("Metalness")
                    visible: PrintBedPropertyBridge.renderingMode === 1
                    StandardSlider {
                        value: PrintBedPropertyBridge.metallic
                        onMoved: function(newValue) {
                            PrintBedPropertyBridge.metallic = newValue
                        }
                    }
                }

                InspectorRow {
                    label: qsTr("Roughness")
                    visible: PrintBedPropertyBridge.renderingMode === 1
                    StandardSlider {
                        value: PrintBedPropertyBridge.roughness
                        onMoved: function(newValue) {
                            PrintBedPropertyBridge.roughness = newValue
                        }
                    }
                }

                InspectorRow {
                    label: qsTr("Ambient Light")
                    visible: PrintBedPropertyBridge.renderingMode === 0
                    StandardSlider {
                        value: PrintBedPropertyBridge.ambient
                        onMoved: function(newValue) {
                            PrintBedPropertyBridge.ambient = newValue
                        }
                    }
                }

                InspectorRow {
                    label: qsTr("Diffuse")
                    visible: PrintBedPropertyBridge.renderingMode === 0
                    StandardSlider {
                        value: PrintBedPropertyBridge.diffuse
                        onMoved: function(newValue) {
                            PrintBedPropertyBridge.diffuse = newValue
                        }
                    }
                }

                InspectorRow {
                    label: qsTr("Specular Reflection")
                    visible: PrintBedPropertyBridge.renderingMode === 0
                    StandardSlider {
                        value: PrintBedPropertyBridge.specular
                        onMoved: function(newValue) {
                            PrintBedPropertyBridge.specular = newValue
                        }
                    }
                }

                InspectorRow {
                    label: qsTr("Specular Intensity")
                    visible: PrintBedPropertyBridge.renderingMode === 0
                    LabeledSlider {
                        from: 1
                        to: 100
                        value: PrintBedPropertyBridge.specularPower
                        valueFormat: "int"
                        sliderWidth: 164
                        labelWidth: 52
                        fieldSpacing: 8
                        onMoved: function(newValue) {
                            PrintBedPropertyBridge.specularPower = newValue
                        }
                    }
                }

                InspectorRow {
                    label: qsTr("Show Edges")
                    Switch {
                        checked: PrintBedPropertyBridge.edgeVisibility
                        onCheckedChanged: PrintBedPropertyBridge.edgeVisibility = checked
                    }
                }

                InspectorRow {
                    label: qsTr("Edge Width")
                    enabled: PrintBedPropertyBridge.edgeVisibility
                    LabeledSlider {
                        from: 0.5
                        to: 5
                        value: PrintBedPropertyBridge.lineWidth
                        valueFormat: "decimal1"
                        sliderWidth: 164
                        labelWidth: 52
                        fieldSpacing: 8
                        onMoved: function(newValue) {
                            PrintBedPropertyBridge.lineWidth = newValue
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }

    component StandardSlider: LabeledSlider {
        from: 0
        to: 1
        valueFormat: "decimal2"
        sliderWidth: 164
        labelWidth: 52
        fieldSpacing: 8
    }

    component ColorPickerButton: Button {
        id: colorButton

        property color selectedColor: "white"
        signal picked(color newColor)

        width: PrintWorkspaceStyle.controlHeight
        height: PrintWorkspaceStyle.controlHeight
        padding: 0
        flat: true

        background: Rectangle {
            radius: PrintWorkspaceStyle.controlRadius
            color: colorButton.selectedColor
            border.width: 1
            border.color: PrintWorkspaceStyle.border(root.SmoothUI.dark)
        }
        contentItem: Item {}

        onClicked: colorDialog.open()

        ColorDialog {
            id: colorDialog
            selectedColor: colorButton.selectedColor
            onAccepted: {
                colorButton.selectedColor = colorDialog.selectedColor
                colorButton.picked(colorDialog.selectedColor)
            }
        }
    }
}
