import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl

Item {
    id: root

    // 外部传入的 FPSMonitor 对象
    property var fpsMonitor: null
    property int targetRefreshRate: 60
    property bool expanded: false

    width: expanded ? 260 : 104
    height: expanded ? 136 : 34
    
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(
            Theme.res.cardBackgroundFillColorDefault.r,
            Theme.res.cardBackgroundFillColorDefault.g,
            Theme.res.cardBackgroundFillColorDefault.b,
            0.92
        )
        border.color: Theme.res.cardStrokeColorDefault
        border.width: 1
        radius: 9
    }
    
    Behavior on width {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }
    
    Behavior on height {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }
    
    Button {
        anchors.fill: parent
        visible: root.expanded
        flat: true
        padding: 0
        background: Item {}
        contentItem: Item {}
        onClicked: root.expanded = !root.expanded
    }
    
    // 简单视图
    Button {
        visible: !root.expanded
        anchors.fill: parent
        flat: true
        padding: 7
        background: Item {}
        onClicked: root.expanded = true

        contentItem: RowLayout {
            spacing: 5

            Icon {
                source: FluentIcons.graph_SpeedHigh
                width: 14
                height: 14
                color: {
                    if (!root.fpsMonitor) return Theme.res.textFillColorSecondary
                    if (root.fpsMonitor.currentFPS < 30) return Theme.res.systemFillColorCritical
                    if (root.fpsMonitor.currentFPS < 50) return Theme.res.systemFillColorCaution
                    return Theme.res.systemFillColorSuccess
                }
            }

            Label {
                text: root.fpsMonitor ? root.fpsMonitor.currentFPS.toFixed(1) + " FPS" : "-- FPS"
                font: Typography.caption
                color: Theme.res.textFillColorPrimary
            }
        }
    }
    
    // 详细视图
    Column {
        visible: root.expanded
        z: 1
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        
        // 标题行
        RowLayout {
            width: parent.width
            
            Label {
                text: qsTr("Performance Monitor")
                font: Typography.bodyStrong
                color: Theme.res.textFillColorPrimary
            }
            
            Item { Layout.fillWidth: true }
            
            IconButton {
                implicitWidth: 24
                implicitHeight: 24
                icon.name: FluentIcons.graph_ChromeClose
                icon.width: 12
                icon.height: 12
                onClicked: root.expanded = false
            }
        }
        
        // 分隔线
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.res.dividerStrokeColorDefault
        }
        
        // FPS 信息
        GridLayout {
            width: parent.width
            columns: 2
            rowSpacing: 4
            columnSpacing: 12
            
            Label {
                text: qsTr("Current FPS:")
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }
            
            Label {
                text: root.fpsMonitor ? root.fpsMonitor.currentFPS.toFixed(1) : "--"
                font: Typography.bodyStrong
                color: {
                    if (!root.fpsMonitor) return Theme.res.textFillColorSecondary
                    if (root.fpsMonitor.currentFPS < 30) return Theme.res.systemFillColorCritical
                    if (root.fpsMonitor.currentFPS < 50) return Theme.res.systemFillColorCaution
                    return Theme.res.systemFillColorSuccess
                }
            }
            
            Label {
                text: qsTr("Average FPS:")
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }
            
            Label {
                text: root.fpsMonitor ? root.fpsMonitor.averageFPS.toFixed(1) : "--"
                font: Typography.body
                color: Theme.res.textFillColorPrimary
            }
            
            Label {
                text: qsTr("Frame Time:")
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }
            
            Label {
                text: root.fpsMonitor ? root.fpsMonitor.frameTime.toFixed(2) + " ms" : "-- ms"
                font: Typography.body
                color: Theme.res.textFillColorPrimary
            }
            
            Label {
                text: qsTr("Dropped:")
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }
            
            Label {
                text: root.fpsMonitor ? root.fpsMonitor.droppedFrames.toString() : "0"
                font: Typography.body
                color: {
                    if (!root.fpsMonitor || root.fpsMonitor.droppedFrames === 0)
                        return Theme.res.textFillColorPrimary
                    return Theme.res.systemFillColorCaution
                }
            }
        }
        
        // 刷新率信息
        RowLayout {
            width: parent.width
            spacing: 8
            
            Label {
                text: qsTr("Target:")
                font: Typography.caption
                color: Theme.res.textFillColorSecondary
            }
            
            Label {
                text: root.targetRefreshRate + " Hz"
                font: Typography.body
                color: Theme.res.textFillColorPrimary
            }
            
            Item { Layout.fillWidth: true }
            
            Button {
                text: qsTr("Reset")
                height: 24
                enabled: root.fpsMonitor !== null
                onClicked: if (root.fpsMonitor) root.fpsMonitor.reset()
            }
        }
    }
}
