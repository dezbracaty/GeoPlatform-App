import QtQuick
import GPlatform
import Render 1.0
import "../../components"

Item {
    id: centralFragment
    objectName: "realisticWorkspaceCentralPart"
    anchors.fill: parent

    property var threadRenderer: parent && parent.parent
                                 ? parent.parent.renderer : null
    readonly property int objectCount: dbBridge.meshCount

    DBPropertyBridge {
        id: dbBridge
    }

    FPSDisplay {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        z: 100
        fpsMonitor: centralFragment.threadRenderer
                    ? centralFragment.threadRenderer.fpsMonitor : null
    }
}
