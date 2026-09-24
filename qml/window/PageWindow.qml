import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0

FramelessWindow {
    id: window
    property var argument
    property alias infoBarManager: infobar_manager

    width: 1080
    height: 760
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    windowEffect: Global.windowEffect
    title: argument && argument.title ? argument.title : qsTr("Page")

    appBar: AppBar {
        implicitHeight: Qt.platform.os === "osx" ? 60 : 48
        windowIcon: Item {}
    }

    onInit: (arg) => {
        argument = arg
    }

    onNewInit: (arg) => {
        argument = arg
    }

    initialItem: {
        if (argument && argument.url) {
            return argument.url
        }
        return undefined
    }

    InfoBarManager {
        id: infobar_manager
        target: window.contentItem
        messageMaximumWidth: 380
    }
}

