import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SmoothUI.Controls
import GPlatform

ContentDialog {
    id: dialog
    objectName: "import.3mf.dialog"
    required property Item hostItem
    required property string stage
    required property string fileName
    property string warningText: ""
    property string questionText: ""
    property bool decided: false
    signal decision(var values)
    parent: hostItem ? hostItem.Overlay.overlay : null
    width: parent ? Math.min(700, parent.width - 32) : 700
    title: stage === "mode" ? qsTr("Open 3MF")
           : stage === "native" ? qsTr("Import options") : qsTr("Replace the current project?")
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    function choose(action) {
        if (decided) return
        decided = true
        decision({action: action})
        close()
    }
    onClosed: choose("cancel")
    onOpened: cancelButton.forceActiveFocus()
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
            wrapMode: Text.Wrap
            text: dialog.stage === "mode"
                ? qsTr("Open the complete project to restore its printer, settings and print plates, or import models using the current printer and process. Material slots are added when needed.")
                : dialog.stage === "native"
                ? dialog.questionText
                : qsTr("The current models and print plates will be replaced with this project. The previous project's undo history will be cleared after the new project opens successfully.")
        }
        ScrollView {
            objectName: "import.3mf.diagnostics"
            Layout.fillWidth: true
            visible: dialog.warningText.length > 0
            Layout.preferredHeight: Math.min(220, warningLabel.implicitHeight)
            clip: true
            Label {
                id: warningLabel
                width: dialog.availableWidth - 24
                text: dialog.warningText
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
        }
    }
    footer: DialogButtonBox {
        Button { id: cancelButton; objectName: "import.3mf.cancel"; text: qsTr("Cancel"); onClicked: dialog.choose("cancel") }
        Button { objectName: "import.3mf.geometry"; visible: dialog.stage === "mode"; text: qsTr("Import models"); onClicked: dialog.choose("geometry") }
        Button { objectName: "import.3mf.project"; visible: dialog.stage === "mode"; text: qsTr("Open project"); onClicked: dialog.choose("project") }
        Button { objectName: "import.3mf.no"; visible: dialog.stage === "native"; text: qsTr("No"); onClicked: dialog.choose("no") }
        Button { objectName: "import.3mf.yes"; visible: dialog.stage === "native"; text: qsTr("Yes"); onClicked: dialog.choose("yes") }
        Button {
            objectName: "import.3mf.confirm"
            visible: dialog.stage === "replace"
            text: qsTr("Replace and open")
            onClicked: dialog.choose("confirm")
        }
    }
}
