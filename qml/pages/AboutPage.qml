import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

ScrollablePage {
    id: root
    title: qsTr("About")
    readonly property var journalService: AppInfo.journalService
    readonly property int journalPort: journalService ? journalService.serverPort : 0
    
    ColumnLayout {
        Layout.fillWidth: true
        Layout.maximumWidth: 640
        Layout.alignment: Qt.AlignLeft
        spacing: 20
        
        Label {
            text: "GPlatform"
            font: Typography.title
        }
        
        Label {
            text: "Version 1.0.0"
            font: Typography.subtitle
        }
        
        Label {
            text: "A versatile platform currently focused on 3D printing management"
            font: Typography.body
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }

        GroupBox {
            title: qsTr("Runtime information")
            Layout.fillWidth: true

            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: 20
                rowSpacing: 12

                Label { text: qsTr("Journal service") }
                Label {
                    objectName: "about.runtime.journalStatus"
                    text: root.journalPort > 0 ? qsTr("Listening")
                         : root.journalService ? qsTr("Not listening") : qsTr("Not enabled")
                }

                Label { text: qsTr("Listen address") }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 8

                    TextField {
                        id: journalAddress
                        objectName: "about.runtime.journalAddress"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: root.journalPort > 0 ? "127.0.0.1:" + root.journalPort : "—"
                        readOnly: true
                        selectByMouse: true
                        font.family: "monospace"
                    }
                    Button {
                        objectName: "about.runtime.copyJournalAddress"
                        text: qsTr("Copy address")
                        enabled: root.journalPort > 0
                        onClicked: {
                            journalAddress.selectAll()
                            journalAddress.copy()
                            journalAddress.deselect()
                        }
                    }
                }

                Label { text: qsTr("Process ID") }
                Label {
                    objectName: "about.runtime.processId"
                    text: String(AppInfo.processId)
                    font.family: "monospace"
                }

                Label {
                    objectName: "about.runtime.journalError"
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.journalService ? root.journalService.listenError : ""
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
