pragma ComponentBehavior: Bound

import QtQuick
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Item {
    id: root
    objectName: "slicing.preview.layerPanel"

    SmoothUI.theme: Theme.of(root)

    width: 76
    visible: SlicingPreviewBridge.previewPanelVisible
             && !SlicingPreviewBridge.isLoading
             && SlicingPreviewBridge.totalLayers > 1
    readonly property int maxLayer: Math.max(0, SlicingPreviewBridge.totalLayers - 1)

    function displayLayer(layer) {
        return Math.round(layer) + 1
    }

    function rangeText() {
        var startIndex = SlicingPreviewBridge.layerRangeStart
        var endIndex = SlicingPreviewBridge.layerRangeEnd
        var stage = SlicingPreviewBridge.layerInfo(endIndex)
        if (startIndex === endIndex && stage.algorithmAudit)
            return stage.stageName
        var start = displayLayer(startIndex)
        var end = displayLayer(endIndex)
        return start === end ? qsTr("Layer %1").arg(start)
                             : qsTr("Layers %1–%2").arg(start).arg(end)
    }

    function millimeters(value) {
        return Number(value || 0).toFixed(2) + " mm"
    }

    component LayerInfoPopup: Rectangle {
        id: popup

        required property int layerIndex
        required property var layerData
        readonly property bool algorithmAudit: layerData.algorithmAudit === true

        width: algorithmAudit ? 320 : 228
        height: content.implicitHeight + 20
        radius: PrintWorkspaceStyle.controlRadius
        color: PrintWorkspaceStyle.surface(root.SmoothUI.dark)
        border.width: 1
        border.color: root.SmoothUI.theme.res.popupBorderColor
        clip: true

        Column {
            id: content
            anchors.centerIn: parent
            width: parent.width - 24
            spacing: 4

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                text: popup.algorithmAudit
                      ? popup.layerData.stageName
                      : qsTr("Layer %1 / %2 · Z %3")
                      .arg(root.displayLayer(popup.layerIndex))
                      .arg(SlicingPreviewBridge.totalLayers)
                      .arg(root.millimeters(popup.layerData.printZMm))
                color: root.SmoothUI.theme.res.textFillColorPrimary
                font: Typography.caption
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                maximumLineCount: popup.algorithmAudit ? 6 : 2
                elide: Text.ElideRight
                text: popup.algorithmAudit
                      ? popup.layerData.purpose + "\n" + popup.layerData.expected
                      : qsTr("Layer Height %1").arg(
                            root.millimeters(popup.layerData.layerHeightMm))
                color: root.SmoothUI.theme.res.textFillColorSecondary
                font: Typography.caption
            }
        }
    }

    Rectangle {
        id: rangeBadge
        z: 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: Math.min(260, layerText.implicitWidth + 18)
        height: 26
        radius: 8
        color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)
        border.width: 1
        border.color: root.SmoothUI.theme.res.popupBorderColor

        Label {
            id: layerText
            objectName: root.objectName + ".rangeText"
            anchors.centerIn: parent
            text: root.rangeText()
            elide: Text.ElideRight
            color: root.SmoothUI.theme.res.textFillColorPrimary
            font: Typography.caption
        }
    }

    VerticalPushRangeSlider {
        id: slider
        objectName: root.objectName + ".slider"
        z: 1
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 30
        anchors.bottom: parent.bottom
        minimumValue: 0
        maximumValue: root.maxLayer
        stepSize: 1
        startValue: SlicingPreviewBridge.layerRangeStart
        endValue: SlicingPreviewBridge.layerRangeEnd
        railColor: root.SmoothUI.theme.res.controlStrongFillColorDefault
        rangeColor: root.SmoothUI.theme.accentColor.defaultBrushFor(
                        root.SmoothUI.dark)
        onRangeMoved: function(start, end) {
            SlicingPreviewBridge.setLayerRange(start, end)
        }

        firstHandleContent: Component {
            Rectangle {
                id: firstHandle
                anchors.fill: parent
                readonly property int layerIndex: slider.startValue
                readonly property var layerData: SlicingPreviewBridge.layerInfo(layerIndex)
                radius: 7
                color: root.SmoothUI.theme.res.controlSolidFillColorDefault
                border.width: slider.firstPressed ? 2 : 1
                border.color: slider.firstHovered || slider.firstPressed
                              ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
                              : root.SmoothUI.theme.res.popupBorderColor

                Label {
                    anchors.centerIn: parent
                    text: root.displayLayer(parent.layerIndex)
                    color: root.SmoothUI.theme.res.textFillColorPrimary
                    font: Typography.caption
                }

                LayerInfoPopup {
                    z: 20
                    anchors.right: parent.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    visible: slider.firstPressed || slider.firstHovered
                    layerIndex: firstHandle.layerIndex
                    layerData: firstHandle.layerData
                }
            }
        }

        secondHandleContent: Component {
            Rectangle {
                id: secondHandle
                anchors.fill: parent
                readonly property int layerIndex: slider.endValue
                readonly property var layerData: SlicingPreviewBridge.layerInfo(layerIndex)
                radius: 7
                color: root.SmoothUI.theme.res.controlSolidFillColorDefault
                border.width: slider.secondPressed ? 2 : 1
                border.color: slider.secondHovered || slider.secondPressed
                              ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
                              : root.SmoothUI.theme.res.popupBorderColor

                Label {
                    anchors.centerIn: parent
                    text: root.displayLayer(parent.layerIndex)
                    color: root.SmoothUI.theme.res.textFillColorPrimary
                    font: Typography.caption
                }

                LayerInfoPopup {
                    z: 20
                    anchors.right: parent.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    visible: slider.secondPressed || slider.secondHovered
                    layerIndex: secondHandle.layerIndex
                    layerData: secondHandle.layerData
                }
            }
        }
    }
}
