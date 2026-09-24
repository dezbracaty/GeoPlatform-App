import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0

Item {
    id: root
    objectName: "cutToolPanel"
    anchors.fill: parent

    SmoothUI.theme: Theme.of(root)

    function resetAll() {
        CutToolBridge.resetPlane()
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        leftPadding: PrintWorkspaceStyle.space16
        rightPadding: PrintWorkspaceStyle.space16
        topPadding: PrintWorkspaceStyle.space12
        bottomPadding: PrintWorkspaceStyle.space12
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: PrintWorkspaceStyle.space12

            GroupBox {
                title: qsTr("Cutting plane")
                Layout.fillWidth: true
                enabled: !CutToolBridge.busy

                ColumnLayout {
                    anchors.fill: parent
                    spacing: PrintWorkspaceStyle.space8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: PrintWorkspaceStyle.space12

                        Label {
                            Layout.preferredWidth: 112
                            text: qsTr("Mode")
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Planar")
                            horizontalAlignment: Text.AlignRight
                            color: root.SmoothUI.theme.res.textFillColorPrimary
                            font: Typography.bodyStrong
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: PrintWorkspaceStyle.space12

                        Label {
                            Layout.preferredWidth: 112
                            text: qsTr("Build volume")
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                        }

                        Label {
                            Layout.fillWidth: true
                            text: CutToolBridge.buildVolumeText
                            horizontalAlignment: Text.AlignRight
                            color: root.SmoothUI.theme.res.textFillColorPrimary
                            font: Typography.bodyStrong
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: PrintWorkspaceStyle.space8

                        Label {
                            Layout.preferredWidth: 112
                            text: qsTr("Cut position")
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                        }

                        NumberBox {
                            id: positionBox
                            objectName: "cutPlanePositionBox"
                            Layout.fillWidth: true
                            implicitHeight: PrintWorkspaceStyle.controlHeight
                            value: CutToolBridge.positionZ
                            precision: 2
                            smallChange: 0.1
                            largeChange: 1.0
                            placementMode: NumberBoxType.Inline

                            onValueChanged: {
                                if (activeFocus && value !== null)
                                    CutToolBridge.requestPositionZ(Number(value))
                            }
                            onEditingFinished: {
                                if (value !== null)
                                    CutToolBridge.requestPositionZ(Number(value))
                            }
                        }

                        Label {
                            text: qsTr("mm")
                            color: root.SmoothUI.theme.res.textFillColorTertiary
                            font: Typography.caption
                        }

                        IconButton {
                            objectName: "flipCutPlaneButton"
                            implicitWidth: PrintWorkspaceStyle.controlHeight
                            implicitHeight: PrintWorkspaceStyle.controlHeight
                            icon.name: FluentIcons.graph_Switch
                            icon.width: 16
                            icon.height: 16
                            onClicked: CutToolBridge.flipPlane()

                            ToolTip.delay: Theme.tooltipDelay
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Flip cutting plane")
                        }

                        IconButton {
                            objectName: "resetCutPlaneButton"
                            implicitWidth: PrintWorkspaceStyle.controlHeight
                            implicitHeight: PrintWorkspaceStyle.controlHeight
                            icon.name: FluentIcons.graph_Undo
                            icon.width: 16
                            icon.height: 16
                            enabled: CutToolBridge.planeModified
                            onClicked: CutToolBridge.resetPlane()

                            ToolTip.delay: Theme.tooltipDelay
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Reset cutting plane")
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Drag the plane or yellow handle to move it. Drag the red and green handles to rotate it; hold Shift to snap.")
                        wrapMode: Text.WordWrap
                        color: root.SmoothUI.theme.res.textFillColorTertiary
                        font: Typography.caption
                    }
                }
            }

            StandardButton {
                Layout.fillWidth: true
                text: qsTr("Add connectors")
                icon.name: FluentIcons.graph_Add
                // Connector placement is a separate gizmo/boolean workflow.
                // Do not advertise a dead control in the planar-cut panel.
                visible: false
            }

            GroupBox {
                objectName: "cutAfterCutGroup"
                title: qsTr("After cut")
                Layout.fillWidth: true
                enabled: !CutToolBridge.busy

                ColumnLayout {
                    anchors.fill: parent
                    spacing: PrintWorkspaceStyle.space8

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: PrintWorkspaceStyle.space8
                        rowSpacing: PrintWorkspaceStyle.space4

                        Label { text: "" }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Keep")
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                            font: Typography.caption
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Place on cut")
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                            font: Typography.caption
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Flip")
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                            font: Typography.caption
                        }

                        Label {
                            text: qsTr("●  Upper part")
                            color: "#00FFFF"
                        }
                        CheckBox {
                            objectName: "keepUpperCheckBox"
                            Layout.alignment: Qt.AlignHCenter
                            checked: CutToolBridge.keepUpper
                            enabled: !CutToolBridge.cutToParts
                            onClicked: CutToolBridge.setKeepUpper(checked)
                            Accessible.name: qsTr("Keep upper part")
                        }
                        CheckBox {
                            objectName: "placeUpperOnCutCheckBox"
                            Layout.alignment: Qt.AlignHCenter
                            checked: CutToolBridge.placeUpperOnCut
                            enabled: CutToolBridge.keepUpper
                                     && !CutToolBridge.cutToParts
                            onClicked: CutToolBridge.setPlaceUpperOnCut(checked)
                            Accessible.name: qsTr("Place upper part on cut")
                        }
                        CheckBox {
                            objectName: "flipUpperCheckBox"
                            Layout.alignment: Qt.AlignHCenter
                            checked: CutToolBridge.flipUpper
                            enabled: CutToolBridge.keepUpper
                                     && !CutToolBridge.cutToParts
                            onClicked: CutToolBridge.setFlipUpper(checked)
                            Accessible.name: qsTr("Flip upper part")
                        }

                        Label {
                            text: qsTr("●  Lower part")
                            color: "#FF00FF"
                        }
                        CheckBox {
                            objectName: "keepLowerCheckBox"
                            Layout.alignment: Qt.AlignHCenter
                            checked: CutToolBridge.keepLower
                            enabled: !CutToolBridge.cutToParts
                            onClicked: CutToolBridge.setKeepLower(checked)
                            Accessible.name: qsTr("Keep lower part")
                        }
                        CheckBox {
                            objectName: "placeLowerOnCutCheckBox"
                            Layout.alignment: Qt.AlignHCenter
                            checked: CutToolBridge.placeLowerOnCut
                            enabled: CutToolBridge.keepLower
                                     && !CutToolBridge.cutToParts
                            onClicked: CutToolBridge.setPlaceLowerOnCut(checked)
                            Accessible.name: qsTr("Place lower part on cut")
                        }
                        CheckBox {
                            objectName: "flipLowerCheckBox"
                            Layout.alignment: Qt.AlignHCenter
                            checked: CutToolBridge.flipLower
                            enabled: CutToolBridge.keepLower
                                     && !CutToolBridge.cutToParts
                            onClicked: CutToolBridge.setFlipLower(checked)
                            Accessible.name: qsTr("Flip lower part")
                        }
                    }

                    CheckBox {
                        objectName: "cutToPartsCheckBox"
                        text: qsTr("Cut to parts")
                        checked: false
                        enabled: false
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Grouping cut sides as parts is not available yet.")
                        wrapMode: Text.WordWrap
                        color: root.SmoothUI.theme.res.textFillColorTertiary
                        font: Typography.caption
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: CutToolBridge.errorText.length > 0
                        text: CutToolBridge.errorText
                        wrapMode: Text.WordWrap
                        color: root.SmoothUI.theme.res.systemFillColorCritical
                        font: Typography.caption
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: PrintWorkspaceStyle.space8

                StandardButton {
                    text: qsTr("Reset")
                    icon.name: FluentIcons.graph_Undo
                    enabled: CutToolBridge.planeModified
                             && !CutToolBridge.busy
                    onClicked: root.resetAll()
                }

                Item { Layout.fillWidth: true }

                StandardButton {
                    text: qsTr("Cancel")
                    enabled: !CutToolBridge.busy
                    onClicked: CutToolBridge.cancel()
                }

                ProgressButton {
                    objectName: "performCutButton"
                    text: qsTr("Perform cut")
                    icon.name: FluentIcons.graph_Cut
                    enabled: CutToolBridge.canPerformCut
                    indeterminate: CutToolBridge.busy
                    onClicked: CutToolBridge.performCut()
                }
            }
        }
    }

    Connections {
        target: CutToolBridge

        function onPositionZChanged() {
            if (!positionBox.activeFocus) {
                positionBox.value = CutToolBridge.positionZ
            }
        }
    }
}
