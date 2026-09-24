import QtQuick
import QtQuick.Effects
import SmoothUI.Controls
import SmoothUI.impl

Rectangle {
    id: root
    
    // Establish SmoothUI theme context (必须在最开始)
    SmoothUI.theme: Theme.of(root)
    
    // Properties
    property bool isDragging: false
    property bool showDragHandle: true
    property int dragHandleHeight: 20
    property alias contentItem: contentLoader.sourceComponent
    property int contentMargin: 0
    property real shadowBlur: 0.65
    property real shadowOpacity: 0.14
    property real shadowVerticalOffset: 2
    property real dragShadowBlur: 1.0
    property real dragShadowOpacity: 0.24
    property real dragShadowVerticalOffset: 4
    property real normalScale: 1.0
    property real dragScale: 1.015
    // Draggable panels sit over a 3D surface and must remain visually solid.
    // layerFillColorDefault is translucent (#80ffffff in the light theme),
    // which disappears when the native window compositor supplies white
    // behind a transparent renderer pixel.
    property color normalColor: root.SmoothUI.theme.res.solidBackgroundFillColorBase
    property color dragColor: root.SmoothUI.theme.res.solidBackgroundFillColorQuarternary
    property color normalBorderColor: root.SmoothUI.theme.res.cardStrokeColorDefault
    property color dragBorderColor: Theme.primaryColor.defaultBrushFor(root.SmoothUI.dark)
    property real normalBorderWidth: 1
    property real dragBorderWidth: 2
    property real animationDuration: 150
    
    // Constraints for dragging
    property real minX: 16
    property real maxX: parent ? parent.width - width - 16 : 0
    property real minY: 16
    property real maxY: parent ? parent.height - height - 16 : 0
    
    // Default properties
    z: 100
    radius: 10
    color: isDragging ? dragColor : normalColor
    border.color: isDragging ? dragBorderColor : normalBorderColor
    border.width: isDragging ? dragBorderWidth : normalBorderWidth
    scale: isDragging ? dragScale : normalScale
    
    // Animations
    Behavior on color { ColorAnimation { duration: animationDuration } }
    Behavior on border.color { ColorAnimation { duration: animationDuration } }
    Behavior on border.width { NumberAnimation { duration: animationDuration } }
    Behavior on scale { NumberAnimation { duration: animationDuration; easing.type: Easing.OutQuad } }
    
    // Shadow effect
    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: "#000000"
        shadowBlur: root.isDragging ? root.dragShadowBlur : root.shadowBlur
        shadowOpacity: root.isDragging ? root.dragShadowOpacity : root.shadowOpacity
        shadowVerticalOffset: root.isDragging ? root.dragShadowVerticalOffset : root.shadowVerticalOffset
    }
    
    // Drag handle
    Rectangle {
        id: dragHandle
        visible: showDragHandle
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: dragHandleHeight
        color: "transparent"
        
        // Handle indicator
        Rectangle {
            anchors.centerIn: parent
            width: 32
            height: 3
            radius: 1.5
            color: root.isDragging ? root.SmoothUI.theme.res.textFillColorSecondary : root.SmoothUI.theme.res.textFillColorTertiary
            
            Behavior on color { ColorAnimation { duration: animationDuration } }
        }
        
        DragHandler {
            id: dragHandler
            target: root
            
            onActiveChanged: {
                root.isDragging = active
                if (active) {
                    root.z = 1000
                } else {
                    root.z = 100
                }
            }
            
            xAxis.minimum: root.minX
            xAxis.maximum: root.maxX
            yAxis.minimum: root.minY
            yAxis.maximum: root.maxY
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }
    }
    
    // Content loader
    Loader {
        id: contentLoader
        anchors.fill: parent
        anchors.topMargin: showDragHandle ? dragHandleHeight : 0
        anchors.leftMargin: contentMargin
        anchors.rightMargin: contentMargin
        anchors.bottomMargin: contentMargin
        clip: true  // 确保内容不会超出圆角边界
    }
}
