import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0
import Render 1.0

Item {
    id: multiView3DPage


    // 背景色
    Rectangle {
        anchors.fill: parent
        color: Theme.res.solidBackgroundFillColorBase
    }

    // 标题栏
    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 48
        color: Theme.res.layerFillColorDefault
        z: 10

        // 分隔线
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.res.dividerStrokeColorDefault
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16

            Label {
                text: qsTr("Multi-View 3D Display")
                font: Typography.subtitle
                color: Theme.res.textFillColorPrimary
            }

            Item { Layout.fillWidth: true }

            // 布局选择按钮组
            Row {
                spacing: 8

                Label {
                    text: qsTr("Layout:")
                    font: Typography.body
                    color: Theme.res.textFillColorSecondary
                    anchors.verticalCenter: parent.verticalCenter
                }

                Row {
                    id: layoutSelector
                    property int currentIndex: 0

                    ButtonGroup {
                        id: layoutButtonGroup
                        exclusive: true
                    }

                    SegmentedButton {
                        property int buttonIndex: 0
                        text: qsTr("2x2")
                        checked: true
                        ButtonGroup.group: layoutButtonGroup
                        onClicked: {
                            layoutSelector.currentIndex = 0
                        }
                    }

                    SegmentedButton {
                        property int buttonIndex: 1
                        text: qsTr("Side by Side")
                        ButtonGroup.group: layoutButtonGroup
                        onClicked: {
                            layoutSelector.currentIndex = 1
                        }
                    }

                    SegmentedButton {
                        property int buttonIndex: 2
                        text: qsTr("Stacked")
                        ButtonGroup.group: layoutButtonGroup
                        onClicked: {
                            layoutSelector.currentIndex = 2
                        }
                    }

                    SegmentedButton {
                        property int buttonIndex: 3
                        text: qsTr("Focus")
                        ButtonGroup.group: layoutButtonGroup
                        onClicked: {
                            layoutSelector.currentIndex = 3
                            focusedView = 0  // 默认显示第一个视图
                        }
                    }
                }
            }

            Item { width: 16 }

            // 同步视图开关
            Switch {
                id: syncViewToggle
                text: qsTr("Sync Views")
                checked: false
            }

            Item { width: 16 }

            // 重置所有相机按钮
            Button {
                text: qsTr("Reset All Cameras")
                icon.name: FluentIcons.graph_Refresh
                icon.width: 16
                icon.height: 16
                onClicked: {
                    resetAllCameras()
                }
            }
        }
    }

    // 主内容区域
    Item {
        id: contentArea
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        anchors.margins: 8

        // 2x2 网格布局
        GridLayout {
            id: gridLayout
            anchors.fill: parent
            columns: 2
            rows: 2
            columnSpacing: 8
            rowSpacing: 8
            visible: layoutSelector.currentIndex === 0


            // Manually create 4 view windows instead of using Repeater

            // View 1 - Standard mode (top-left)
            Rectangle {
                id: view1
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.res.layerFillColorDefault
                border.color: Theme.res.cardStrokeColorDefault
                border.width: 1
                radius: 8
                clip: true

                // 视图标题
                Rectangle {
                    id: view1Header
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 32
                        color: Theme.res.layerFillColorAlt
                        radius: 8

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 8
                            color: parent.color
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8

                            Label {
                                text: qsTr("View 1 - Standard")
                                font: Typography.caption
                                color: Theme.res.textFillColorSecondary
                            }

                            Item { Layout.fillWidth: true }

                            // 视图控制按钮
                            Row {
                                spacing: 4

                                IconButton {
                                    implicitWidth: 24
                                    implicitHeight: 24
                                    icon.name: FluentIcons.graph_ZoomToFit
                                    icon.width: 12
                                    icon.height: 12

                                    ToolTip {
                                        text: qsTr("Fit to View")
                                        visible: parent.hovered
                                        delay: Theme.tooltipDelay
                                    }

                                    onClicked: {
                                        if (renderItems[0]) {
                                            ActionManager.triggerAction("camera.reset", {"viewIndex": 0})
                                        }
                                    }
                                }

                                IconButton {
                                    implicitWidth: 24
                                    implicitHeight: 24
                                    icon.name: FluentIcons.graph_Fullscreen
                                    icon.width: 12
                                    icon.height: 12

                                    ToolTip {
                                        text: qsTr("Maximize")
                                        visible: parent.hovered
                                        delay: Theme.tooltipDelay
                                    }

                                    onClicked: {
                                        layoutSelector.currentIndex = 3  // Switch to focus view
                                        focusedView = 0
                                    }
                                }
                            }
                        }
                    }

                    // VTK 渲染器
                    ThreadRendererQmlItem {
                        id: renderer1
                        anchors.top: view1Header.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 1

                        // 设置为标准模式
                        renderingMode: ThreadRendererQmlItem.RenderingMode.Standard

                        Component.onCompleted: {
                            console.log("🔄 View 1 ThreadRendererQmlItem created with Standard mode")
                            renderItems[0] = renderer1
                            renderersInitialized++

                            // 为视图1创建立方体
                            multiView3DPage.initializeView(0)
                        }

                        // 鼠标控制
                        ControlMouseArea {
                            anchors.fill: parent
                            rendererItem: renderer1
                            enabled: !syncViewToggle.checked || true  // View 1 总是可以接受输入

                            // Note: Camera synchronization would need to be implemented
                            // through properties or connections if needed
                        }
                    }

                    // FPS 显示
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        width: 60
                        height: 24
                        color: Qt.rgba(0, 0, 0, 0.6)
                        radius: 4

                        Label {
                            anchors.centerIn: parent
                            text: renderer1.fpsMonitor ? "FPS: " + Math.round(renderer1.fpsMonitor.currentFPS) : "FPS: --"
                            font: Typography.caption
                            color: Colors.white
                        }
                    }
                }

            // View 2 - Wireframe mode (top-right)
            Rectangle {
                id: view2
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.res.layerFillColorDefault
                border.color: Theme.res.cardStrokeColorDefault
                border.width: 1
                radius: 8
                clip: true

                Rectangle {
                    id: view2Header
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 32
                    color: Theme.res.layerFillColorAlt
                    radius: 8

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 8
                        color: parent.color
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8

                        Label {
                            text: qsTr("View 2 - Wireframe")
                            font: Typography.caption
                            color: Theme.res.textFillColorSecondary
                        }

                        Item { Layout.fillWidth: true }

                        Row {
                            spacing: 4

                            IconButton {
                                implicitWidth: 24
                                implicitHeight: 24
                                icon.name: FluentIcons.graph_ZoomToFit
                                icon.width: 12
                                icon.height: 12

                                ToolTip {
                                    text: qsTr("Fit to View")
                                    visible: parent.hovered
                                    delay: Theme.tooltipDelay
                                }

                                onClicked: {
                                    if (renderItems[1]) {
                                        ActionManager.triggerAction("camera.reset", {"viewIndex": 1})
                                    }
                                }
                            }

                            IconButton {
                                implicitWidth: 24
                                implicitHeight: 24
                                icon.name: FluentIcons.graph_Fullscreen
                                icon.width: 12
                                icon.height: 12

                                ToolTip {
                                    text: qsTr("Maximize")
                                    visible: parent.hovered
                                    delay: Theme.tooltipDelay
                                }

                                onClicked: {
                                    layoutSelector.currentIndex = 3
                                    focusedView = 1
                                }
                            }
                        }
                    }
                }

                ThreadRendererQmlItem {
                    id: renderer2
                    anchors.top: view2Header.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 1

                    renderingMode: ThreadRendererQmlItem.RenderingMode.Wireframe

                    Component.onCompleted: {
                        console.log("🔄 View 2 ThreadRendererQmlItem created with Wireframe mode")
                        renderItems[1] = renderer2
                        renderersInitialized++
                        multiView3DPage.initializeView(1)
                    }

                    ControlMouseArea {
                        anchors.fill: parent
                        rendererItem: renderer2
                        enabled: !syncViewToggle.checked
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    width: 60
                    height: 24
                    color: Qt.rgba(0, 0, 0, 0.6)
                    radius: 4

                    Label {
                        anchors.centerIn: parent
                        text: renderer2.fpsMonitor ? "FPS: " + Math.round(renderer2.fpsMonitor.currentFPS) : "FPS: --"
                        font: Typography.caption
                        color: Colors.white
                    }
                }
            }

            // View 3 - Simplified mode (bottom-left)
            Rectangle {
                id: view3
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.res.layerFillColorDefault
                border.color: Theme.res.cardStrokeColorDefault
                border.width: 1
                radius: 8
                clip: true

                Rectangle {
                    id: view3Header
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 32
                    color: Theme.res.layerFillColorAlt
                    radius: 8

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 8
                        color: parent.color
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8

                        Label {
                            text: qsTr("View 3 - Simplified")
                            font: Typography.caption
                            color: Theme.res.textFillColorSecondary
                        }

                        Item { Layout.fillWidth: true }

                        Row {
                            spacing: 4

                            IconButton {
                                implicitWidth: 24
                                implicitHeight: 24
                                icon.name: FluentIcons.graph_ZoomToFit
                                icon.width: 12
                                icon.height: 12

                                ToolTip {
                                    text: qsTr("Fit to View")
                                    visible: parent.hovered
                                    delay: Theme.tooltipDelay
                                }

                                onClicked: {
                                    if (renderItems[2]) {
                                        ActionManager.triggerAction("camera.reset", {"viewIndex": 2})
                                    }
                                }
                            }

                            IconButton {
                                implicitWidth: 24
                                implicitHeight: 24
                                icon.name: FluentIcons.graph_Fullscreen
                                icon.width: 12
                                icon.height: 12

                                ToolTip {
                                    text: qsTr("Maximize")
                                    visible: parent.hovered
                                    delay: Theme.tooltipDelay
                                }

                                onClicked: {
                                    layoutSelector.currentIndex = 3
                                    focusedView = 2
                                }
                            }
                        }
                    }
                }

                ThreadRendererQmlItem {
                    id: renderer3
                    anchors.top: view3Header.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 1

                    renderingMode: ThreadRendererQmlItem.RenderingMode.Simplified

                    Component.onCompleted: {
                        console.log("🔄 View 3 ThreadRendererQmlItem created with Simplified mode")
                        renderItems[2] = renderer3
                        renderersInitialized++
                        multiView3DPage.initializeView(2)
                    }

                    ControlMouseArea {
                        anchors.fill: parent
                        rendererItem: renderer3
                        enabled: !syncViewToggle.checked
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    width: 60
                    height: 24
                    color: Qt.rgba(0, 0, 0, 0.6)
                    radius: 4

                    Label {
                        anchors.centerIn: parent
                        text: renderer3.fpsMonitor ? "FPS: " + Math.round(renderer3.fpsMonitor.currentFPS) : "FPS: --"
                        font: Typography.caption
                        color: Colors.white
                    }
                }
            }

            // View 4 - Highlight mode (bottom-right)
            Rectangle {
                id: view4
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.res.layerFillColorDefault
                border.color: Theme.res.cardStrokeColorDefault
                border.width: 1
                radius: 8
                clip: true

                Rectangle {
                    id: view4Header
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 32
                    color: Theme.res.layerFillColorAlt
                    radius: 8

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 8
                        color: parent.color
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8

                        Label {
                            text: qsTr("View 4 - Highlight")
                            font: Typography.caption
                            color: Theme.res.textFillColorSecondary
                        }

                        Item { Layout.fillWidth: true }

                        Row {
                            spacing: 4

                            IconButton {
                                implicitWidth: 24
                                implicitHeight: 24
                                icon.name: FluentIcons.graph_ZoomToFit
                                icon.width: 12
                                icon.height: 12

                                ToolTip {
                                    text: qsTr("Fit to View")
                                    visible: parent.hovered
                                    delay: Theme.tooltipDelay
                                }

                                onClicked: {
                                    if (renderItems[3]) {
                                        ActionManager.triggerAction("camera.reset", {"viewIndex": 3})
                                    }
                                }
                            }

                            IconButton {
                                implicitWidth: 24
                                implicitHeight: 24
                                icon.name: FluentIcons.graph_Fullscreen
                                icon.width: 12
                                icon.height: 12

                                ToolTip {
                                    text: qsTr("Maximize")
                                    visible: parent.hovered
                                    delay: Theme.tooltipDelay
                                }

                                onClicked: {
                                    layoutSelector.currentIndex = 3
                                    focusedView = 3
                                }
                            }
                        }
                    }
                }

                ThreadRendererQmlItem {
                    id: renderer4
                    anchors.top: view4Header.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 1

                    renderingMode: ThreadRendererQmlItem.RenderingMode.Highlight

                    Component.onCompleted: {
                        console.log("🔄 View 4 ThreadRendererQmlItem created with Highlight mode")
                        renderItems[3] = renderer4
                        renderersInitialized++
                        multiView3DPage.initializeView(3)
                    }

                    ControlMouseArea {
                        anchors.fill: parent
                        rendererItem: renderer4
                        enabled: !syncViewToggle.checked
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    width: 60
                    height: 24
                    color: Qt.rgba(0, 0, 0, 0.6)
                    radius: 4

                    Label {
                        anchors.centerIn: parent
                        text: renderer4.fpsMonitor ? "FPS: " + Math.round(renderer4.fpsMonitor.currentFPS) : "FPS: --"
                        font: Typography.caption
                        color: Colors.white
                    }
                }
            }
        }

        // 水平布局 (Side by Side)
        RowLayout {
            id: horizontalLayout
            anchors.fill: parent
            spacing: 8
            visible: layoutSelector.currentIndex === 1

            Repeater {
                model: 2

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.res.layerFillColorDefault
                    border.color: Theme.res.cardStrokeColorDefault
                    border.width: 1
                    radius: 8
                    clip: true

                    ThreadRendererQmlItem {
                        id: horizontalRenderer
                        anchors.fill: parent
                        anchors.margins: 1

                        property int viewIndex: index
                    }

                    // FPS 显示
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        width: 60
                        height: 24
                        color: Qt.rgba(0, 0, 0, 0.6)
                        radius: 4

                        Label {
                            anchors.centerIn: parent
                            text: horizontalRenderer.fpsMonitor ? "FPS: " + Math.round(horizontalRenderer.fpsMonitor.currentFPS) : "FPS: --"
                            font: Typography.caption
                            color: Colors.white
                        }
                    }
                }
            }
        }

        // 垂直布局 (Stacked)
        ColumnLayout {
            id: verticalLayout
            anchors.fill: parent
            spacing: 8
            visible: layoutSelector.currentIndex === 2

            Repeater {
                model: 2

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.res.layerFillColorDefault
                    border.color: Theme.res.cardStrokeColorDefault
                    border.width: 1
                    radius: 8
                    clip: true

                    ThreadRendererQmlItem {
                        id: verticalRenderer
                        anchors.fill: parent
                        anchors.margins: 1

                        property int viewIndex: index
                    }

                    // FPS 显示
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        width: 60
                        height: 24
                        color: Qt.rgba(0, 0, 0, 0.6)
                        radius: 4

                        Label {
                            anchors.centerIn: parent
                            text: verticalRenderer.fpsMonitor ? "FPS: " + Math.round(verticalRenderer.fpsMonitor.currentFPS) : "FPS: --"
                            font: Typography.caption
                            color: Colors.white
                        }
                    }
                }
            }
        }

        // 焦点视图 (单个大视图)
        Rectangle {
            id: focusLayout
            anchors.fill: parent
            color: Theme.res.layerFillColorDefault
            border.color: Theme.res.cardStrokeColorDefault
            border.width: 1
            radius: 8
            clip: true
            visible: layoutSelector.currentIndex === 3

            // 标题栏
            Rectangle {
                id: focusViewHeader
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 32
                color: Theme.res.layerFillColorAlt
                radius: 8
                z: 1

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 8
                    color: parent.color
                }

                Label {
                    anchors.centerIn: parent
                    text: qsTr("View %1 - Focus Mode").arg(focusedView + 1)
                    font: Typography.body
                    color: Theme.res.textFillColorPrimary
                }
            }

            // 主渲染区域
            ThreadRendererQmlItem {
                id: focusRenderer
                anchors.top: focusViewHeader.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: thumbnailBar.top
                anchors.margins: 1

                Component.onCompleted: {
                    // 复用已有的渲染器
                    if (renderItems[focusedView]) {
                        // 这里可以添加视图同步逻辑
                    }
                }

                // FPS 显示
                Rectangle {
                    id: focusFpsDisplay
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: 10
                    anchors.rightMargin: 10
                    width: 80
                    height: 25
                    color: Qt.rgba(0, 0, 0, 0.7)
                    radius: 4
                    border.color: Qt.rgba(1, 1, 1, 0.3)
                    border.width: 1

                    Label {
                        anchors.centerIn: parent
                        text: focusRenderer.fpsMonitor ? "FPS: " + Math.round(focusRenderer.fpsMonitor.currentFPS) : "FPS: --"
                        font: Typography.caption
                        color: Colors.white
                    }
                }
            }

            // 缩略图选择栏
            Rectangle {
                id: thumbnailBar
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 100
                color: Qt.rgba(0, 0, 0, 0.2)

                Row {
                    anchors.centerIn: parent
                    spacing: 12

                    Repeater {
                        model: 4

                        Rectangle {
                            property int viewIndex: index
                            width: 100
                            height: 75
                            color: viewIndex === focusedView ?
                                   Colors.blue.defaultBrushFor() :
                                   Theme.res.layerFillColorDefault
                            border.color: viewIndex === focusedView ?
                                         Colors.blue.defaultBrushFor() :
                                         Theme.res.cardStrokeColorDefault
                            border.width: viewIndex === focusedView ? 3 : 1
                            radius: 6

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 2
                                color: Theme.res.layerFillColorDefault
                                radius: 4

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4

                                    Icon {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        source: parent.parent.viewIndex === 0 ? FluentIcons.graph_Cube :
                                              parent.parent.viewIndex === 1 ? FluentIcons.graph_CircleFill :
                                              parent.parent.viewIndex === 2 ? FluentIcons.graph_TriangleSolid :
                                              FluentIcons.graph_RectangleLandscape
                                        width: 24
                                        height: 24
                                        color: parent.parent.viewIndex === focusedView ?
                                               Colors.blue.defaultBrushFor() :
                                               Theme.res.textFillColorSecondary
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: qsTr("View %1").arg(parent.parent.viewIndex + 1)
                                        font: Typography.caption
                                        color: parent.parent.viewIndex === focusedView ?
                                               Colors.blue.defaultBrushFor() :
                                               Theme.res.textFillColorSecondary
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true

                                onEntered: parent.scale = 1.05
                                onExited: parent.scale = 1.0

                                onClicked: {
                                    console.log("Clicked view:", parent.viewIndex)
                                    focusedView = parent.viewIndex
                                    // 切换到对应视图的内容
                                    switchFocusView(parent.viewIndex)
                                }
                            }

                            Behavior on scale {
                                NumberAnimation { duration: 150 }
                            }
                        }
                    }
                }
            }
        }
    }

    // 底部状态栏
    Rectangle {
        id: statusBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 32
        color: Theme.res.layerFillColorDefault
        z: 10

        // 分隔线
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.res.dividerStrokeColorDefault
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16

            Label {
                text: qsTr("Render Threads Active: 4")
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }

            Item { Layout.fillWidth: true }

            Label {
                text: qsTr("Total Objects: %1").arg(totalObjectCount)
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }

            Rectangle {
                width: 1
                height: 16
                color: Theme.res.dividerStrokeColorDefault
            }

            Label {
                text: qsTr("Memory: -- MB")
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }
        }
    }

    // 属性和函数
    property var renderItems: [null, null, null, null]  // Pre-size for 4 views
    property int focusedView: 0
    property int totalObjectCount: 0
    property int renderersInitialized: 0  // Track how many renderers are initialized

    // 渲染模式从C++端获取，不再在QML定义

    function updateLayout() {
        // 布局切换逻辑 - 现在通过 visible 属性绑定自动处理
        var layoutNames = ["2x2 Grid", "Side by Side", "Stacked", "Focus View"]
        console.log("Layout changed to:", layoutNames[layoutSelector.currentIndex])
    }

    function switchFocusView(viewIndex) {
        // 切换焦点视图显示的内容
        focusedView = viewIndex
        console.log("Switched focus view to View", viewIndex + 1)

        // 如果需要，这里可以添加视图内容同步逻辑
        // 例如：将对应视图的内容复制到焦点视图
    }

    function resetAllCameras() {
        for (var i = 0; i < renderItems.length; i++) {
            if (renderItems[i]) {
                ActionManager.triggerAction("camera.reset", {"viewIndex": i})
            }
        }
    }

    function syncCameras(sourceIndex) {
        // 同步所有视图的相机到源视图
        if (!renderItems[sourceIndex]) return

        for (var i = 0; i < renderItems.length; i++) {
            if (i !== sourceIndex && renderItems[i]) {
                // 同步相机位置和方向
                // 这里需要实际的相机同步逻辑
            }
        }
    }

    // 不再需要这个函数，因为我们使用属性方式
    // 保留一个简单的版本以便动态切换
    function changeRenderingMode(viewIndex, mode) {
        if (renderItems[viewIndex]) {
            console.log("🎨 Changing view", viewIndex + 1, "to mode:", mode)
            renderItems[viewIndex].renderingMode = mode
        }
    }

    function initializeView(viewIndex) {
        console.log("🎯 initializeView called for viewIndex:", viewIndex)

        // 只在第一个视图初始化时创建共享对象
        // 所有视图将显示相同的对象，但使用不同的渲染模式
        if (viewIndex === 0) {
            console.log("📦 Creating objects for first view...")

            // 创建一个球体作为演示对象
            console.log("1️⃣ Creating sphere...")
            ActionManager.triggerAction("geometry.create.sphere", {
                "x": 0, "y": 0, "z": 0,
                "radius": 0.8
            })
            totalObjectCount++
            console.log("   Sphere creation triggered, totalObjectCount:", totalObjectCount)

            // 使用定时器延迟创建立方体，避免ActionManager阻塞
            Qt.callLater(function() {
                console.log("2️⃣ Creating cube...")
                ActionManager.triggerAction("geometry.create.cube", {
                    "x": 2.5, "y": 0, "z": 0,
                    "size": 1.0
                })
                totalObjectCount++
                console.log("   Cube creation triggered, totalObjectCount:", totalObjectCount)

                // 再次延迟创建圆柱体
                Qt.callLater(function() {
                    console.log("3️⃣ Creating cylinder...")
                    ActionManager.triggerAction("geometry.create.cylinder", {
                        "x": -2.5, "y": 0, "z": 0,
                        "height": 1.5,
                        "radius": 0.5
                    })
                    totalObjectCount++
                    console.log("   Cylinder creation triggered, totalObjectCount:", totalObjectCount)
                })
            })

            console.log("✅ All 3 objects creation triggered")
        } else {
            console.log("⏭️ Skipping object creation for viewIndex:", viewIndex)
        }
        // 其他视图不创建新对象，它们会自动显示共享的DB对象
        // 通过不同的renderingMode属性来展示不同的渲染效果
    }

}
