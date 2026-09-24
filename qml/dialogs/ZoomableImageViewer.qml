import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SmoothUI.Controls
import SmoothUI.impl

Item {
    id: control

    property alias source: image.source
    property real minZoom: 0.1
    property real maxZoom: 10
    property real zoomStep: 0.2
    property bool enablePanGesture: true
    property bool enableZoomGesture: true
    property string label: ""

    // Current zoom level
    property real currentZoom: 1.0

    // Background
    Rectangle {
        anchors.fill: parent
        color: Colors.black
        opacity: 0.05
    }

    // Label at top
    Label {
        id: titleLabel
        text: control.label
        anchors {
            top: parent.top
            horizontalCenter: parent.horizontalCenter
            topMargin: 10
        }
        font.pixelSize: 12
        opacity: 0.8
        visible: control.label !== ""
    }

    // Scroll view for panning
    Flickable {
        id: flickable
        anchors {
            fill: parent
            topMargin: titleLabel.visible ? 35 : 10
            leftMargin: 10
            rightMargin: 10
            bottomMargin: 40
        }
        clip: true

        contentWidth: imageContainer.width
        contentHeight: imageContainer.height

        boundsBehavior: Flickable.StopAtBounds

        // Image container with transformations
        Item {
            id: imageContainer
            width: Math.max(flickable.width, image.width * control.currentZoom)
            height: Math.max(flickable.height, image.height * control.currentZoom)

            Image {
                id: image
                anchors.centerIn: parent
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
                cache: true

                // Apply zoom transformation
                scale: control.currentZoom

                // Ensure image fits initially
                onStatusChanged: {
                    if (status === Image.Ready) {
                        // Calculate initial zoom to fit image in view
                        var widthRatio = flickable.width / sourceSize.width
                        var heightRatio = flickable.height / sourceSize.height
                        control.currentZoom = Math.min(widthRatio, heightRatio, 1.0)

                        // Center the view
                        flickable.contentX = (flickable.contentWidth - flickable.width) / 2
                        flickable.contentY = (flickable.contentHeight - flickable.height) / 2
                    }
                }
            }
        }

        // Mouse wheel zoom
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            enabled: control.enableZoomGesture

            onWheel: function(wheel) {
                var zoomFactor = wheel.angleDelta.y > 0 ? 1 + control.zoomStep : 1 - control.zoomStep
                var newZoom = Math.max(control.minZoom, Math.min(control.maxZoom, control.currentZoom * zoomFactor))

                // Calculate zoom point relative to content
                var mouseX = wheel.x + flickable.contentX
                var mouseY = wheel.y + flickable.contentY

                // Calculate new content position after zoom
                var xRatio = mouseX / flickable.contentWidth
                var yRatio = mouseY / flickable.contentHeight

                control.currentZoom = newZoom

                // Adjust content position to keep mouse point stable
                flickable.contentX = xRatio * flickable.contentWidth - wheel.x
                flickable.contentY = yRatio * flickable.contentHeight - wheel.y
            }
        }

        // Pinch to zoom (for touch devices)
        PinchArea {
            anchors.fill: parent
            enabled: control.enableZoomGesture

            property real initialZoom

            onPinchStarted: {
                initialZoom = control.currentZoom
            }

            onPinchUpdated: function(pinch) {
                var newZoom = initialZoom * pinch.scale
                control.currentZoom = Math.max(control.minZoom, Math.min(control.maxZoom, newZoom))

                // Center on pinch point
                var centerX = pinch.center.x + flickable.contentX
                var centerY = pinch.center.y + flickable.contentY

                flickable.contentX = centerX - flickable.width / 2
                flickable.contentY = centerY - flickable.height / 2
            }
        }
    }

    // Zoom controls at bottom
    RowLayout {
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            bottomMargin: 10
        }
        spacing: 10

        // Zoom out button
        IconButton {
            text: "-"
            onClicked: {
                control.currentZoom = Math.max(control.minZoom, control.currentZoom - control.zoomStep)
            }
        }

        // Zoom level display
        Label {
            text: Math.round(control.currentZoom * 100) + "%"
            Layout.preferredWidth: 60
            horizontalAlignment: Text.AlignHCenter
        }

        // Zoom in button
        IconButton {
            text: "+"
            onClicked: {
                control.currentZoom = Math.min(control.maxZoom, control.currentZoom + control.zoomStep)
            }
        }

        // Reset zoom button
        Button {
            text: qsTr("Fit")
            onClicked: {
                // Reset to fit view
                var widthRatio = flickable.width / image.sourceSize.width
                var heightRatio = flickable.height / image.sourceSize.height
                control.currentZoom = Math.min(widthRatio, heightRatio, 1.0)

                // Center the view
                flickable.contentX = (flickable.contentWidth - flickable.width) / 2
                flickable.contentY = (flickable.contentHeight - flickable.height) / 2
            }
        }

        // 1:1 button
        Button {
            text: "1:1"
            onClicked: {
                control.currentZoom = 1.0

                // Center the view
                flickable.contentX = (flickable.contentWidth - flickable.width) / 2
                flickable.contentY = (flickable.contentHeight - flickable.height) / 2
            }
        }
    }
}
