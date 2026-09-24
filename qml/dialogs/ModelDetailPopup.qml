import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

AppPopup {
    id: control

    SmoothUI.theme: Theme.of(control)

    property var modelData: null

    readonly property string modelName: control.textValue(
                                                   modelData ? modelData.name : "")
    readonly property string authorName: control.textValue(
                                                   modelData ? modelData.author : "")
    readonly property string authorAvatar: control.textValue(
                                                     modelData ? modelData.authorAvatar : "")
    readonly property string previewSource: control.textValue(
                                                     modelData ? modelData.thumbnail : "")
    readonly property string modelFormat: control.textValue(
                                                    modelData ? modelData.format : "")
    readonly property string modelSource: control.textValue(
                                                    modelData && modelData.source
                                                    ? modelData.source
                                                    : (modelData ? modelData.dataSource : ""))
    readonly property string modelLicense: control.textValue(
                                                     modelData ? modelData.license : "")
    readonly property string modelDescription: control.textValue(
                                                         modelData ? modelData.description : "")
    readonly property string modelUrl: control.textValue(
                                                 modelData ? modelData.url : "")
    readonly property string downloadUrl: control.textValue(
                                                    modelData ? modelData.downloadUrl : "")
    readonly property bool hasModelUrl: modelUrl.length > 0
    readonly property bool hasDownloadUrl: downloadUrl.length > 0
    readonly property bool hasDescription: modelDescription.length > 0

    function textValue(value) {
        return value === undefined || value === null ? "" : String(value).trim()
    }

    function formatNumber(value) {
        var number = Number(value || 0)
        if (number >= 1000000)
            return (number / 1000000).toFixed(1) + "M"
        if (number >= 1000)
            return (number / 1000).toFixed(1) + "K"
        return String(number)
    }

    preferredWidth: 840
    preferredHeight: 560
    title: qsTr("Model Details")

    component MetadataRow: RowLayout {
        required property string label
        required property string value

        visible: value.length > 0
        spacing: 12

        Label {
            Layout.preferredWidth: 44
            text: parent.label
            font: Typography.caption
            color: control.SmoothUI.theme.res.textFillColorSecondary
        }

        Label {
            Layout.fillWidth: true
            text: parent.value
            font: Typography.body
            color: control.SmoothUI.theme.res.textFillColorPrimary
            elide: Text.ElideRight
        }
    }

    RowLayout {
        id: contentLayout

        anchors.fill: parent
        anchors.margins: PrintWorkspaceStyle.space16
        spacing: 24

        Rectangle {
            id: previewSurface

            Layout.preferredWidth: Math.round(contentLayout.width * 0.62)
            Layout.fillHeight: true
            radius: 10
            color: control.SmoothUI.theme.res.cardBackgroundFillColorSecondary
            border.width: 1
            border.color: control.SmoothUI.theme.res.cardStrokeColorDefault
            clip: true

            Image {
                id: detailImage

                anchors.fill: parent
                anchors.margins: 12
                source: control.previewSource
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                visible: source.toString().length > 0 && status !== Image.Error
            }

            ProgressRing {
                anchors.centerIn: parent
                width: 32
                height: 32
                indeterminate: true
                visible: detailImage.status === Image.Loading
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 220)
                spacing: 10
                visible: detailImage.source.toString().length === 0
                         || detailImage.status === Image.Error

                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    width: 42
                    height: 42
                    source: FluentIcons.graph_CubeShape
                    color: control.SmoothUI.theme.res.textFillColorSecondary
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Preview Unavailable")
                    horizontalAlignment: Text.AlignHCenter
                    font: Typography.bodyStrong
                    color: control.SmoothUI.theme.res.textFillColorSecondary
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Label {
                Layout.fillWidth: true
                text: control.modelName
                font: Typography.subtitle
                color: control.SmoothUI.theme.res.textFillColorPrimary
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: control.SmoothUI.theme.res.cardBackgroundFillColorDefault
                        border.width: 1
                        border.color: control.SmoothUI.theme.res.cardStrokeColorDefault
                    }

                    RoundImageView {
                        id: authorImage

                        anchors.fill: parent
                        source: control.authorAvatar
                        visible: source.toString().length > 0
                                 && view.status !== Image.Error
                        view.fillMode: Image.PreserveAspectCrop
                    }

                    Label {
                        anchors.centerIn: parent
                        text: control.authorName.length > 0
                              ? control.authorName.charAt(0).toUpperCase() : ""
                        font: Typography.caption
                        color: control.SmoothUI.theme.res.textFillColorSecondary
                        visible: !authorImage.visible
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: control.authorName
                    font: Typography.bodyStrong
                    color: control.SmoothUI.theme.res.textFillColorPrimary
                    elide: Text.ElideRight
                }
            }

            RowLayout {
                spacing: 18

                RowLayout {
                    spacing: 5
                    Icon {
                        width: 14
                        height: 14
                        source: FluentIcons.graph_Like
                        color: control.SmoothUI.theme.res.textFillColorSecondary
                    }
                    Label {
                        text: control.formatNumber(
                                  control.modelData ? control.modelData.likes : 0)
                        font: Typography.caption
                        color: control.SmoothUI.theme.res.textFillColorSecondary
                    }
                }

                RowLayout {
                    spacing: 5
                    Icon {
                        width: 14
                        height: 14
                        source: FluentIcons.graph_Download
                        color: control.SmoothUI.theme.res.textFillColorSecondary
                    }
                    Label {
                        text: control.formatNumber(
                                  control.modelData ? control.modelData.downloads : 0)
                        font: Typography.caption
                        color: control.SmoothUI.theme.res.textFillColorSecondary
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: control.SmoothUI.theme.res.dividerStrokeColorDefault
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                MetadataRow {
                    Layout.fillWidth: true
                    label: qsTr("Format")
                    value: control.modelFormat
                }

                MetadataRow {
                    Layout.fillWidth: true
                    label: qsTr("Source")
                    value: control.modelSource
                }

                MetadataRow {
                    Layout.fillWidth: true
                    label: qsTr("License")
                    value: control.modelLicense
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                visible: control.hasDescription

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Model Overview")
                    font: Typography.bodyStrong
                    color: control.SmoothUI.theme.res.textFillColorPrimary
                }

                ScrollView {
                    id: descriptionScroll

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    Label {
                        width: descriptionScroll.availableWidth
                        text: control.modelDescription
                        font: Typography.body
                        color: control.SmoothUI.theme.res.textFillColorSecondary
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Item {
                Layout.fillHeight: true
                visible: !control.hasDescription
            }
        }
    }

    footerData: DialogButtonBox {
        anchors.fill: parent
        alignment: Qt.AlignRight

        Button {
            text: qsTr("Close")
            onClicked: control.close()
        }

        Button {
            text: qsTr("Open Models Page")
            icon.name: FluentIcons.graph_OpenInNewWindow
            highlighted: !control.hasDownloadUrl
            visible: control.hasModelUrl
            onClicked: Qt.openUrlExternally(control.modelUrl)
        }

        Button {
            text: qsTr("Download Model")
            icon.name: FluentIcons.graph_Download
            highlighted: true
            visible: control.hasDownloadUrl
            onClicked: {
                DownloadManager.downloadModel(control.modelData)
                NotificationManager.showInfo(
                            qsTr("Download started"),
                            qsTr("Downloading: %1").arg(control.modelName))
                control.close()
            }
        }
    }
}
