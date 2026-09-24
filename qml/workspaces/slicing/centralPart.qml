import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0
import Render 1.0
import "../../components"

Item {
    id: centralFragment

    property int exportFileRequestId: 0
    property string exportFormatId: ""

    function toLocalFilePath(fileUrl) {
        var filePath = fileUrl.toString();

        if (filePath.startsWith("file:///")) {
            filePath = filePath.substring(7);
        } else if (filePath.startsWith("file://")) {
            filePath = filePath.substring(7);
        }

        if (filePath.startsWith("localhost/")) {
            filePath = filePath.substring(10);
        }

        try {
            filePath = decodeURIComponent(filePath);
        } catch (error) {
            console.warn("Unable to decode export file URL:", fileUrl, error)
        }

        if (Qt.platform.os === "windows" && filePath.match(/^\/[A-Za-z]:\//)) {
            filePath = filePath.substring(1);
        }

        return filePath;
    }

    function requestPrintOutputExport(formatId) {
        if (!SlicingPreviewBridge.canExportPrintOutput(formatId)) {
            return
        }
        exportFormatId = formatId
        exportFileRequestId = FilePicker.saveFile({
            title: formatId === "gcode-3mf"
                   ? qsTr("Export Slicing Job")
                   : qsTr("Export G-code"),
            nameFilters: SlicingPreviewBridge.printOutputNameFilters(formatId),
            currentFile: SlicingPreviewBridge.defaultExportPrintOutputFileUrl(formatId),
            parentWindow: centralFragment.Window.window
        })
    }

    function buildManualSupportSettingsParams() {
        var params = {
            paintMode: manualSupportPaintMode,
            brushShape: manualSupportBrushShape,
            brushSizeMm: manualSupportBrushSizeMm,
            densityPercent: manualSupportDensityPercent,
            smartFill: manualSupportSmartFill,
            clipToOverhang: manualSupportClipToOverhang,
            smartFillAngleDeg: manualSupportSmartFillAngleDeg,
            gapArea: manualSupportGapArea,
            sectionViewRatio: manualSupportSectionViewRatio
        }

        var rawSupportAngle = Number(SliceSettingsBridge.getSetting("support_angle"));
        if (isFinite(rawSupportAngle)) {
            params.overhangThresholdDeg = Math.max(0, Math.min(90, Math.round(rawSupportAngle)));
        }

        return params;
    }

    function triggerManualSupportSettingsUpdate() {
        if (!manualSupportActive) {
            return;
        }
        SlicingHandlerBridge.manualSupportApplySettings(buildManualSupportSettingsParams());
    }

    function pushManualSupportDebugOptions() {
        if (manualSupportActive) {
            SlicingHandlerBridge.manualSupportSetDebugVisibility({
                showActor: manualDebugShowActor,
                showStrokePolyline: manualDebugShowStrokePolyline,
                showStrokePoints: manualDebugShowStrokePoints,
                showPickNormals: manualDebugShowPickNormals,
                showProjectedRing: manualDebugShowProjectedRing,
                showMergedSelectionSurfaceOutline: manualDebugShowMergedSelectionSurfaceOutline,
                showContactPatch: manualDebugShowContactPatch
            });
        }
        SlicingHandlerBridge.showDebugData = manualDebugShowActor;
    }

    function startManualSupportSession(resetSession) {
        var targetModelId = SlicingPreviewBridge.currentModelId;
        if (targetModelId < 0) {
            NotificationManager.showWarning(
                qsTr("Manual Supports"),
                qsTr("There are no editable models to slice"));
            return false;
        }

        var params = {
            modelId: targetModelId,
            switchEnvironment: true,
            returnEnvironment: "slicing",
            resetSession: resetSession,
            showActor: manualDebugShowActor,
            showStrokePolyline: manualDebugShowStrokePolyline,
            showStrokePoints: manualDebugShowStrokePoints,
            showPickNormals: manualDebugShowPickNormals,
            showProjectedRing: manualDebugShowProjectedRing,
            showMergedSelectionSurfaceOutline: manualDebugShowMergedSelectionSurfaceOutline,
            showContactPatch: manualDebugShowContactPatch
        }

        var manualSettings = buildManualSupportSettingsParams()
        for (var key in manualSettings) {
            params[key] = manualSettings[key]
        }

        return ActionManager.triggerAction("support.manual.enter", params);
    }

    function stopManualSupportSession() {
        SlicingHandlerBridge.manualSupportCommitStroke();
        ActionManager.triggerAction("support.manual.leave", {});
    }

    function triggerResliceCurrentPreview() {
        if (!SlicingPreviewBridge.resliceAvailable) {
            return;
        }
        ActionManager.triggerAction("slicing.reslice", {});
    }

    function setManualSupportEnabled(enabled) {
        if (manualSupportActive === enabled) {
            return;
        }
        if (enabled) {
            if (startManualSupportSession(true)) {
                manualSupportPanelVisible = true;
            }
        } else {
            stopManualSupportSession();
        }
    }

    function placeManualSupportDebugPanel() {
        if (!manualSupportDebugPanel || !manualSupportDebugPanelVisible) {
            return;
        }

        var margin = 16;
        var gap = 12;
        var panelW = manualSupportDebugPanel.width;
        var panelH = manualSupportDebugPanel.height;
        var hostW = centralFragment.width;
        var hostH = centralFragment.height;

        function clampValue(value, minValue, maxValue) {
            return Math.max(minValue, Math.min(value, maxValue));
        }

        var anchorX = manualSupportPanel.visible ? manualSupportPanel.x : hostW - panelW - margin;
        var anchorY = manualSupportPanel.visible ? manualSupportPanel.y : Math.max(70, Math.min((hostH - panelH) / 2, hostH - panelH - margin));
        var anchorW = manualSupportPanel.visible ? manualSupportPanel.width : 0;
        var anchorH = manualSupportPanel.visible ? manualSupportPanel.height : 0;

        var alignedY = clampValue(anchorY, margin, hostH - panelH - margin);
        var alignedX = clampValue(anchorX + anchorW - panelW, margin, hostW - panelW - margin);

        // 优先放在手动支撑面板右侧
        var rightX = anchorX + anchorW + gap;
        if (rightX + panelW <= hostW - margin) {
            manualSupportDebugPanel.x = rightX;
            manualSupportDebugPanel.y = alignedY;
            return;
        }

        // 其次放在左侧
        var leftX = anchorX - gap - panelW;
        if (leftX >= margin) {
            manualSupportDebugPanel.x = leftX;
            manualSupportDebugPanel.y = alignedY;
            return;
        }

        // 再尝试放在下方
        var belowY = anchorY + anchorH + gap;
        if (belowY + panelH <= hostH - margin) {
            manualSupportDebugPanel.x = alignedX;
            manualSupportDebugPanel.y = belowY;
            return;
        }

        // 再尝试放在上方
        var aboveY = anchorY - gap - panelH;
        if (aboveY >= margin) {
            manualSupportDebugPanel.x = alignedX;
            manualSupportDebugPanel.y = aboveY;
            return;
        }

        // 兜底：放到可视区域内最靠右位置
        manualSupportDebugPanel.x = clampValue(hostW - panelW - margin, margin, hostW - panelW - margin);
        manualSupportDebugPanel.y = alignedY;
    }

    function adjustManualSupportBrushSize(delta) {
        var nextSize = Math.max(1, Math.min(30, manualSupportBrushSizeMm + delta));
        if (nextSize === manualSupportBrushSizeMm) {
            return;
        }
        SlicingHandlerBridge.manualSupportBrushSizeMm = nextSize;
        triggerManualSupportSettingsUpdate();
    }

    property bool cellHighlightActive: false  // 跟踪面片高亮功能是否激活

    property string currentMode: "Slicing Mode"
    property string estimatedTime: "00:00"
    property bool gridVisible: true  // Default to true for print bed grid
    property bool hasSelection: SelectionBridge.hasSelection
    readonly property bool pathLegendVisible: SlicingPreviewBridge.previewPanelVisible && SlicingPreviewBridge.previewInspectorOpen
    property bool slicingSettingsVisible: false
    readonly property bool manualSupportActive: SlicingHandlerBridge.manualSupportActive
    property bool manualSupportPanelVisible: false
    property bool manualSupportDebugPanelVisible: false

    // Orca-style manual support settings (support angle sourced from the active libslicer config)
    readonly property string manualSupportPaintMode: SlicingHandlerBridge.manualSupportPaintMode // enforce / block / erase
    readonly property string manualSupportBrushShape: SlicingHandlerBridge.manualSupportBrushShape // circle / sphere / fill / gap
    readonly property real manualSupportBrushSizeMm: SlicingHandlerBridge.manualSupportBrushSizeMm
    readonly property real manualSupportDensityPercent: SlicingHandlerBridge.manualSupportDensityPercent
    readonly property bool manualSupportSmartFill: SlicingHandlerBridge.manualSupportSmartFill
    readonly property bool manualSupportClipToOverhang: SlicingHandlerBridge.manualSupportClipToOverhang
    readonly property real manualSupportSmartFillAngleDeg: SlicingHandlerBridge.manualSupportSmartFillAngleDeg
    readonly property real manualSupportGapArea: SlicingHandlerBridge.manualSupportGapArea
    readonly property real manualSupportSectionViewRatio: SlicingHandlerBridge.manualSupportSectionViewRatio

    // Manual support debug visibility flags (single source of truth: SlicingHandlerBridge)
    readonly property bool manualDebugShowActor: SlicingHandlerBridge.manualDebugShowActor
    readonly property bool manualDebugShowStrokePolyline: SlicingHandlerBridge.manualDebugShowStrokePolyline
    readonly property bool manualDebugShowStrokePoints: SlicingHandlerBridge.manualDebugShowStrokePoints
    readonly property bool manualDebugShowPickNormals: SlicingHandlerBridge.manualDebugShowPickNormals
    readonly property bool manualDebugShowProjectedRing: SlicingHandlerBridge.manualDebugShowProjectedRing
    readonly property bool manualDebugShowMergedSelectionSurfaceOutline: SlicingHandlerBridge.manualDebugShowMergedSelectionSurfaceOutline
    readonly property bool manualDebugShowContactPatch: SlicingHandlerBridge.manualDebugShowContactPatch

    // Print status properties
    property bool isPrinting: false
    property real layerHeight: 0.2
    property real modelHeight: 50.0
    property real modelZoom: 100
    property int objectCount: 0
    property real printProgress: 0.0  // 0-100
    property string printTime: "00:00"
    property bool propertiesExpanded: false

    property bool rotationWidgetActive: false  // 跟踪旋转widget是否真正激活

    // Selected model for properties display
    property var selectedModel: SelectionBridge.selectedModel
    property string selectedTool: "select"
    property bool showBedBounds: true
    property bool snapEnabled: true
    property int stepSize: 1

    // 通过父级访问 ThreadRendererQmlItem
    // centralPart.qml 在 Loader 中，Loader 的 parent 是 Item，Item 的 parent 是 centralDock
    property var threadRenderer: parent && parent.parent ? parent.parent.renderer : null

    Connections {
        target: FilePicker

        function onAccepted(requestId, urls) {
            if (requestId !== centralFragment.exportFileRequestId) {
                return
            }
            centralFragment.exportFileRequestId = 0
            if (urls && urls.length > 0) {
                SlicingPreviewBridge.exportCurrentPrintOutputAs(
                            centralFragment.exportFormatId,
                            centralFragment.toLocalFilePath(urls[0]))
            }
            centralFragment.exportFormatId = ""
        }

        function onRejected(requestId) {
            if (requestId === centralFragment.exportFileRequestId) {
                centralFragment.exportFileRequestId = 0
                centralFragment.exportFormatId = ""
            }
        }
    }

    anchors.fill: parent

    Component.onCompleted: {
        SlicingHandlerBridge.showDebugData = manualDebugShowActor;
        if (manualSupportActive) {
            manualSupportPanelVisible = true;
        }
    }

    Component.onDestruction: {
        if (manualSupportActive) {
            stopManualSupportSession();
        }
    }

    Connections {
        function onManualSupportActiveChanged() {
            if (SlicingHandlerBridge.manualSupportActive) {
                manualSupportPanelVisible = true;
            } else {
                manualSupportPanelVisible = false;
                manualSupportDebugPanelVisible = false;
            }
        }

        target: SlicingHandlerBridge
    }

    Connections {
        function onSettingChanged(key, value) {
            if (key !== "support_angle" || !manualSupportActive) {
                return;
            }
            triggerManualSupportSettingsUpdate();
        }

        target: SliceSettingsBridge
    }

    CameraViewAnimation {
        id: cameraAnimator
        active: threadRenderer ? threadRenderer.renderActive : false
        viewport: threadRenderer
    }

    // The Qt overlay hit-tests locally and delegates transitions to QML.
    OrcaViewNavigator {
        objectName: "print.viewport.slicing.navigator"
        anchors.left: leftPrintToolbar.right
        anchors.leftMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16 + PrintWorkspaceStyle.globalCommandBarHeight + 12
        z: 20
        visible: threadRenderer ? threadRenderer.renderActive : false
        viewDirection: Qt.vector3d(CameraBridge.cameraX - CameraBridge.focalX,
                                   CameraBridge.cameraY - CameraBridge.focalY,
                                   CameraBridge.cameraZ - CameraBridge.focalZ)
        viewUp: Qt.vector3d(CameraBridge.upX, CameraBridge.upY, CameraBridge.upZ)
        onDirectionActivated: function(direction) { cameraAnimator.animateToDirection(direction) }
    }

    FPSDisplay {
        id: fpsDisplay
        anchors.margins: 16
        anchors.right: parent.right
        anchors.top: parent.top
        z: 100
        fpsMonitor: threadRenderer ? threadRenderer.fpsMonitor : null
    }

    // Left Print Toolbar（简化版，移除打印床控制）
    DraggablePanel {
        id: leftPrintToolbar
        property real toolbarContentHeight: 0
        showDragHandle: false

        height: toolbarContentHeight + 16

        width: PrintWorkspaceStyle.toolbarWidth
        x: 16
        y: Math.max(70, (parent.height - height) / 2)  // Center but not too close to top

        contentItem: Component {
            Item {
                anchors.fill: parent

                Column {
                    id: toolbarColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 4

                    Component.onCompleted: {
                        leftPrintToolbar.toolbarContentHeight = implicitHeight
                    }

                    onImplicitHeightChanged: {
                        leftPrintToolbar.toolbarContentHeight = implicitHeight
                    }

                    // View Controls
                    Label {
                        visible: false
                        color: Theme.res.textFillColorSecondary
                        font: Typography.caption
                        height: 0
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("View")
                        verticalAlignment: Text.AlignVCenter
                        width: parent.width
                    }

                    // Reset camera button
                    IconButton {
                        icon.height: 20
                        icon.name: FluentIcons.graph_Refresh
                        icon.width: 20
                        implicitHeight: PrintWorkspaceStyle.toolbarButtonSize
                        implicitWidth: PrintWorkspaceStyle.toolbarButtonSize
                        radius: 6

                        onClicked: {
                            ActionManager.triggerAction("camera.reset", {});
                        }

                        ToolTip {
                            delay: Theme.tooltipDelay
                            text: qsTr("Reset Camera")
                            visible: parent.hovered
                        }
                    }

                    // Separator
                    Rectangle {
                        color: Theme.res.dividerStrokeColorDefault
                        height: 1
                        width: parent.width
                    }

                    // Panel Controls
                    Label {
                        visible: false
                        color: Theme.res.textFillColorSecondary
                        font: Typography.caption
                        height: 0
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Panels")
                        verticalAlignment: Text.AlignVCenter
                        width: parent.width
                    }

                    IconButton {
                        icon.height: 20
                        icon.name: FluentIcons.graph_Color
                        icon.width: 20
                        enabled: true
                        highlighted: pathLegendVisible
                        implicitHeight: PrintWorkspaceStyle.toolbarButtonSize
                        implicitWidth: PrintWorkspaceStyle.toolbarButtonSize
                        radius: 6

                        onClicked: {
                            SlicingPreviewBridge.togglePreviewInspector()
                        }

                        ToolTip {
                            delay: Theme.tooltipDelay
                            text: pathLegendVisible ? qsTr("Hide Preview Panel") : qsTr("Show Preview Panel")
                            visible: parent.hovered
                        }
                    }

                    IconButton {
                        icon.height: 20
                        icon.name: FluentIcons.graph_Settings
                        icon.width: 20
                        highlighted: slicingSettingsVisible
                        implicitHeight: PrintWorkspaceStyle.toolbarButtonSize
                        implicitWidth: PrintWorkspaceStyle.toolbarButtonSize
                        radius: 6

                        onClicked: {
                            slicingSettingsVisible = !slicingSettingsVisible
                        }

                        ToolTip {
                            delay: Theme.tooltipDelay
                            text: slicingSettingsVisible ? qsTr("Hide Slice Settings") : qsTr("Show Slice Settings")
                            visible: parent.hovered
                        }
                    }

                    Rectangle {
                        color: Theme.res.dividerStrokeColorDefault
                        height: 1
                        width: parent.width
                    }

                    Label {
                        visible: false
                        color: Theme.res.textFillColorSecondary
                        font: Typography.caption
                        height: 0
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Manual")
                        verticalAlignment: Text.AlignVCenter
                        width: parent.width
                    }

                    IconButton {
                        icon.height: 20
                        icon.name: FluentIcons.graph_Print
                        icon.width: 20
                        enabled: !TaskStateNotifier.isBusy
                                 && (manualSupportActive || SlicingPreviewBridge.currentModelId >= 0)
                        highlighted: manualSupportActive
                        implicitHeight: PrintWorkspaceStyle.toolbarButtonSize
                        implicitWidth: PrintWorkspaceStyle.toolbarButtonSize
                        radius: 6

                        onClicked: {
                            setManualSupportEnabled(!manualSupportActive);
                        }

                        ToolTip {
                            delay: Theme.tooltipDelay
                            text: manualSupportActive
                                  ? qsTr("Disable Manual Supports")
                                  : (SlicingPreviewBridge.currentModelId >= 0
                                     ? qsTr("Enable Manual Supports")
                                     : qsTr("There are no editable models to slice"))
                            visible: parent.hovered
                        }
                    }

                    IconButton {
                        icon.height: 20
                        icon.name: FluentIcons.graph_Settings
                        icon.width: 20
                        enabled: manualSupportActive
                        highlighted: manualSupportDebugPanelVisible
                        implicitHeight: manualSupportActive ? PrintWorkspaceStyle.toolbarButtonSize : 0
                        implicitWidth: manualSupportActive ? PrintWorkspaceStyle.toolbarButtonSize : 0
                        radius: 6
                        visible: manualSupportActive

                        onClicked: {
                            manualSupportDebugPanelVisible = !manualSupportDebugPanelVisible;
                            if (manualSupportDebugPanelVisible && !manualSupportPanelVisible) {
                                manualSupportPanelVisible = true;
                            }
                            if (manualSupportDebugPanelVisible) {
                                Qt.callLater(placeManualSupportDebugPanel);
                            }
                        }

                        ToolTip {
                            delay: Theme.tooltipDelay
                            text: manualSupportDebugPanelVisible ? qsTr("Hide Manual Support Debug Panel") : qsTr("Show Manual Support Debug Panel")
                            visible: parent.hovered
                        }
                    }
                }
            }
        }
    }

    // Slicing Settings Panel（切片参数专用面板）
    DraggablePanel {
        id: slicingSettingsPanel

        property int contentWidthHint: 860

        contentMargin: 0
        dragHandleHeight: 14
        height: Math.min(parent.height - 90, 680)
        maxX: parent.width - width - 16
        maxY: parent.height - height - 16
        minX: 16
        minY: 16
        showDragHandle: true
        visible: slicingSettingsVisible
        width: Math.min(parent.width - 32, Math.max(620, Math.min(1120, contentWidthHint)))
        x: parent.width - width - 16
        y: Math.max(16, Math.min((parent.height - height) / 2, parent.height - height - 16))
        z: 99

        contentItem: Component {
            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 8

                        Icon {
                            color: Theme.res.textFillColorPrimary
                            height: 16
                            source: FluentIcons.graph_Print
                            width: 16
                        }

                        Label {
                            Layout.fillWidth: true
                            color: Theme.res.textFillColorPrimary
                            font: Typography.bodyStrong
                            text: qsTr("Slice Settings")
                        }

                        IconButton {
                            Layout.preferredHeight: 28
                            Layout.preferredWidth: 28
                            icon.height: 14
                            icon.name: FluentIcons.graph_ChromeClose
                            icon.width: 14
                            radius: 4

                            onClicked: {
                                slicingSettingsVisible = false
                            }

                            ToolTip {
                                delay: Theme.tooltipDelay
                                text: qsTr("Close")
                                visible: parent.hovered
                            }
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        color: Theme.res.dividerStrokeColorDefault
                        height: 1
                        width: parent.width
                    }
                }

                SlicingSettingsContent {
                    id: slicingSettingsContent
                    Layout.fillHeight: true
                    Layout.fillWidth: true

                    Component.onCompleted: {
                        slicingSettingsPanel.contentWidthHint = preferredWidth
                    }

                    onPreferredWidthChanged: {
                        slicingSettingsPanel.contentWidthHint = preferredWidth
                    }
                }
            }
        }
    }

    // Manual Support Settings Panel（参考 OrcaSlicer 手动支撑）
    DraggablePanel {
        id: manualSupportPanel

        contentMargin: 0
        dragHandleHeight: 14
        height: Math.min(parent.height - 100, 520)
        maxX: parent.width - width - 16
        maxY: parent.height - height - 16
        minX: 16
        minY: 16
        showDragHandle: true
        visible: manualSupportPanelVisible
        width: 340
        x: parent.width - width - 16
        y: Math.max(70, Math.min((parent.height - height) / 2, parent.height - height - 16))
        z: 101

        contentItem: Component {
            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            color: Theme.res.textFillColorPrimary
                            font: Typography.bodyStrong
                            text: qsTr("Manual Supports")
                        }

                        IconButton {
                            Layout.preferredHeight: 28
                            Layout.preferredWidth: 28
                            icon.height: 14
                            icon.name: FluentIcons.graph_ChromeClose
                            icon.width: 14
                            radius: 4

                            onClicked: {
                                manualSupportPanelVisible = false;
                            }

                            ToolTip {
                                delay: Theme.tooltipDelay
                                text: qsTr("Close")
                                visible: parent.hovered
                            }
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        color: Theme.res.dividerStrokeColorDefault
                        height: 1
                        width: parent.width
                    }
                }

                ScrollView {
                    id: manualSupportScrollView

                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    clip: true
                    leftPadding: 12
                    rightPadding: 12
                    topPadding: 10
                    bottomPadding: 10
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        id: manualSupportContent
                        spacing: 10
                        width: manualSupportScrollView.availableWidth

                        Label {
                            color: Theme.res.textFillColorSecondary
                            font: Typography.caption
                            text: qsTr("Tool Type")
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: [
                                    { key: "circle", tip: qsTr("Circle"), icon: FluentIcons.graph_CircleRing, enabled: true },
                                    { key: "square", tip: qsTr("Square"), icon: FluentIcons.graph_Stop, enabled: true },
                                    { key: "triangle", tip: qsTr("Triangle"), icon: FluentIcons.graph_IncidentTriangle, enabled: true },
                                    { key: "sphere", tip: qsTr("Sphere"), icon: FluentIcons.graph_CircleShapeSolid, enabled: true },
                                    { key: "fill", tip: qsTr("Fill"), icon: FluentIcons.graph_InkingToolFill, enabled: true },
                                    { key: "gap", tip: qsTr("Gap Fill"), icon: FluentIcons.graph_HolePunchOff, enabled: true }
                                ]
                                IconButton {
                                    Layout.preferredHeight: 34
                                    Layout.preferredWidth: 34
                                    enabled: modelData.enabled
                                    highlighted: manualSupportBrushShape === modelData.key
                                    icon.height: 16
                                    icon.name: modelData.icon
                                    icon.width: 16
                                    radius: 6

                                    onClicked: {
                                        if (!modelData.enabled) {
                                            return;
                                        }
                                        SlicingHandlerBridge.manualSupportBrushShape = modelData.key;
                                        SlicingHandlerBridge.manualSupportSmartFill = modelData.key === "fill";
                                        triggerManualSupportSettingsUpdate();
                                    }

                                    ToolTip {
                                        delay: Theme.tooltipDelay
                                        text: modelData.tip
                                        visible: parent.hovered
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Repeater {
                                model: [
                                    { key: "enforce", tip: qsTr("Enforce Support"), icon: FluentIcons.graph_CheckMark },
                                    { key: "block", tip: qsTr("Block Support"), icon: FluentIcons.graph_Stop },
                                    { key: "erase", tip: qsTr("Erase"), icon: FluentIcons.graph_Delete }
                                ]
                                IconButton {
                                    Layout.preferredHeight: 34
                                    Layout.preferredWidth: 34
                                    highlighted: manualSupportPaintMode === modelData.key
                                    icon.height: 16
                                    icon.name: modelData.icon
                                    icon.width: 16
                                    radius: 6

                                    onClicked: {
                                        SlicingHandlerBridge.manualSupportPaintMode = modelData.key;
                                        triggerManualSupportSettingsUpdate();
                                    }

                                    ToolTip {
                                        delay: Theme.tooltipDelay
                                        text: modelData.tip
                                        visible: parent.hovered
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.res.dividerStrokeColorDefault
                            height: 1
                        }

                        CheckBox {
                            checked: manualSupportClipToOverhang
                            font: Typography.caption
                            text: qsTr("On overhangs only")

                            onCheckedChanged: {
                                SlicingHandlerBridge.manualSupportClipToOverhang = checked;
                                triggerManualSupportSettingsUpdate();
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.res.dividerStrokeColorDefault
                            height: 1
                        }

                        Label {
                            color: Theme.res.textFillColorSecondary
                            font: Typography.caption
                            text: qsTr("Tool Parameters")
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: manualSupportBrushShape === "circle" ||
                                     manualSupportBrushShape === "square" ||
                                     manualSupportBrushShape === "triangle" ||
                                     manualSupportBrushShape === "sphere"

                            Label {
                                color: Theme.res.textFillColorSecondary
                                font: Typography.caption
                                text: qsTr("Pen size  %1 mm").arg(manualSupportBrushSizeMm.toFixed(1))
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: 1
                                stepSize: 0.5
                                to: 30
                                value: manualSupportBrushSizeMm

                                onMoved: {
                                    SlicingHandlerBridge.manualSupportBrushSizeMm = value;
                                }
                                onPressedChanged: {
                                    if (!pressed) {
                                        triggerManualSupportSettingsUpdate();
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: manualSupportBrushShape === "fill"

                            Label {
                                color: Theme.res.textFillColorSecondary
                                font: Typography.caption
                                text: qsTr("Smart fill angle  %1°").arg(Math.round(manualSupportSmartFillAngleDeg))
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: 0
                                stepSize: 1
                                to: 90
                                value: manualSupportSmartFillAngleDeg

                                onMoved: {
                                    SlicingHandlerBridge.manualSupportSmartFillAngleDeg = value;
                                }
                                onPressedChanged: {
                                    if (!pressed) {
                                        triggerManualSupportSettingsUpdate();
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: manualSupportBrushShape === "gap"

                            Label {
                                color: Theme.res.textFillColorSecondary
                                font: Typography.caption
                                text: qsTr("Gap area  %1").arg(manualSupportGapArea.toFixed(2))
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: 0
                                stepSize: 0.05
                                to: 5
                                value: manualSupportGapArea

                                onMoved: {
                                    SlicingHandlerBridge.manualSupportGapArea = value;
                                }
                                onPressedChanged: {
                                    if (!pressed) {
                                        triggerManualSupportSettingsUpdate();
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.res.dividerStrokeColorDefault
                            height: 1
                        }

                        Label {
                            color: Theme.res.textFillColorSecondary
                            font: Typography.caption
                            text: qsTr("Section View")
                        }

                        Label {
                            color: Theme.res.textFillColorSecondary
                            font: Typography.caption
                            text: qsTr("Section view  %1%").arg(Math.round(manualSupportSectionViewRatio * 100))
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            stepSize: 0.01
                            to: 1
                            value: manualSupportSectionViewRatio

                            onMoved: {
                                SlicingHandlerBridge.manualSupportSectionViewRatio = value;
                            }
                            onPressedChanged: {
                                if (!pressed) {
                                    triggerManualSupportSettingsUpdate();
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Item {
                                Layout.fillWidth: true
                            }

                            Button {
                                font: Typography.caption
                                text: qsTr("Reset direction")

                                onClicked: {
                                    SlicingHandlerBridge.manualSupportSectionViewRatio = 0;
                                    triggerManualSupportSettingsUpdate();
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.res.dividerStrokeColorDefault
                            height: 1
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Button {
                                Layout.fillWidth: true
                                enabled: manualSupportActive
                                font: Typography.caption
                                text: qsTr("Commit")

                                onClicked: {
                                    SlicingHandlerBridge.manualSupportCommitStroke();
                                }
                            }

                            Button {
                                Layout.fillWidth: true
                                enabled: manualSupportActive
                                font: Typography.caption
                                text: qsTr("Undo")

                                onClicked: {
                                    ActionManager.triggerAction("edit.undo", {});
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Button {
                                Layout.fillWidth: true
                                enabled: manualSupportActive
                                font: Typography.caption
                                text: qsTr("Erase all painting")

                                onClicked: {
                                    SlicingHandlerBridge.manualSupportClearStrokes();
                                }
                            }

                            Button {
                                Layout.fillWidth: true
                                enabled: manualSupportActive && manualSupportBrushShape === "gap"
                                font: Typography.caption
                                text: qsTr("Perform")
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            color: Theme.res.textFillColorSecondary
                            font: Typography.caption
                            text: qsTr("Left: Enforce  Right: Block  Shift+Left: Erase")
                            wrapMode: Text.WordWrap
                        }

                        Item {
                            Layout.preferredHeight: 2
                        }
                    }
                }
            }
        }
    }

    // Manual Support Debug Data Panel
    DraggablePanel {
        id: manualSupportDebugPanel

        contentMargin: 0
        dragHandleHeight: 14
        height: Math.min(parent.height - 160, 420)
        maxX: parent.width - width - 16
        maxY: parent.height - height - 16
        minX: 16
        minY: 16
        showDragHandle: true
        visible: manualSupportDebugPanelVisible
        width: 300
        x: Math.min(parent.width - width - 16, manualSupportPanel.x + manualSupportPanel.width + 12)
        y: Math.max(16, Math.min(manualSupportPanel.y, parent.height - height - 16))
        z: 100

        onVisibleChanged: {
            if (visible) {
                Qt.callLater(placeManualSupportDebugPanel);
            }
        }

        contentItem: Component {
            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 8

                        Icon {
                            color: Theme.res.textFillColorPrimary
                            height: 16
                            source: FluentIcons.graph_Settings
                            width: 16
                        }
                        Label {
                            Layout.fillWidth: true
                            color: Theme.res.textFillColorPrimary
                            font: Typography.bodyStrong
                            text: qsTr("Manual Support Debug Data")
                        }
                        IconButton {
                            Layout.preferredHeight: 28
                            Layout.preferredWidth: 28
                            icon.height: 14
                            icon.name: FluentIcons.graph_ChromeClose
                            icon.width: 14
                            radius: 4

                            onClicked: {
                                manualSupportDebugPanelVisible = false;
                            }
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        color: Theme.res.dividerStrokeColorDefault
                        height: 1
                        width: parent.width
                    }
                }

                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.topMargin: 10
                    spacing: 4

                    CheckBox {
                        checked: manualDebugShowActor
                        font: Typography.caption
                        text: qsTr("Show Debug Actor")

                        onCheckedChanged: {
                            SlicingHandlerBridge.manualDebugShowActor = checked;
                            pushManualSupportDebugOptions();
                        }
                    }
                    CheckBox {
                        checked: manualDebugShowStrokePolyline
                        font: Typography.caption
                        text: qsTr("Show Candidate Surfaces (preview layer)")

                        onCheckedChanged: {
                            SlicingHandlerBridge.manualDebugShowStrokePolyline = checked;
                            pushManualSupportDebugOptions();
                        }
                    }
                    CheckBox {
                        checked: manualDebugShowStrokePoints
                        font: Typography.caption
                        text: qsTr("Show Pick-line Layer (ray/normal)")

                        onCheckedChanged: {
                            SlicingHandlerBridge.manualDebugShowStrokePoints = checked;
                            pushManualSupportDebugOptions();
                        }
                    }
                    CheckBox {
                        checked: manualDebugShowPickNormals
                        font: Typography.caption
                        text: qsTr("Pick-line layer synchronization (compatibility)")
                        visible: false

                        onCheckedChanged: {
                            SlicingHandlerBridge.manualDebugShowPickNormals = checked;
                            pushManualSupportDebugOptions();
                        }
                    }
                    CheckBox {
                        checked: manualDebugShowProjectedRing
                        font: Typography.caption
                        text: qsTr("Show Projection Ring")

                        onCheckedChanged: {
                            SlicingHandlerBridge.manualDebugShowProjectedRing = checked;
                            pushManualSupportDebugOptions();
                        }
                    }
                    CheckBox {
                        checked: manualDebugShowMergedSelectionSurfaceOutline
                        font: Typography.caption
                        text: qsTr("Show Final Surface Contours")

                        onCheckedChanged: {
                            SlicingHandlerBridge.manualDebugShowMergedSelectionSurfaceOutline = checked;
                            pushManualSupportDebugOptions();
                        }
                    }
                    CheckBox {
                        checked: manualDebugShowContactPatch
                        font: Typography.caption
                        text: qsTr("Show Model Contact Surface")

                        onCheckedChanged: {
                            SlicingHandlerBridge.manualDebugShowContactPatch = checked;
                            pushManualSupportDebugOptions();
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        color: Theme.res.dividerStrokeColorDefault
                        height: 1
                    }

                    CheckBox {
                        checked: SlicingHandlerBridge.showOriginalModel
                        font: Typography.caption
                        text: qsTr("Show Original Model")

                        onCheckedChanged: {
                            SlicingHandlerBridge.showOriginalModel = checked;
                        }
                    }
                    CheckBox {
                        checked: SlicingHandlerBridge.showSlicingModel
                        font: Typography.caption
                        text: qsTr("Show Slicing Model")

                        onCheckedChanged: {
                            SlicingHandlerBridge.showSlicingModel = checked;
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Button {
                            Layout.fillWidth: true
                            font: Typography.caption
                            text: qsTr("Show All")

                            onClicked: {
                                SlicingHandlerBridge.manualDebugShowActor = true;
                                SlicingHandlerBridge.manualDebugShowStrokePolyline = true;
                                SlicingHandlerBridge.manualDebugShowStrokePoints = true;
                                SlicingHandlerBridge.manualDebugShowPickNormals = true;
                                SlicingHandlerBridge.manualDebugShowProjectedRing = true;
                                SlicingHandlerBridge.manualDebugShowMergedSelectionSurfaceOutline = true;
                                SlicingHandlerBridge.manualDebugShowContactPatch = true;
                                pushManualSupportDebugOptions();
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            font: Typography.caption
                            text: qsTr("Hide All")

                            onClicked: {
                                SlicingHandlerBridge.manualDebugShowActor = false;
                                SlicingHandlerBridge.manualDebugShowStrokePolyline = false;
                                SlicingHandlerBridge.manualDebugShowStrokePoints = false;
                                SlicingHandlerBridge.manualDebugShowPickNormals = false;
                                SlicingHandlerBridge.manualDebugShowProjectedRing = false;
                                SlicingHandlerBridge.manualDebugShowMergedSelectionSurfaceOutline = false;
                                SlicingHandlerBridge.manualDebugShowContactPatch = false;
                                pushManualSupportDebugOptions();
                            }
                        }
                    }
                }
            }
        }
    }

    ToolpathPreviewLegendPanel {
        // The shared Toolpath/G-code panel sits on the right, beside
        // the vertical layer scrubber. The Dock workspace tabs occupy the
        // upper-left corner of the viewport.
        id: toolpathPreviewLegendPanel
        anchors.right: parent.right
        anchors.rightMargin: 108
        anchors.top: parent.top
        anchors.topMargin: 122
        maximumExpandedHeight: Math.max(44,
                                        parent.height
                                        - 16 - PrintWorkspaceStyle.globalCommandBarHeight - 12 - y)
        z: 99
    }

    ToolpathPreviewLayerSlider {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 78
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16 + PrintWorkspaceStyle.globalCommandBarHeight + 12
        z: 99
    }
}
