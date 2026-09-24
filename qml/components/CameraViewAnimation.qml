import QtQuick
import GPlatform 1.0

// The view navigator drives the camera-property animation path.
Item {
    id: root
    property bool active: true
    property var viewport: null
    property int duration: 220
    readonly property bool running: animation.running
    property vector3d targetPosition
    property vector3d targetUp

    function animateToDirection(direction) {
        if (!active)
            return
        animation.stop()
        if (viewport)
            viewport.activateView()
        var pose = CameraBridge.calculateTargetPose(direction)
        if (pose.position !== undefined) {
            targetPosition = pose.position
            targetUp = pose.up
            animation.restart()
        }
    }

    function stop() { animation.stop() }
    onActiveChanged: if (!active) animation.stop()
    Component.onDestruction: animation.stop()

    ParallelAnimation {
        id: animation
        NumberAnimation { target: CameraBridge; property: "cameraX"; to: root.targetPosition.x; duration: root.duration; easing.type: Easing.InOutCubic }
        NumberAnimation { target: CameraBridge; property: "cameraY"; to: root.targetPosition.y; duration: root.duration; easing.type: Easing.InOutCubic }
        NumberAnimation { target: CameraBridge; property: "cameraZ"; to: root.targetPosition.z; duration: root.duration; easing.type: Easing.InOutCubic }
        NumberAnimation { target: CameraBridge; property: "upX"; to: root.targetUp.x; duration: root.duration; easing.type: Easing.InOutCubic }
        NumberAnimation { target: CameraBridge; property: "upY"; to: root.targetUp.y; duration: root.duration; easing.type: Easing.InOutCubic }
        NumberAnimation { target: CameraBridge; property: "upZ"; to: root.targetUp.z; duration: root.duration; easing.type: Easing.InOutCubic }
        onFinished: CameraBridge.syncOrbitParameters()
    }

    Connections {
        target: CameraBridge
        // Stop before another frame can write into a newly bound camera.
        function onCameraConnected() { animation.stop() }
    }
}
