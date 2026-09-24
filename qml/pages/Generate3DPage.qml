import QtQuick
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import "./generate3d" as Generate3D

// 使用简单的Item而非Layout避免递归
Item {
    id: root

    property string currentState: "idle"
    property string inputMode: "text"
    property var selectedStyles: []
    property bool isGenerating: false
    property bool hasGenerated: false  // 是否已经生成过

    // 动态查找 pageRouter 的函数
    function findRouter() {
        var p = root.parent
        while (p) {
            if (p.router !== undefined) return p.router
            if (p.pageRouter !== undefined) return p.pageRouter
            p = p.parent
        }
        return null
    }

    // pageRouter 属性 - 在需要时动态获取
    property var pageRouter: findRouter()

    // 背景由 ModelCenterPage 管理，这里不需要单独设置

    // 顶部横幅 - 固定高度
    Generate3D.TopBanner {
        id: topBanner
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20
        height: 140
    }

    // 左右面板容器
    Item {
        id: panelsContainer
        anchors.top: topBanner.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 24
        anchors.topMargin: 16

        // 左侧面板背景
        Rectangle {
            id: leftPanelBg
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: (parent.width - 48) * 0.48
            color: Qt.rgba(50/255, 30/255, 80/255, 0.6)
            radius: 20

            // 内阴影效果
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                color: "transparent"
                radius: 19
                border.color: Qt.rgba(1, 1, 1, 0.05)
                border.width: 1
            }
        }

        // 右侧面板背景
        Rectangle {
            id: rightPanelBg
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: (parent.width - 48) * 0.48
            color: Qt.rgba(50/255, 30/255, 80/255, 0.6)
            radius: 20

            // 内阴影效果
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                color: "transparent"
                radius: 19
                border.color: Qt.rgba(1, 1, 1, 0.05)
                border.width: 1
            }
        }

        // 左侧输入面板
        Generate3D.LeftPart {
            id: leftPart
            anchors.top: leftPanelBg.top
            anchors.bottom: leftPanelBg.bottom
            anchors.left: leftPanelBg.left
            anchors.right: leftPanelBg.right
            anchors.margins: 16
            inputMode: root.inputMode
            selectedStyles: root.selectedStyles
            isGenerating: root.isGenerating
            hasGenerated: root.hasGenerated
            onInputModeChanged: root.inputMode = inputMode
            onSelectedStylesChanged: root.selectedStyles = selectedStyles
            onGenerateClicked: {
                root.isGenerating = true
                root.currentState = "generating"
            }
            onGenerationCompleted: {
                root.isGenerating = false
                root.hasGenerated = true
                root.currentState = "completed"
            }
        }

        // 右侧预览面板
        Generate3D.RightPart {
            id: rightPart
            anchors.top: rightPanelBg.top
            anchors.bottom: rightPanelBg.bottom
            anchors.left: rightPanelBg.left
            anchors.right: rightPanelBg.right
            anchors.margins: 16
            currentState: root.currentState
            pageRouter: root.pageRouter  // 传递页面路由器
        }
    }
}
