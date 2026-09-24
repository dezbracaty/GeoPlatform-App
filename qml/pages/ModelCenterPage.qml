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

    // pageRouter 由 PageRouterView 自动注入，不需要声明默认值
    // 如果需要声明，使用 alias 保持注入的值
    property var pageRouter

    // 对外暴露的信号
    signal modelImported(string libraryItemId)

    // 当前选中的标签索引
    property int currentTabIndex: 0
    property bool useLightLibraryTheme: currentTabIndex === 0 && !root.SmoothUI.dark

    // 声明标题栏背景配置（与页面深蓝色背景协调，底部接近页面背景色）
    property var appBarBackground: !root.SmoothUI.dark ? ({
        type: "color",
        color: "#F4F6F8"
    }) : ({
        type: "gradient",
        colors: ["#603A5A7C", "#801A3A5C"],
        direction: "vertical"
    })

    // 声明侧边栏背景配置（与标题栏风格一致，90%不透明度）
    // #E6 = 90% 不透明度
    property var sideBarBackground: !root.SmoothUI.dark ? ({
        type: "color",
        color: "#F0F2F5"
    }) : ({
        type: "gradient",
        colors: ["#E63A5A7C", "#E61A3A5C"],
        direction: "vertical"
    })

    // 声明侧边栏文字样式（白色/浅色系，配合深蓝色背景）
    property var sideBarTextStyle: !root.SmoothUI.dark ? ({
        normalColor: "#59616D",
        selectedColor: "#202328",
        hoverColor: "#202328",
        disabledColor: "#9DA3AC"
    }) : ({
        normalColor: "#E0E0E0",      // 浅灰色 - 正常状态
        selectedColor: "#FFFFFF",    // 纯白色 - 选中状态
        hoverColor: "#F0F0F0",       // 接近白色 - 悬停状态
        disabledColor: "#808080"     // 中灰色 - 禁用状态
    })

    // 背景层容器 - 用于切换不同背景
    Item {
        anchors.fill: parent

        // 在线库使用模型社区所需的安静中性背景。
        Rectangle {
            anchors.fill: parent
            visible: root.currentTabIndex === 0
            color: root.SmoothUI.dark ? "#17191D" : "#F4F6F8"
        }

        // 我的模型背景 - 青绿色系渐变
        Rectangle {
            anchors.fill: parent
            visible: root.currentTabIndex === 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#0A1A18" }
                GradientStop { position: 0.5; color: "#0F2D28" }
                GradientStop { position: 1.0; color: "#1A4038" }
            }
        }

        // AI创作背景 - 深紫渐变
        Rectangle {
            anchors.fill: parent
            visible: root.currentTabIndex === 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#1A0A3E" }
                GradientStop { position: 0.5; color: "#2A1A4A" }
                GradientStop { position: 1.0; color: "#3A2A7A" }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 顶部区域 - 标签切换居中
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: root.currentTabIndex === 0
                   ? (root.SmoothUI.dark ? "#1D2025" : "#FFFFFF")
                   : "transparent"

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
                    component TabButton: Rectangle {
                        id: tabBtn
                        width: tabContent.implicitWidth + 28
                        height: 36
                        radius: 8
                        border.width: isSelected ? 2 : 1

                        property bool isSelected: false
                        property string buttonText: ""
                        property var buttonIcon: ""
                        property string inputObjectName: ""
                        property color selectedColor: Theme.accentColor.defaultBrushFor()
                        property bool isHovered: false

                        // 使用绑定计算颜色，而不是直接赋值
                        color: {
                            if (isSelected) {
                                return selectedColor
                            } else if (isHovered) {
                                return root.useLightLibraryTheme
                                        ? "#F0F2F5" : Qt.rgba(1, 1, 1, 0.15)
                            } else {
                                return root.useLightLibraryTheme
                                        ? "transparent" : Qt.rgba(1, 1, 1, 0.1)
                            }
                        }
                        border.color: isSelected ? selectedColor
                                                  : (root.useLightLibraryTheme
                                                     ? "#E5E8EC"
                                                     : Qt.rgba(1, 1, 1, 0.2))

                        Behavior on color {
                            ColorAnimation { duration: 150 }
                        }

                        Behavior on border.color {
                            ColorAnimation { duration: 150 }
                        }

                        Row {
                            id: tabContent
                            anchors.centerIn: parent
                            spacing: 6

                            Icon {
                                source: tabBtn.buttonIcon
                                width: 14
                                height: 14
                                color: tabBtn.isSelected ? "#FFFFFF"
                                                         : (root.useLightLibraryTheme
                                                            ? "#69717D" : "#C0C8D0")
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: tabBtn.buttonText
                                font.pixelSize: 14
                                font.bold: tabBtn.isSelected
                                color: tabBtn.isSelected ? "#FFFFFF"
                                                         : (root.useLightLibraryTheme
                                                            ? "#4F5661" : "#C0C8D0")
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            objectName: tabBtn.inputObjectName
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: tabBtn.clicked()
                            onEntered: tabBtn.isHovered = true
                            onExited: tabBtn.isHovered = false
                        }

                        signal clicked()
                    }

                    // 在线库 - 亮蓝色
                    TabButton {
                        isSelected: root.currentTabIndex === 0
                        buttonText: qsTr("Online Library")
                        buttonIcon: FluentIcons.graph_Cloud
                        selectedColor: Theme.accentColor.defaultBrushFor()
                        onClicked: root.currentTabIndex = 0
                    }

                    // 我的模型 - 亮青绿色
                    TabButton {
                        inputObjectName: "modelCenterMyModelsTabInput"
                        isSelected: root.currentTabIndex === 1
                        buttonText: qsTr("My Models")
                        buttonIcon: FluentIcons.graph_Folder
                        selectedColor: "#3A9078"
                        onClicked: root.currentTabIndex = 1
                    }

                    // AI创作 - 亮紫色
                    TabButton {
                        isSelected: root.currentTabIndex === 2
                        buttonText: qsTr("AI Creation")
                        buttonIcon: FluentIcons.graph_Robot
                        selectedColor: "#7A5ABC"
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
                        color: root.useLightLibraryTheme ? "#69717D" : "#C0C8D0"
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
            color: root.useLightLibraryTheme ? "#E5E8EC" : Qt.rgba(1, 1, 1, 0.1)
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
