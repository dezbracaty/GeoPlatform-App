import QtQuick
import QtQuick.Controls as QQC

FocusScope {
    id: content
    objectName: "gplatform.startup.content"
    focus: enabled
    required property bool animating
    readonly property bool dark: StartupState.loadingDark
    readonly property bool failed: StartupState.errorMessage.length > 0
    readonly property color backgroundColor: dark ? "#181b20" : "#f4f6f8"
    readonly property color foregroundColor: dark ? "#f3f5f8" : "#20242a"
    readonly property color mutedColor: dark ? "#b5bfce" : "#606975"
    readonly property color borderColor: dark ? "#414955" : "#d9dee7"
    readonly property color accentColor: dark ? "#6bb7f0" : "#0078d4"
    readonly property color errorColor: dark ? "#ff9b9b" : "#c43131"
    readonly property bool narrow: width < 384
    readonly property int inset: narrow ? 20 : 28
    readonly property bool shortWindow: height - actions.height < 185
    readonly property int verticalInset: shortWindow ? 8 : (narrow ? 20 : 24)
    readonly property int textColumn: 56
    readonly property int statusTop: separator.y + (shortWindow ? 7 : 14)
    implicitWidth: 420
    implicitHeight: (narrow ? 260 : 252) + (failed ? 52 : 0)

    onFailedChanged: {
        viewport.contentY = 0
        if (failed && enabled)
            errorText.forceActiveFocus(Qt.OtherFocusReason)
    }
    // Child controls handle navigation and activation before this scope
    // consumes remaining keys above the workspace.
    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) { event.accepted = true }
    Keys.onReleased: function(event) { event.accepted = true }

    component StartupButton: Rectangle {
        id: button
        required property string text
        property bool primary: false
        signal clicked()
        width: Math.min(actions.width, title.implicitWidth + (primary ? 26 : 12))
        height: 36
        radius: 5
        color: primary ? (content.dark ? "#282d36" : "#ffffff") : "transparent"
        border.color: button.activeFocus ? content.accentColor : content.borderColor
        border.width: primary || button.activeFocus ? 1 : 0
        opacity: pointer.containsMouse ? 0.8 : 1
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: button.text
        Accessible.onPressAction: button.clicked()
        Keys.onSpacePressed: button.clicked()
        Keys.onReturnPressed: button.clicked()
        Keys.onEnterPressed: button.clicked()
        Text {
            id: title
            anchors.centerIn: parent
            text: button.text
            color: button.primary ? content.foregroundColor : content.mutedColor
            font.pixelSize: 14
            font.weight: Font.Medium
        }
        MouseArea {
            id: pointer
            anchors.fill: parent
            hoverEnabled: true
            onClicked: button.clicked()
        }
    }

    Row {
        id: brand
        objectName: "gplatform.startup.brand"
        x: content.inset
        y: content.verticalInset
        height: 44
        spacing: 12
        Image {
            width: 44; height: 44
            source: "../../res/images/logo.png"
            fillMode: Image.PreserveAspectFit
        }
        Column {
            Text {
                text: "GPlatform"
                font.pixelSize: 28
                font.weight: Font.DemiBold
                color: content.foregroundColor
                height: 30
            }
            Text {
                text: StartupState.version
                font.pixelSize: 12
                color: content.mutedColor
                height: 14
            }
        }
    }

    Rectangle {
        id: separator
        objectName: "gplatform.startup.separator"
        x: content.inset
        y: brand.y + brand.height + (content.shortWindow ? 6 : (content.narrow ? 12 : 16))
        width: parent.width - 2 * content.inset
        height: 1
        color: content.borderColor
    }

    Flickable {
        id: viewport
        objectName: "gplatform.startup.statusViewport"
        x: content.inset
        y: content.statusTop + (content.failed ? status.height + 6 : 0)
        width: parent.width - 2 * content.inset
        height: Math.max(0, actions.y - y - 12)
        contentWidth: width
        contentHeight: message.height
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        readonly property int scrollGutter: contentHeight > height ? 8 : 0
        QQC.ScrollBar.vertical: QQC.ScrollBar {
            width: 8
            policy: viewport.scrollGutter ? QQC.ScrollBar.AlwaysOn : QQC.ScrollBar.AlwaysOff
        }

        Column {
            id: message
            width: viewport.width
            spacing: 6

            Item {
                id: status
                objectName: "gplatform.startup.status"
                // Keep the failure heading above the scrolling details.
                parent: content.failed ? content : message
                x: content.failed ? content.inset : 0
                y: content.failed ? content.statusTop : 0
                width: viewport.width
                height: Math.max(32, stage.implicitHeight)

                LoadingGlyph {
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !content.failed
                    animating: content.animating
                    accentColor: content.accentColor
                    mutedColor: content.mutedColor
                }
                Text {
                    width: 44
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignHCenter
                    visible: content.failed
                    text: "!"
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    color: content.errorColor
                }
                Text {
                    id: stage
                    objectName: "gplatform.startup.stageText"
                    x: content.textColumn
                    width: parent.width - x
                    anchors.verticalCenter: parent.verticalCenter
                    text: StartupState.stageText
                    wrapMode: Text.Wrap
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 20
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: content.foregroundColor
                }
            }

            Text {
                objectName: "gplatform.startup.slowHint"
                x: content.textColumn
                width: parent.width - x
                visible: StartupState.elapsedMs > 10000 && !content.failed
                text: qsTr("Startup is taking longer. See logs for details.")
                color: content.mutedColor
                font.pixelSize: 13
                wrapMode: Text.Wrap
                lineHeightMode: Text.FixedHeight
                lineHeight: 18
            }

            TextEdit {
                id: errorText
                objectName: "gplatform.startup.errorText"
                x: content.textColumn
                width: parent.width - x - 8
                visible: content.failed
                text: StartupState.errorMessage
                textFormat: TextEdit.PlainText
                color: content.foregroundColor
                font.pixelSize: 13
                readOnly: true
                selectByMouse: true
                selectByKeyboard: true
                wrapMode: TextEdit.Wrap
                activeFocusOnTab: true
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.tab: logs
                KeyNavigation.backtab: exit
                Keys.onPressed: function(event) {
                    if (event.modifiers !== Qt.NoModifier) {
                        event.accepted = false
                        return
                    }
                    const end = Math.max(0, viewport.contentHeight - viewport.height)
                    let position = viewport.contentY
                    switch (event.key) {
                    case Qt.Key_Up: position -= 18; break
                    case Qt.Key_Down: position += 18; break
                    case Qt.Key_PageUp: position -= viewport.height; break
                    case Qt.Key_PageDown: position += viewport.height; break
                    case Qt.Key_Home: position = 0; break
                    case Qt.Key_End: position = end; break
                    default: event.accepted = false; return
                    }
                    viewport.contentY = Math.max(0, Math.min(end, position))
                    event.accepted = true
                }
            }
        }
    }

    Flow {
        id: actions
        objectName: "gplatform.startup.actions"
        x: content.inset
        y: content.height - content.verticalInset - height
        width: parent.width - 2 * content.inset
        height: implicitHeight
        spacing: 8
        StartupButton {
            id: logs
            objectName: "gplatform.startup.logs"
            focus: true
            text: qsTr("Logs")
            KeyNavigation.priority: KeyNavigation.BeforeItem
            KeyNavigation.tab: content.failed ? restart : exit
            KeyNavigation.backtab: content.failed ? errorText : exit
            onClicked: StartupState.showLogs()
        }
        Item {
            readonly property real spare: actions.width - logs.width - exit.width - 2 * actions.spacing
                                         - (restart.visible ? restart.width + actions.spacing : 0)
            width: Math.max(0, spare)
            height: 1
            visible: spare >= 0
        }
        StartupButton {
            id: restart
            objectName: "gplatform.startup.restart"
            visible: content.failed
            text: qsTr("Restart")
            KeyNavigation.priority: KeyNavigation.BeforeItem
            KeyNavigation.tab: exit
            KeyNavigation.backtab: logs
            onClicked: StartupState.requestRestart()
        }
        StartupButton {
            id: exit
            objectName: "gplatform.startup.exit"
            text: qsTr("Exit")
            primary: true
            KeyNavigation.priority: KeyNavigation.BeforeItem
            KeyNavigation.tab: content.failed ? errorText : logs
            KeyNavigation.backtab: content.failed ? restart : logs
            onClicked: StartupState.requestExit()
        }
    }
}
