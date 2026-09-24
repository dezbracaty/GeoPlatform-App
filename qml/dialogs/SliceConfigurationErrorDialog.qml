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

    title: qsTr("切片失败")
    width: 560

    ColumnLayout {
        width: 512
        spacing: PrintWorkspaceStyle.space12

        Label {
            Layout.fillWidth: true
            text: qsTr("OrcaSlicer 返回以下错误：")
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
            text: qsTr("当前配置：%1").arg(dialog.presetName)
            elide: Text.ElideMiddle
            font: Typography.caption
            color: Theme.res.textFillColorSecondary
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("关闭")
            onClicked: dialog.close()
        }

        Button {
            visible: dialog.optionKey.length > 0
            text: qsTr("跳转到设置")
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
