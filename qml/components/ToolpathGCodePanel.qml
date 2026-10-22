import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Item {
    id: root
    objectName: "slicing.preview.gcodeContent"
    SmoothUI.theme: Theme.of(root)
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
        RowLayout {
            visible: root.codeModel.hasFiberRoute
            Button { text: qsTr("Cut command"); onClicked: root.codeModel.navigateFiberEvent(false) }
            Button { text: qsTr("Fiber depleted"); onClicked: root.codeModel.navigateFiberEvent(true) }
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
                color: highlighted ? Colors.withOpacity(Theme.of(root).accentColor.defaultBrushFor(root.SmoothUI.dark), 0.25) : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 8
                    Text {
                        Layout.preferredWidth: 54
                        Layout.alignment: Qt.AlignTop
                        text: lineNumber
                        horizontalAlignment: Text.AlignRight
                        color: root.SmoothUI.theme.res.textFillColorSecondary
                        font.family: "monospace"
                        font.pixelSize: Theme.captionFontSize
                    }
                    TextEdit {
                        id: sourceText
                        Layout.fillWidth: true
                        text: codeText
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        color: root.SmoothUI.theme.res.textFillColorPrimary
                        font.family: "monospace"
                        font.pixelSize: Theme.captionFontSize
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
