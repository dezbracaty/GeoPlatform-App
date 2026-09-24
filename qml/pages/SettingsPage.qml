import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

ScrollablePage {

    title: qsTr("Settings")
    columnSpacing: 24

    Dialog {
        id: dialog_restart
        x: Math.ceil((parent.width - width) / 2)
        y: Math.ceil((parent.height - height) / 2)
        parent: Overlay.overlay
        modal: true
        width: 300
        title: qsTr("Friendly Reminder")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            ApplicationHelper.restart()
        }
        Column {
            spacing: 20
            anchors.fill: parent
            Label {
                width: parent.width
                wrapMode: Label.WrapAnywhere
                text: qsTr("This action requires a restart of the program to take effect, is it restarted?")
            }
        }
    }

    GroupBox{
        title: qsTr("Theme mode")
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            RadioButton {
                checked: Theme.darkMode === SmoothUI.System
                text: qsTr("system")
                onClicked: {
                    Theme.darkMode = SmoothUI.System
                }
            }
            RadioButton {
                checked: Theme.darkMode === SmoothUI.Light
                text: qsTr("light")
                onClicked: {
                    Theme.darkMode = SmoothUI.Light
                }
            }
            RadioButton {
                checked: Theme.darkMode === SmoothUI.Dark
                text: qsTr("dark")
                onClicked: {
                    Theme.darkMode = SmoothUI.Dark
                }
            }
        }
    }

    GroupBox{
        title: qsTr("Navigation Pane Display Mode")
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            RadioButton {
                checked: Global.displayMode === NavigationViewType.Top
                text: qsTr("top")
                onClicked: {
                    Global.displayMode = NavigationViewType.Top
                }
            }
            RadioButton {
                checked: Global.displayMode === NavigationViewType.Open
                text: qsTr("open")
                onClicked: {
                    Global.displayMode = NavigationViewType.Open
                }
            }
            RadioButton {
                checked: Global.displayMode === NavigationViewType.Compact
                text: qsTr("compact")
                onClicked: {
                    Global.displayMode = NavigationViewType.Compact
                }
            }
            RadioButton {
                checked: Global.displayMode === NavigationViewType.Minimal
                text: qsTr("minimal")
                onClicked: {
                    Global.displayMode = NavigationViewType.Minimal
                }
            }
            RadioButton {
                checked: Global.displayMode === NavigationViewType.Auto
                text: qsTr("auto")
                onClicked: {
                    Global.displayMode = NavigationViewType.Auto
                }
            }
        }
    }

    ListModel{
        id: accentColors
        ListElement{
            name: "Yellow"
            color: function(){return Colors.yellow}
        }
        ListElement{
            name: "Orange"
            color: function(){return Colors.orange}
        }
        ListElement{
            name: "Red"
            color: function(){return Colors.red}
        }
        ListElement{
            name: "Magenta"
            color: function(){return Colors.magenta}
        }
        ListElement{
            name: "Purple"
            color: function(){return Colors.purple}
        }
        ListElement{
            name: "Blue"
            color: function(){return Colors.blue}
        }
        ListElement{
            name: "Teal"
            color: function(){return Colors.teal}
        }
        ListElement{
            name: "Green"
            color: function(){return Colors.green}
        }
    }

    GroupBox{
        title: qsTr("Primary Color")
        Layout.fillWidth: true
        Flow {
            spacing: 10
            anchors.fill: parent
            Repeater{
                model: accentColors
                delegate: Rectangle{
                    required property var model
                    implicitWidth: 48
                    implicitHeight: 48
                    radius: 4
                    border.color: model.color().darkest()
                    border.width: 1
                    color: mouse_item_accent_color.containsMouse? model.color().lightest() : model.color().normal
                    Icon{
                        source: FluentIcons.graph_CheckMark
                        width: 24
                        height: 24
                        anchors.centerIn: parent
                        color: Colors.basedOnLuminance(parent.color)
                        visible: model.color() === Theme.primaryColor
                    }
                    MouseArea{
                        id: mouse_item_accent_color
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            Theme.primaryColor = model.color()
                        }
                    }
                }
            }
        }
    }

    GroupBox{
        title: qsTr("Locale")
        Layout.fillWidth: true
        Flow {
            spacing: 10
            anchors.fill: parent
            RadioButton{
                text: "English"
                checked: AppInfo.locale === "en_US"
                onClicked: {
                    AppInfo.locale = "en_US"
                    SettingsHelper.saveLocale("en_US")
                    dialog_restart.open()
                }
            }
            RadioButton{
                text: "中文"
                checked: AppInfo.locale === "zh_CN"
                onClicked: {
                    AppInfo.locale = "zh_CN"
                    SettingsHelper.saveLocale("zh_CN")
                    dialog_restart.open()
                }
            }
        }
    }
    
    GroupBox{
        title: qsTr("Startup")
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            Switch {
                id: rememberLastPageSwitch
                text: qsTr("Remember last opened page")
                checked: SettingsHelper.getRememberLastPage()
                onCheckedChanged: {
                    SettingsHelper.saveRememberLastPage(checked)
                }
            }
            Label {
                text: qsTr("When enabled, the application will open to the last viewed page on startup")
                font.pixelSize: 12
                opacity: 0.6
                wrapMode: Label.WrapAnywhere
                Layout.fillWidth: true
                visible: rememberLastPageSwitch.checked
            }
        }
    }
}