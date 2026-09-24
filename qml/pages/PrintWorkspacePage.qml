import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform
import Render 1.0
import "../components"
import "../workspaces/normal/components"

Item {
    id: root

    // Each concrete page fixes these values once. Separate page instances own
    // separate renderers while sharing this pixel-identical presentation.
    property string workspaceName: "normal"
    property int rendererRole: ThreadRendererQmlItem.Compatibility
    property bool renderActive: true
    property bool initializeScene: false
    property alias renderer: threadRenderer
    readonly property var workspaceContent: centralFragmentLoader.item
    required property QtObject workspaceContext
    signal workspaceRequested(string workspaceName)

    // 声明标题栏背景配置（渐变：上方偏白 → 下方天蓝色，高透明度）
    // #30 = 19% 不透明度，#50 = 31% 不透明度
    property var appBarBackground: ({
        type: "gradient",
        colors: ["#30FFFFFF", "#5087CEEB"],
        direction: "vertical"
    })

    // 声明侧边栏背景配置（与标题栏风格一致，80%以上不透明度）
    // #CC = 80% 不透明度，#D0 = 82% 不透明度
    property var sideBarBackground: ({
        type: "gradient",
        colors: ["#CCFFFFFF", "#D087CEEB"],
        direction: "vertical"
    })

    property bool gridVisible: true
    property bool showBedBounds: true
    property string currentSettingsPanel: ""  // 当前打开的设置面板类型

    // 初始化场景
    function initScene() {
        if (workspaceContext.sceneInitialized) {
            return;
        }

        // App 选择默认机型，床面尺寸与资源全部来自 libslicer 内置 preset。
        var success = ActionManager.triggerAction("scene.init", {
            "printBed": SliceSettingsBridge.selectedMachine
        });

        if (success) {
            workspaceContext.sceneInitialized = true;
        }
    }

    // 主布局 - 使用 RowLayout
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // 中央视图区域
        Item {
            id: centralArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 暴露 ThreadRendererQmlItem 引用
            property alias renderer: threadRenderer

            // VTK emits transparent pixels for the empty part of the view.
            // A DockWidget adds another composition boundary, so without an
            // explicit opaque backing those pixels fall through to the white
            // native window surface. Keep the canvas identical in live output
            // and QQuickWindow::grabWindow screenshots.
            Rectangle {
                anchors.fill: parent
                color: "#FFF4F6F8"
            }

            // Page-role-selected renderer
            ThreadRendererQmlItem {
                id: threadRenderer
                anchors.fill: parent
                rendererRole: root.rendererRole
                renderActive: root.renderActive

                Component.onCompleted: {
                    if (root.initializeScene) {
                        sceneInitTimer.start()
                    }
                }

                Timer {
                    id: sceneInitTimer
                    interval: 100
                    repeat: true
                    onTriggered: {
                        initScene()
                        if (workspaceContext.sceneInitialized) {
                            stop()
                        }
                    }
                }

                // 鼠标控制区域
                ControlMouseArea {
                    id: mouseControl
                    objectName: "print.viewport." + root.workspaceName + ".input"
                    anchors.fill: parent
                    rendererItem: threadRenderer
                }
            }

            // Loading 覆盖层
            Rectangle {
                id: loadingOverlay
                anchors.fill: parent
                z: 5
                color: Theme.res.solidBackgroundFillColorBase
                // Once a valid frame exists, keep it visible during scene updates.
                visible: !threadRenderer.hasPresentedFrame && !threadRenderer.isFrameReady

                Column {
                    anchors.centerIn: parent
                    spacing: 20

                    ProgressRing {
                        indeterminate: true
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 80
                        height: 80
                        strokeWidth: 7
                    }

                    Label {
                        text: qsTr("Loading model...")
                        font: Typography.subtitle
                        color: Theme.res.textFillColorPrimary
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }

            // UI 层
            Loader {
                id: centralFragmentLoader
                anchors.fill: parent
                z: 50
                source: "qrc:/qt/qml/GPlatform/qml/workspaces/" + root.workspaceName + "/centralPart.qml"
            }

            // 拖拽文件支持
            DropArea {
                id: dropArea
                anchors.fill: parent
                z: 100

                property bool isDragging: false
                property string dragFileType: ""  // "model" or "gcode"

                // 支持的文件扩展名
                property var modelExtensions: ["stl", "obj", "3mf", "glb", "gltf", "ply", "3ds", "dae", "fbx"]
                property var gcodeExtensions: ["gcode", "gco", "g"]

                // 检查文件类型
                function getFileType(url) {
                    var path = url.toString()
                    // 移除 file:/// 前缀
                    if (path.startsWith("file:///")) {
                        path = path.substring(7)
                    } else if (path.startsWith("file://")) {
                        path = path.substring(7)
                    }

                    var ext = path.split('.').pop().toLowerCase()

                    if (modelExtensions.indexOf(ext) !== -1) {
                        return "model"
                    } else if (gcodeExtensions.indexOf(ext) !== -1) {
                        return "gcode"
                    }
                    return ""
                }

                onEntered: function(drag) {
                    if (drag.hasUrls) {
                        var urls = drag.urls
                        for (var i = 0; i < urls.length; i++) {
                            var fileType = getFileType(urls[i])
                            if (fileType !== "") {
                                isDragging = true
                                dragFileType = fileType
                                drag.accepted = true
                                return
                            }
                        }
                    }
                    drag.accepted = false
                }

                onExited: {
                    isDragging = false
                    dragFileType = ""
                }

                onDropped: function(drop) {
                    if (drop.hasUrls) {
                        var urls = drop.urls
                        var accepted = false
                        for (var i = 0; i < urls.length; i++) {
                            var fileType = getFileType(urls[i])
                            if (fileType !== "") {
                                if (ActionManager.triggerAction("file.import", {filePath: urls[i]}))
                                    accepted = true
                                else
                                    console.error("File import failed:", JSON.stringify(ActionManager.lastActionOutcome()))
                            }
                        }
                        drop.accepted = accepted
                    } else {
                        drop.accepted = false
                    }
                    isDragging = false
                    dragFileType = ""
                }

                // 拖拽提示覆盖层
                Rectangle {
                    anchors.fill: parent
                    color: "#80000000"  // 半透明黑色背景
                    visible: dropArea.isDragging
                    z: 1000

                    Rectangle {
                        anchors.centerIn: parent
                        width: 300
                        height: 150
                        radius: 16
                        color: "#F3F3F3"  // 浅灰背景
                        border.color: "#0078D4"  // 蓝色边框
                        border.width: 2

                        Column {
                            anchors.centerIn: parent
                            spacing: 16

                            // 图标
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: dropArea.dragFileType === "gcode" ? "\uE8E5" : "\uE8E4"  // 文档或模型图标
                                font.family: "Segoe Fluent Icons"
                                font.pixelSize: 48
                                color: "#0078D4"  // 蓝色
                            }

                            // 提示文字
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: dropArea.dragFileType === "gcode" ?
                                    qsTr("Drop to load a G-code file") :
                                    qsTr("Drop to import a model file")
                                font: Typography.subtitle
                                color: "#1A1A1A"
                            }

                            // 文件类型说明
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: dropArea.dragFileType === "gcode" ?
                                    qsTr("Supports .gcode and .gco files") :
                                    qsTr("Supports .stl, .obj, .3mf, and other formats")
                                font: Typography.caption
                                color: "#666666"
                            }
                        }
                    }
                }
            }
        }

    }

}
