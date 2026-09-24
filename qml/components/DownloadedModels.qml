import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Item {
    id: control
    objectName: "downloadedModelsContent"

    property var downloadedModels: []
    property bool active: false
    property int readyPreviewCount: 0
    readonly property bool screenshotReady: active
                                               && downloadedModels.length > 0
                                               && readyPreviewCount >= Math.min(
                                                   3, downloadedModels.length)
    property string searchText: ""
    property string sortBy: "date" // date, name, size

    signal modelImported(string libraryItemId)

    function updateModelProjection() {
        downloadedModels = LocalModelLibraryBridge.models(searchText, sortBy)
    }

    function updateReadyPreviewCount() {
        var count = 0
        for (var i = 0; i < downloadedModels.length; ++i) {
            if (ModelPreviewService.isPreviewCached(downloadedModels[i].filePath)) {
                count += 1
            }
        }
        readyPreviewCount = count
    }

    Component.onCompleted: {
        updateModelProjection()
        Qt.callLater(updateReadyPreviewCount)
    }
    onActiveChanged: if (active) {
        updateModelProjection()
        Qt.callLater(updateReadyPreviewCount)
    }

    Connections {
        target: LocalModelLibraryBridge
        function onModelsChanged() {
            control.updateModelProjection()
            control.updateReadyPreviewCount()
        }
    }

    Connections {
        target: ModelPreviewService
        function onPreviewReady(filePath, previewUrl) {
            control.updateReadyPreviewCount()
        }
        function onPreviewFailed(filePath, errorMessage) {
            control.updateReadyPreviewCount()
        }
    }

    function formatFileSize(sizeStr) {
        var size = parseInt(sizeStr)
        if (size >= 1024 * 1024 * 1024) {
            return (size / (1024 * 1024 * 1024)).toFixed(2) + " GB"
        } else if (size >= 1024 * 1024) {
            return (size / (1024 * 1024)).toFixed(2) + " MB"
        } else if (size >= 1024) {
            return (size / 1024).toFixed(2) + " KB"
        }
        return size + " B"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 24
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        spacing: 20

        // 顶部工具栏 - 标题和操作按钮
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 50

            RowLayout {
                anchors.fill: parent
                spacing: 12

                // 标题
                ColumnLayout {
                    spacing: 4

                    Label {
                        text: qsTr("My Models")
                        font: Typography.title
                        color: "#FFFFFF"
                    }

                    Label {
                        text: qsTr("Manage your downloaded models")
                        font: Typography.body
                        color: "#A0B8C0"
                    }
                }

                Item { Layout.fillWidth: true }

                // 模型数量标签
                Label {
                    text: "(" + downloadedModels.length + "/" + LocalModelLibraryBridge.totalCount + ")"
                    font: Typography.body
                    color: "#A0B8C0"
                }

                // 刷新按钮
                IconButton {
                    icon.name: FluentIcons.graph_Refresh
                    icon.width: 16
                    icon.height: 16
                    ToolTip.text: qsTr("Refresh")
                    ToolTip.visible: hovered
                    ToolTip.delay: Theme.tooltipDelay
                    onClicked: ActionManager.triggerAction("model.library.local.refresh", {})
                }

                // 排序选择
                ComboBox {
                    Layout.preferredWidth: 150
                    Layout.preferredHeight: 36
                    model: [
                        { text: qsTr("Sort by Date"), value: "date" },
                        { text: qsTr("Sort by Name"), value: "name" },
                        { text: qsTr("Sort by Size"), value: "size" }
                    ]
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: 0
                    onCurrentValueChanged: {
                        if (currentValue === undefined || currentValue === "") {
                            return
                        }
                        sortBy = currentValue
                        control.updateModelProjection()
                    }
                }
            }
        }

        // 搜索框行
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 36

            TextBox {
                id: searchBox
                anchors.fill: parent
                placeholderText: qsTr("Search models...")
                onTextChanged: {
                    searchText = text
                    control.updateModelProjection()
                }
            }
        }

        // 分隔线
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Qt.rgba(1, 1, 1, 0.1)
        }

        // 内容区域
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 空状态
            ColumnLayout {
                anchors.centerIn: parent
                visible: downloadedModels.length === 0
                spacing: 16

                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    source: FluentIcons.graph_FolderOpen
                    width: 64
                    height: 64
                    color: "#607080"
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: LocalModelLibraryBridge.totalCount === 0
                          ? qsTr("No downloaded models yet")
                          : qsTr("No matching models")
                    font: Typography.subtitle
                    color: "#A0B8C0"
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: LocalModelLibraryBridge.totalCount === 0
                          ? qsTr("Downloaded models will appear here")
                          : qsTr("Try a different search term")
                    font: Typography.body
                    color: "#8090A0"
                }
            }

            // 模型列表
            ListView {
                id: listView
                anchors.fill: parent
                anchors.margins: 16
                visible: downloadedModels.length > 0
                clip: true
                spacing: 8
                model: downloadedModels

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                delegate: Rectangle {
                    id: listItem
                    width: listView.width
                    height: 112
                    radius: 16
                    color: itemMouseArea.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.15)

                    property var modelData: listView.model[index] || {}
                    property string previewSource: ""
                    property bool previewPending: false
                    property bool previewFailed: false
                    property bool pageActive: control.active
                    property bool inViewport: y + height >= listView.contentY
                                                  && y <= listView.contentY + listView.height

                    function refreshPreview() {
                        previewSource = ""
                        previewPending = false
                        previewFailed = false
                        if (!pageActive || !inViewport || !modelData.filePath) {
                            return
                        }
                        previewSource = ModelPreviewService.requestPreview(
                                    modelData.filePath,
                                    modelData.sourceThumbnailUrl || modelData.thumbnail || "")
                        previewPending = previewSource === ""
                    }

                    Component.onCompleted: refreshPreview()
                    onModelDataChanged: refreshPreview()
                    onPageActiveChanged: {
                        if (pageActive) {
                            refreshPreview()
                        }
                    }
                    onInViewportChanged: {
                        if (inViewport) {
                            refreshPreview()
                        }
                    }

                    Connections {
                        target: ModelPreviewService

                        function onPreviewReady(filePath, previewUrl) {
                            if (filePath === listItem.modelData.filePath) {
                                listItem.previewSource = previewUrl
                                listItem.previewPending = false
                                listItem.previewFailed = false
                            }
                        }

                        function onPreviewFailed(filePath, errorMessage) {
                            if (filePath === listItem.modelData.filePath) {
                                listItem.previewPending = false
                                listItem.previewFailed = true
                            }
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 16

                        // 真实模型预览。缓存缺失时服务会异步生成，列表先显示占位状态。
                        Rectangle {
                            Layout.preferredWidth: 104
                            Layout.preferredHeight: 80
                            radius: 12
                            color: Qt.rgba(1, 1, 1, 0.055)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.12)
                            clip: true

                            Image {
                                id: modelPreview
                                objectName: "localModelPreviewImage"
                                property string previewFilePath: listItem.modelData.filePath || ""
                                anchors.fill: parent
                                anchors.margins: 4
                                source: listItem.previewSource
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: true
                                visible: source !== "" && status !== Image.Error

                            }

                            Icon {
                                anchors.centerIn: parent
                                source: FluentIcons.graph_CubeShape
                                width: 24
                                height: 24
                                color: "#7895A0"
                                opacity: listItem.previewPending ? 0.38 : 1.0
                                visible: modelPreview.source === ""
                                         || listItem.previewFailed
                                         || modelPreview.status === Image.Error
                            }

                            BusyIndicator {
                                anchors.centerIn: parent
                                width: 24
                                height: 24
                                running: listItem.previewPending
                                visible: running
                            }
                        }

                        // 内容
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                Layout.fillWidth: true
                                text: modelData.name || "Unknown Model"
                                font: Typography.bodyStrong
                                color: "#FFFFFF"
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.filePath || ""
                                font: Typography.caption
                                color: "#8090A0"
                                elide: Text.ElideMiddle
                            }

                            RowLayout {
                                spacing: 16

                                Label {
                                    text: formatFileSize(modelData.fileSize || "0")
                                    font: Typography.caption
                                    color: "#A0B8C0"
                                }

                                Rectangle {
                                    width: 1
                                    height: 12
                                    color: Qt.rgba(1, 1, 1, 0.2)
                                }

                                Label {
                                    text: modelData.downloadDate || ""
                                    font: Typography.caption
                                    color: "#A0B8C0"
                                }
                            }
                        }

                        // 操作按钮
                        RowLayout {
                            spacing: 4

                            // 导入模型
                            IconButton {
                                icon.name: FluentIcons.graph_Import
                                icon.width: 16
                                icon.height: 16
                                icon.color: hovered ? Theme.accentColor.defaultBrushFor() : "#A0B8C0"
                                ToolTip.text: qsTr("Import Model")
                                ToolTip.visible: hovered
                                ToolTip.delay: Theme.tooltipDelay
                                onClicked: {
                                    control.modelImported(modelData.libraryItemId)
                                }
                            }

                            // 在文件管理器中打开
                            IconButton {
                                icon.name: FluentIcons.graph_FolderOpen
                                icon.width: 16
                                icon.height: 16
                                icon.color: hovered ? Theme.accentColor.defaultBrushFor() : "#A0B8C0"
                                ToolTip.text: qsTr("Open in Finder")
                                ToolTip.visible: hovered
                                ToolTip.delay: Theme.tooltipDelay
                                onClicked: {
                                    ActionManager.triggerAction(
                                                "model.library.local.reveal",
                                                { libraryItemId: modelData.libraryItemId })
                                }
                            }

                            // 删除
                            IconButton {
                                icon.name: FluentIcons.graph_Delete
                                icon.width: 16
                                icon.height: 16
                                icon.color: hovered ? "#FF6B6B" : "#A0B8C0"
                                ToolTip.text: qsTr("Delete Model")
                                ToolTip.visible: hovered
                                ToolTip.delay: Theme.tooltipDelay
                                onClicked: {
                                    ActionManager.triggerAction(
                                                "model.library.local.delete",
                                                { libraryItemId: modelData.libraryItemId })
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: itemMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
            }
        }
    }
}
