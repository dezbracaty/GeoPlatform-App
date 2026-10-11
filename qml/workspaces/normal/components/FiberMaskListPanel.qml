import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0

ScrollView {
    id: root
    objectName: "fiberMaskListPanel"
    clip: true
    contentWidth: availableWidth
    rightPadding: 12
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
        width: root.availableWidth
        spacing: 8
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Later masks override earlier masks")
        }
        Repeater {
            model: FiberMaskBridge.modelGroups
            delegate: ColumnLayout {
                id: group
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("%1 · %2 masks").arg(group.modelData.name).arg(group.modelData.count)
                    font: Typography.bodyStrong
                    elide: Text.ElideMiddle
                }
                Label {
                    visible: group.modelData.count === 0
                    text: qsTr("No masks")
                }
                Repeater {
                    model: group.modelData.masks
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Button {
                            id: maskButton
                            objectName: "fiberMaskList.model." + group.index + ".mask." + index
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            contentItem: Label {
                                text: maskButton.text
                                font: maskButton.font
                                color: maskButton.SmoothUI.textColor
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            text: (index + 1) + ". " + modelData.name
                            checkable: true
                            checked: FiberMaskBridge.active && Number(FiberMaskBridge.state.selectedMaskId) === Number(modelData.id)
                            highlighted: checked
                            ToolTip.visible: hovered && contentItem.truncated
                            ToolTip.text: text
                            ToolTip.delay: 500
                            onClicked: FiberMaskBridge.selectMask(modelData.id)
                        }
                        CheckBox {
                            objectName: "fiberMaskList.model." + group.index + ".mask." + index + ".visible"
                            text: qsTr("Visible")
                            checked: modelData.visible
                            onClicked: FiberMaskBridge.setMaskVisible(modelData.id, checked)
                        }
                    }
                }
            }
        }
    }
}
