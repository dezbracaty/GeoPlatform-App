import QtQuick
import QtQuick.Window

Window {
    id: root
    objectName: "gplatform.startup"
    width: 480
    height: 480
    visible: true
    flags: Qt.Tool | Qt.FramelessWindowHint
    title: "GPlatform"
    color: "#f4f6f8"
    x: Screen.virtualX + (Screen.width - width) / 2
    y: Screen.virtualY + (Screen.height - height) / 2
    onClosing: function(event) { event.accepted = false; StartupState.requestExit() }

    Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#dce3ec" }
    Image {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 88; width: 88; height: 88
        source: "qrc:/qt/qml/GPlatform/res/images/logo.png"
        fillMode: Image.PreserveAspectFit
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 193; text: "GPlatform"; color: "#173456"
        font.pixelSize: 34; font.weight: Font.DemiBold
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 244; text: StartupState.version; color: "#728196"; font.pixelSize: 14
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 290; width: parent.width - 48
        text: StartupState.stageText; horizontalAlignment: Text.AlignHCenter
        color: "#40516a"; font.pixelSize: 15
    }
    Image {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 330; width: 32; height: 32
        source: "qrc:/gplatform/startup/spinner.svg"
        visible: StartupState.errorMessage.length === 0
        RotationAnimator on rotation { from: 0; to: 360; duration: 1000; loops: Animation.Infinite }
    }
    Text {
        x: 24; y: 326; width: parent.width - 48; height: 70
        visible: StartupState.errorMessage.length > 0
        text: StartupState.errorMessage; wrapMode: Text.Wrap
        color: "#a42834"; font.pixelSize: 13
    }
    Text {
        x: 24; y: 380; width: parent.width - 48
        visible: StartupState.elapsedMs > 10000 && StartupState.errorMessage.length === 0
        text: qsTr("Startup is taking longer. See logs for details.")
        color: "#728196"; font.pixelSize: 12; wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 423; spacing: 18
        Repeater {
            model: StartupState.errorMessage.length > 0
                   ? [qsTr("Logs"), qsTr("Restart"), qsTr("Exit")]
                   : [qsTr("Logs"), qsTr("Exit")]
            delegate: Rectangle {
                required property string modelData
                required property int index
                width: 100; height: 32; radius: 5
                color: mouse.containsMouse ? "#dce8f8" : "#e8edf4"
                Text { anchors.centerIn: parent; text: parent.modelData; color: "#25456c"; font.pixelSize: 13 }
                MouseArea {
                    id: mouse; anchors.fill: parent; hoverEnabled: true
                    onClicked: {
                        if (index === 0) StartupState.showLogs()
                        else if (StartupState.errorMessage.length > 0 && index === 1) StartupState.requestRestart()
                        else StartupState.requestExit()
                    }
                }
            }
        }
    }
}
