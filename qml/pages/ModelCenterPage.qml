import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform
import "../pages"

Item {
    id: root
    objectName: "modelCenterPage"
    SmoothUI.theme: Theme.of(root)

    // pageRouter 由 PageRouterView 自动注入，不需要声明默认值
    // 如果需要声明，使用 alias 保持注入的值
    property var pageRouter

    // 对外暴露的信号
    signal modelImported(string libraryItemId)

    // 当前选中的标签索引
    property int currentTabIndex: 0

    Rectangle {
        anchors.fill: parent
        color: root.SmoothUI.theme.res.solidBackgroundFillColorBase
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 顶部区域 - 标签切换居中
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: root.SmoothUI.theme.res.cardBackgroundColor

            // 使用 Item 作为容器，让标签按钮真正居中
            Item {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24

                // 自定义标签按钮组 - 更明显的样式
                Row {
                    id: tabBar
                    anchors.centerIn: parent
                    spacing: 4

                    // 标签按钮组件
                    component TabButton: StandardButton {
                        id: tabBtn
                        property bool isSelected: false
                        property string buttonText: ""
                        property var buttonIcon: ""
                        property string inputObjectName: ""

                        objectName: inputObjectName
                        text: buttonText
                        icon.name: buttonIcon
                        highlighted: isSelected
                        font: isSelected ? Typography.bodyStrong : Typography.body
                    }

                    // 在线库
                    TabButton {
                        isSelected: root.currentTabIndex === 0
                        buttonText: qsTr("Online Library")
                        buttonIcon: FluentIcons.graph_Cloud
                        onClicked: root.currentTabIndex = 0
                    }

                    // 我的模型
                    TabButton {
                        inputObjectName: "modelCenterMyModelsTabInput"
                        isSelected: root.currentTabIndex === 1
                        buttonText: qsTr("My Models")
                        buttonIcon: FluentIcons.graph_Folder
                        onClicked: root.currentTabIndex = 1
                    }

                    // AI创作
                    TabButton {
                        isSelected: root.currentTabIndex === 2
                        buttonText: qsTr("AI Creation")
                        buttonIcon: FluentIcons.graph_Robot
                        onClicked: root.currentTabIndex = 2
                    }
                }

                // 右侧：加载状态指示器 - 绝对定位在右侧
                RowLayout {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Label {
                        visible: currentTabIndex === 0 && ModelService.loading
                        text: qsTr("Loading...")
                        font: Typography.caption
                        color: root.SmoothUI.theme.res.textFillColorSecondary
                    }

                    ProgressRing {
                        visible: currentTabIndex === 0 && ModelService.loading
                        width: 16
                        height: 16
                    }
                }
            }
        }

        // 分隔线
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
        }

        // 内容区域 - 使用 StackLayout 切换，带滑动动画
        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // 页面容器 - 用于滑动动画
            Item {
                id: pageContainer
                width: contentArea.width * 3
                height: contentArea.height
                x: -root.currentTabIndex * contentArea.width

                Behavior on x {
                    NumberAnimation {
                        duration: 300
                        easing.type: Easing.OutCubic
                    }
                }

                // Tab 0: 在线库
                ModelLibraryPage {
                    id: onlineLibraryPage
                    width: contentArea.width
                    height: contentArea.height
                    x: 0
                    onRequestLocalModels: root.currentTabIndex = 1
                }

                // Tab 1: 我的模型
                DownloadedModelsPage {
                    id: downloadedModelsPage
                    width: contentArea.width
                    height: contentArea.height
                    x: contentArea.width
                    previewActive: root.visible && root.currentTabIndex === 1
                }

                // Tab 2: AI创作
                Generate3DPage {
                    id: aiCreationPage
                    width: contentArea.width
                    height: contentArea.height
                    x: contentArea.width * 2
                    pageRouter: root.pageRouter
                }
            }
        }
    }

    // 监听下载完成事件
    Connections {
        target: DownloadManager
        function onDownloadCompleted(modelId, filePath) {
            // 可选：下载完成后提示切换到"我的模型"标签
        }
    }
}
