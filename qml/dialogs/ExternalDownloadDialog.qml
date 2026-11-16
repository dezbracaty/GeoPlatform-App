import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SmoothUI.Controls
import GPlatform

ContentDialog {
    id: dialog
    objectName: "external.download.dialog"
    required property Item hostItem
    required property var downloadItem
    required property string fileName
    required property string siteName
    required property string requestId
    property bool resolved: false
    signal cancelRequested()
    parent: hostItem ? hostItem.Overlay.overlay : null
    title: qsTr("Downloading model")
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    width: parent ? Math.min(480, parent.width - 32) : 480
    function cancelAndClose() {
        if (!resolved) { resolved = true; cancelRequested() }
        close()
    }
    onAboutToHide: { if (!resolved) { resolved = true; cancelRequested() } }
    onOpened: cancelButton.forceActiveFocus()
    ColumnLayout {
        width: dialog.availableWidth
        spacing: PrintWorkspaceStyle.space12
        Label { Layout.fillWidth: true; text: dialog.fileName; textFormat: Text.PlainText; elide: Text.ElideMiddle }
        Label { Layout.fillWidth: true; text: dialog.siteName; textFormat: Text.PlainText; elide: Text.ElideMiddle }
        ProgressBar {
            Layout.fillWidth: true
            indeterminate: !dialog.downloadItem || dialog.downloadItem.totalSize <= 0
            value: dialog.downloadItem ? dialog.downloadItem.progress : 0
        }
    }
    footer: DialogButtonBox {
        Button {
            id: cancelButton
            objectName: "external.download.cancel"
            text: qsTr("Cancel")
            onClicked: dialog.cancelAndClose()
        }
    }
}
