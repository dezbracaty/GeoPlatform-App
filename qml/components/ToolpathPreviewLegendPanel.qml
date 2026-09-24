import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

PanelSurface {
    id: root

    SmoothUI.theme: Theme.of(root)

    objectName: "slicing.preview.inspector"
    readonly property bool codeTab: SlicingPreviewBridge.previewInspectorTab === "gcode"
    property var panelData: ({})
    property real maximumExpandedHeight: 560
    readonly property int totalPreviewSteps: Math.max(0, SlicingPreviewBridge.totalSteps)
    readonly property int maxPreviewStep: Math.max(
                                                0,
                                                totalPreviewSteps - 1)
    readonly property int shownPreviewStep: SlicingPreviewBridge.currentStep < 0
                                            ? maxPreviewStep
                                            : Math.min(
                                                  SlicingPreviewBridge.currentStep,
                                                  maxPreviewStep)
    readonly property int shownPreviewStepNumber: totalPreviewSteps > 0
                                                   ? shownPreviewStep + 1 : 0
    readonly property string viewType: SlicingPreviewBridge.previewPanelViewType
    readonly property var auditStage: SlicingPreviewBridge.layerInfo(
                                          SlicingPreviewBridge.currentLayer)
    readonly property var localizedViewTypes: {
        var source = SlicingPreviewBridge.previewPanelViewTypes || []
        var result = []
        for (var i = 0; i < source.length; ++i) {
            result.push({
                id: source[i].id,
                label: root.viewTypeLabel(source[i].id),
                layoutKind: source[i].layoutKind
            })
        }
        return result
    }

    function viewTypeLabel(id) {
        var labels = {
            summary: qsTr("Summary"), lineType: qsTr("Line Type"), filament: qsTr("Filament"),
            speed: qsTr("Speed"), actualSpeed: qsTr("Actual Speed"), acceleration: qsTr("Acceleration"),
            jerk: qsTr("Jerk"), layerHeight: qsTr("Layer Height"), lineWidth: qsTr("Line Width"),
            volumetricFlow: qsTr("Volumetric Flow"), actualVolumetricFlow: qsTr("Actual Volumetric Flow"),
            layerTime: qsTr("Layer Time"), layerTimeLog: qsTr("Layer Time (log)"),
            fanSpeed: qsTr("Fan Speed"), temperature: qsTr("Temperature"),
            pressureAdvance: qsTr("Pressure Advance")
        }
        return labels[id] || id
    }

    function refresh() {
        panelData = SlicingPreviewBridge.previewPanelData(viewType)
    }

    function setPreviewStep(position) {
        SlicingPreviewBridge.currentStep = Math.round(position)
    }

    width: 400
    height: SlicingPreviewBridge.previewPanelFolded
            ? 44 : Math.max(44, Math.min(maximumExpandedHeight, 620,
                                         shellColumn.implicitHeight + 16))
    surfaceRadius: PrintWorkspaceStyle.dialogRadius
    elevation: 2
    clip: true
    visible: SlicingPreviewBridge.previewPanelVisible
             && SlicingPreviewBridge.previewInspectorOpen
             && SlicingPreviewBridge.currentModelId >= 0

    Component.onCompleted: refresh()

    Connections {
        target: SlicingPreviewBridge
        function onPreviewPanelViewTypeChanged() { root.refresh() }
        function onPreviewLegendItemsChanged() { root.refresh() }
        function onStatisticsChanged() { root.refresh() }
        function onCurrentToolpathPreviewChanged() { root.refresh() }
        function onPathVisibilityChanged() { root.refresh() }
    }

    ColumnLayout {
        id: shellColumn
        anchors.fill: parent
        anchors.margins: 8
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 6
            IconButton {
                objectName: "slicing.preview.foldInspector"
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                icon.name: SlicingPreviewBridge.previewPanelFolded ? FluentIcons.graph_ChevronDown : FluentIcons.graph_ChevronUp
                icon.width: 12
                icon.height: 12
                onClicked: SlicingPreviewBridge.togglePreviewPanelFolded()
            }
            Button {
                objectName: "slicing.preview.typesTab"
                Layout.preferredHeight: 28
                text: qsTr("Toolpath Types")
                highlighted: !root.codeTab
                onClicked: SlicingPreviewBridge.previewInspectorTab = "types"
            }
            Button {
                objectName: "slicing.preview.gcodeTab"
                Layout.preferredHeight: 28
                text: "G-code"
                highlighted: root.codeTab
                onClicked: SlicingPreviewBridge.previewInspectorTab = "gcode"
            }
            Item { Layout.fillWidth: true }
            IconButton {
                objectName: "slicing.preview.closeInspector"
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                icon.name: FluentIcons.graph_ChromeClose
                icon.width: 12
                icon.height: 12
                onClicked: SlicingPreviewBridge.previewInspectorOpen = false
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: !SlicingPreviewBridge.previewPanelFolded && !root.codeTab
            Label { text: qsTr("Color by"); font: Typography.caption }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: viewSelector
                objectName: "slicing.preview.viewSelector"
                Layout.preferredWidth: 132
                Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                font: Typography.caption
                model: root.localizedViewTypes
                textRole: "label"
                valueRole: "id"
                currentIndex: {
                    for (var i = 0; i < model.length; ++i) {
                        if (model[i].id === root.viewType) return i
                    }
                    return -1
                }
                onActivated: function(index) {
                    if (index >= 0) {
                        SlicingPreviewBridge.setPreviewPanelViewType(model[index].id)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
            visible: !SlicingPreviewBridge.previewPanelFolded
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: auditColumn.implicitHeight + 20
            radius: PrintWorkspaceStyle.controlRadius
            color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)
            border.width: 1
            border.color: root.SmoothUI.theme.res.cardStrokeColorDefault
            visible: !SlicingPreviewBridge.previewPanelFolded
                     && !root.codeTab && root.auditStage.algorithmAudit === true

            ColumnLayout {
                id: auditColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 5

                Label {
                    Layout.fillWidth: true
                    text: root.auditStage.stageName || ""
                    color: root.SmoothUI.theme.res.textFillColorPrimary
                    font: Typography.bodyStrong
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Purpose: %1").arg(root.auditStage.purpose || "")
                    color: root.SmoothUI.theme.res.textFillColorSecondary
                    font: Typography.caption
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Expected: %1").arg(root.auditStage.expected || "")
                    color: root.SmoothUI.theme.res.textFillColorSecondary
                    font: Typography.caption
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Data: %1 lines · %2 records · %3 ms\n%4")
                          .arg(root.auditStage.lineCount || 0)
                          .arg(root.auditStage.recordCount || 0)
                          .arg(Number(root.auditStage.elapsedMs || 0).toFixed(2))
                          .arg(root.auditStage.metricsText || "")
                    color: root.SmoothUI.theme.res.textFillColorSecondary
                    font: Typography.caption
                    wrapMode: Text.Wrap
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: !SlicingPreviewBridge.previewPanelFolded

            Label {
                Layout.preferredWidth: 38
                text: qsTr("Play")
                color: root.SmoothUI.theme.res.textFillColorSecondary
                font: Typography.caption
            }

            IconButton {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: PrintWorkspaceStyle.controlRadius
                icon.name: SlicingPreviewBridge.isPlaying
                           ? FluentIcons.graph_Pause
                           : FluentIcons.graph_Play
                icon.width: 15
                icon.height: 15
                onClicked: {
                    if (SlicingPreviewBridge.isPlaying) {
                        SlicingPreviewBridge.pause()
                    } else {
                        SlicingPreviewBridge.play()
                    }
                }

                ToolTip {
                    delay: Theme.tooltipDelay
                    text: SlicingPreviewBridge.isPlaying
                          ? qsTr("Pause Toolpath Playback")
                          : qsTr("Play toolpaths for all visible layers together")
                    visible: parent.hovered
                }
            }

            IconButton {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: PrintWorkspaceStyle.controlRadius
                icon.name: FluentIcons.graph_Stop
                icon.width: 14
                icon.height: 14
                onClicked: SlicingPreviewBridge.stop()
            }

            IconButton {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: PrintWorkspaceStyle.controlRadius
                highlighted: SlicingPreviewBridge.isLooping
                icon.name: FluentIcons.graph_RepeatAll
                icon.width: 14
                icon.height: 14
                onClicked: SlicingPreviewBridge.isLooping = !SlicingPreviewBridge.isLooping
            }

            Button {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 32
                flat: true
                text: SlicingPreviewBridge.playbackSpeed.toFixed(1) + "x"
                font: Typography.caption
                onClicked: {
                    var speeds = [0.5, 1.0, 2.0, 5.0]
                    var next = 0
                    for (var index = 0; index < speeds.length; ++index) {
                        if (speeds[index] > SlicingPreviewBridge.playbackSpeed) {
                            next = index
                            break
                        }
                    }
                    SlicingPreviewBridge.playbackSpeed = speeds[next]
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                Layout.preferredWidth: 72
                text: qsTr("Layer %1/%2")
                      .arg(SlicingPreviewBridge.totalLayers > 0
                           ? SlicingPreviewBridge.currentLayer + 1 : 0)
                      .arg(Math.max(0, SlicingPreviewBridge.totalLayers))
                horizontalAlignment: Text.AlignRight
                color: root.SmoothUI.theme.res.textFillColorSecondary
                font: Typography.caption
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: !SlicingPreviewBridge.previewPanelFolded

            Slider {
                objectName: "slicing.preview.stepSlider"
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                from: 0
                to: root.maxPreviewStep
                stepSize: 1
                snapMode: Slider.SnapAlways
                live: true
                enabled: root.totalPreviewSteps > 1
                value: root.shownPreviewStep
                onValueChanged: {
                    if (pressed)
                        root.setPreviewStep(value)
                }
                onPressedChanged: {
                    if (!pressed)
                        root.setPreviewStep(value)
                }
            }

            Label {
                Layout.preferredWidth: 82
                text: qsTr("Visible steps %1/%2")
                      .arg(root.shownPreviewStepNumber)
                      .arg(root.totalPreviewSteps)
                horizontalAlignment: Text.AlignRight
                color: root.SmoothUI.theme.res.textFillColorSecondary
                font: Typography.caption
            }

            Button {
                Layout.preferredWidth: 50
                Layout.preferredHeight: 32
                text: qsTr("All")
                font: Typography.caption
                onClicked: SlicingPreviewBridge.currentStep = -1
            }
        }

        Loader {
            id: contentLoader
            objectName: "slicing.preview.typesContent"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: item ? Math.min(480, item.implicitHeight) : 0
            visible: !SlicingPreviewBridge.previewPanelFolded && !root.codeTab
            sourceComponent: {
                switch (root.panelData.layoutKind || "") {
                case "summary": return summaryComponent
                case "featureTable": return featureComponent
                case "filamentTable": return filamentComponent
                case "range": return rangeComponent
                default: return null
                }
            }
            onLoaded: {
                item.width = Qt.binding(function() { return contentLoader.width })
                item.height = Qt.binding(function() { return contentLoader.height })
                item.panelData = Qt.binding(function() { return root.panelData })
                item.viewType = Qt.binding(function() { return root.viewType })
            }
        }
        ToolpathGCodePanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 320
            Layout.minimumHeight: 0
            visible: !SlicingPreviewBridge.previewPanelFolded && root.codeTab
        }

    }

    Component { id: summaryComponent; ToolpathPreviewSummaryContent {} }
    Component { id: featureComponent; ToolpathPreviewFeatureTable {} }
    Component { id: filamentComponent; ToolpathPreviewFilamentTable {} }
    Component { id: rangeComponent; ToolpathPreviewRangeContent {} }
}
