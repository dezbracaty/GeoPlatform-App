import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SmoothUI.Controls
import GPlatform

ContentDialog {
    id: dialog
    objectName: "import.units.dialog"
    required property Item hostItem
    required property string fileName
    required property string currentSize
    required property string convertedSize
    required property string suspectedUnit
    required property real conversionFactor
    property bool decided: false
    signal decision(int choice)

    parent: hostItem ? hostItem.Overlay.overlay : null
    width: parent ? Math.min(640, parent.width - 32) : 640
    title: qsTr("Model dimensions may use different units")
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    function choose(choice) {
        if (decided) return
        decided = true
        decision(choice)
        close()
    }
    onClosed: choose(0)
    onOpened: keepButton.forceActiveFocus()

    ColumnLayout {
        width: dialog.availableWidth
        spacing: PrintWorkspaceStyle.space12
        Label {
            Layout.fillWidth: true
            text: dialog.fileName
            textFormat: Text.PlainText
            font: Typography.bodyStrong
            elide: Text.ElideMiddle
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("This STL has no unit information. Its volume suggests the coordinates may be in %1. Choose how to import it.").arg(dialog.suspectedUnit)
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Current size: %1").arg(dialog.currentSize)
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Size after conversion: %1").arg(dialog.convertedSize)
            wrapMode: Text.Wrap
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "import.units.cancel"
            text: qsTr("Cancel import")
            onClicked: dialog.choose(0)
        }
        Button {
            id: keepButton
            objectName: "import.units.keep"
            text: qsTr("Keep original size")
            onClicked: dialog.choose(1)
        }
        Button {
            objectName: "import.units.convert"
            text: qsTr("Convert to millimeters (×%1)").arg(dialog.conversionFactor)
            onClicked: dialog.choose(2)
        }
    }
}
