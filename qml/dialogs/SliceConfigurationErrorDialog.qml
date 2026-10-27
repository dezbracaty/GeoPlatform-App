import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SmoothUI.Controls
import GPlatform

ContentDialog {
    id: dialog

    property string presetName: ""
    property string errorMessage: ""
    property string optionKey: ""

    title: qsTr("Slicing failed")
    width: 560

    ColumnLayout {
        width: 512
        spacing: PrintWorkspaceStyle.space12

        Label {
            Layout.fillWidth: true
            text: qsTr("OrcaSlicer returned the following errors:")
            font: Typography.body
            color: Theme.res.textFillColorPrimary
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: errorContent.implicitHeight + 24
            radius: PrintWorkspaceStyle.controlRadius
            color: Theme.res.systemFillColorCriticalBackground
            border.width: 1
            border.color: Qt.alpha(Theme.res.systemFillColorCritical, 0.28)

            RowLayout {
                id: errorContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: PrintWorkspaceStyle.space16
                anchors.rightMargin: PrintWorkspaceStyle.space16
                spacing: PrintWorkspaceStyle.space12

                Icon {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    Layout.alignment: Qt.AlignTop
                    source: FluentIcons.graph_Error
                    color: Theme.res.systemFillColorCritical
                }

                TextEdit {
                    Layout.fillWidth: true
                    text: dialog.errorMessage
                    readOnly: true
                    wrapMode: TextEdit.WordWrap
                    selectByMouse: true
                    Layout.preferredHeight: contentHeight
                    font: Typography.body
                    color: Theme.res.textFillColorPrimary
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: dialog.presetName.length > 0
            text: qsTr("Current configuration: %1").arg(dialog.presetName)
            elide: Text.ElideMiddle
            font: Typography.caption
            color: Theme.res.textFillColorSecondary
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Close")
            onClicked: dialog.close()
        }

        Button {
            visible: dialog.optionKey.length > 0
            text: qsTr("Go to settings")
            highlighted: true
            onClicked: {
                const targetKey = dialog.optionKey
                dialog.close()
                Qt.callLater(function() {
                    if (!Global.mainScreen
                            || !Global.mainScreen.openSliceSettings({
                                "settingKey": targetKey
                            })) {
                        console.error("Cannot open slice settings for", targetKey)
                    }
                })
            }
        }
    }
}
