import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0
import "../../../components"

ToolbarSurface {
    id: rightPrintToolbar
    width: PrintWorkspaceStyle.toolbarWidth
    height: toolbarColumn.implicitHeight + 16

    // 信号：打开设置面板
    signal openSettingsPanel(string panelType, var triggerButton)

    // 当前选中的面板类型
    property string currentPanelType: ""

    ScrollView {
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.topMargin: 8
        anchors.bottomMargin: 8

        Column {
            id: toolbarColumn
            width: rightPrintToolbar.width - 12
            spacing: 4

            // ==================== Settings 分组 ====================
            HoverExpandableButton {
                icon.name: FluentIcons.graph_Settings
                tooltip: qsTr("Settings")
                expandItems: [
                    {
                        icon: FluentIcons.graph_View,
                        text: qsTr("Scene Settings"),
                        tooltip: qsTr("View elements, background, and build plate appearance"),
                        enabled: true,
                        action: function() {
                            rightPrintToolbar.openSettingsPanel("scene", rightPrintToolbar)
                        }
                    }
                ]
                expandDirection: 2  // 向左展开
                hoverDelay: 300
                hideDelay: 100
            }

            // 分隔线
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            // ==================== 帮助 ====================
            HoverExpandableButton {
                icon.name: FluentIcons.graph_Help
                tooltip: qsTr("Help")
                expandItems: [
                    {
                        icon: FluentIcons.graph_Help,
                        text: qsTr("User Guide"),
                        tooltip: qsTr("View User Guide"),
                        enabled: true,
                        action: function() {
                            console.log("Help clicked")
                        }
                    },
                    {
                        icon: FluentIcons.graph_Info,
                        text: qsTr("About"),
                        tooltip: qsTr("About GPlatform"),
                        enabled: true,
                        action: function() {
                            console.log("About clicked")
                        }
                    }
                ]
                expandDirection: 2  // 向左展开
                hoverDelay: 300
                hideDelay: 100
            }
        }
    }
}
