import QtQuick
import QtQuick.Window
import GPlatform 1.0

Window {
    id: root
    objectName: "gplatform.startup"
    readonly property rect workArea: StartupState.splashWorkArea
    width: workArea.width > 0 ? Math.min(startupContent.implicitWidth, workArea.width) : startupContent.implicitWidth
    height: workArea.height > 0 ? Math.min(startupContent.implicitHeight, workArea.height) : startupContent.implicitHeight
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
           | (Qt.platform.os === "osx" && StartupState.backgroundReplay ? Qt.WindowDoesNotAcceptFocus : 0)
    title: "GPlatform"
    color: startupContent.backgroundColor
    x: Math.floor(workArea.x + (workArea.width - width) / 2)
    y: Math.floor(workArea.y + (workArea.height - height) / 2)
    onClosing: function(event) { event.accepted = false; StartupState.requestExit() }

    Rectangle { anchors.fill: parent; color: "transparent"; border.color: startupContent.borderColor }
    StartupLoadingContent {
        id: startupContent
        anchors.fill: parent
        animating: root.visible && (root.active || StartupState.backgroundReplay)
                   && !StartupState.mainWindowAttached
    }
}
