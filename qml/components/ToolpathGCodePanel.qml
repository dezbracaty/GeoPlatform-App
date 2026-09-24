import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import SmoothUI
import SmoothUI.Controls
import GPlatform

Item {
    id: root
    objectName: "slicing.preview.gcodeContent"
    property var codeModel: SlicingPreviewBridge.gcodeLines
    implicitWidth: 400
    implicitHeight: 320

    function revealCurrentLine() {
        if (visible && codeModel.highlightedLine > 0)
            codeList.positionViewAtIndex(codeModel.highlightedLine - 1, ListView.Center)
    }
    onVisibleChanged: if (visible) Qt.callLater(revealCurrentLine)

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            Label { text: "G-code"; font: Typography.bodyStrong; Layout.fillWidth: true }
            Button {
                text: root.codeModel.pinned ? qsTr("Unpin code") : qsTr("Pin code")
                enabled: root.codeModel.highlightedLine > 0
                onClicked: root.codeModel.pinned = !root.codeModel.pinned
            }
            Button {
                text: qsTr("Copy line")
                enabled: root.codeModel.highlightedLine > 0
                onClicked: root.codeModel.copyHighlightedLine()
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.codeModel.description || qsTr("Hover a visible path or point to inspect its G-code.")
            wrapMode: Text.WordWrap
            font: Typography.caption
        }
        ListView {
            id: codeList
            objectName: "gcodeList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.codeModel
            reuseItems: true
            QQC.ScrollBar.vertical: QQC.ScrollBar {}
            delegate: Rectangle {
                required property int lineNumber
                required property string codeText
                required property bool highlighted
                width: codeList.width
                height: Math.max(24, sourceText.implicitHeight + 6)
                color: highlighted ? "#4052a8ed" : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 8
                    Text {
                        Layout.preferredWidth: 54
                        Layout.alignment: Qt.AlignTop
                        text: lineNumber
                        horizontalAlignment: Text.AlignRight
                        color: Theme.res.textFillColorSecondary
                        font.family: "monospace"
                        font.pixelSize: 12
                    }
                    TextEdit {
                        id: sourceText
                        Layout.fillWidth: true
                        text: codeText
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        color: Theme.res.textFillColorPrimary
                        font.family: "monospace"
                        font.pixelSize: 12
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.codeModel.status
            font: Typography.caption
            color: Theme.res.textFillColorSecondary
            wrapMode: Text.WordWrap
        }
    }
    Connections {
        target: root.codeModel
        function onInspectionChanged() { root.revealCurrentLine() }
        function onNavigationRequested() { root.revealCurrentLine() }
    }
}
