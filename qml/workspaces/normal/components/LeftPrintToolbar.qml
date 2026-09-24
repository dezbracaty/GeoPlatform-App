import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0
import Render 1.0
import "../../../components"

ToolbarSurface {
    id: leftPrintToolbar
    width: PrintWorkspaceStyle.toolbarWidth
    height: toolbarColumn.implicitHeight + 16

    // 属性声明 - 从父组件传递
    property bool gridVisible: true
    property bool showBedBounds: true

    ScrollView {
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.topMargin: 8
        anchors.bottomMargin: 8

        Column {
            id: toolbarColumn
            width: leftPrintToolbar.width - 12
            spacing: 4

            // ==================== File 分组 ====================
            HoverExpandableButton {
                icon.name: FluentIcons.graph_Document
                tooltip: qsTr("File")
                expandItems: [
                    {
                        icon: FluentIcons.graph_Import,
                        text: qsTr("Import…"),
                        tooltip: qsTr("Import model or GCode file"),
                        enabled: true,
                        action: function() {
                            Global.starter.chooseModel({});
                        }
                    },
                    {
                        icon: FluentIcons.graph_Export,
                        text: qsTr("Export STL"),
                        tooltip: qsTr("Export selected model as STL"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            console.log("Export STL clicked");
                        }
                    },
                    {
                        icon: FluentIcons.graph_Save,
                        text: qsTr("Save Project"),
                        tooltip: qsTr("Save current project"),
                        enabled: true,
                        action: function() {
                            console.log("Save Project clicked");
                        }
                    },
                    {
                        icon: FluentIcons.graph_FolderOpen,
                        text: qsTr("Open Project"),
                        tooltip: qsTr("Open existing project"),
                        enabled: true,
                        action: function() {
                            console.log("Open Project clicked");
                        }
                    }
                ]
                expandDirection: 0
                hoverDelay: 300
                hideDelay: 100
            }

            // 分隔线
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            // ==================== View 分组 ====================
            HoverExpandableButton {
                icon.name: FluentIcons.graph_View
                tooltip: qsTr("View")
                expandItems: [
                    {
                        icon: FluentIcons.graph_Refresh,
                        text: qsTr("Reset Camera"),
                        tooltip: qsTr("Reset camera to default position"),
                        enabled: true,
                        action: function() {
                            ActionManager.triggerAction("camera.reset", {});
                        }
                    },
                    {
                        icon: FluentIcons.graph_Zoom,
                        text: qsTr("Zoom to Fit"),
                        tooltip: qsTr("Zoom camera to fit all objects (0)"),
                        enabled: true,
                        action: function() {
                            ActionManager.triggerAction("camera.reset", {});
                        }
                    },
                    {
                        icon: FluentIcons.graph_GridView,
                        text: gridVisible ? qsTr("Hide Grid") : qsTr("Show Grid"),
                        tooltip: qsTr("Show or hide print bed grid"),
                        enabled: true,
                        action: function() {
                            gridVisible = !gridVisible;
                            ActionManager.triggerAction("printbed.update.grid", {
                                "showGrid": gridVisible
                            });
                        }
                    },
                    {
                        icon: FluentIcons.graph_Stop,
                        text: showBedBounds ? qsTr("Hide Bounds") : qsTr("Show Bounds"),
                        tooltip: qsTr("Show or hide print volume"),
                        enabled: true,
                        action: function() {
                            showBedBounds = !showBedBounds;
                            ActionManager.triggerAction("printbed.update.bounds", {
                                "showBounds": showBedBounds
                            });
                        }
                    }
                ]
                expandDirection: 0
                hoverDelay: 300
                hideDelay: 100
            }

            // 分隔线
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            // ==================== Transform 分组 ====================
            HoverExpandableButton {
                icon.name: FluentIcons.graph_Move
                enabled: SelectionBridge.hasSelection
                tooltip: qsTr("Transform")
                expandItems: [
                    {
                        icon: FluentIcons.graph_Move,
                        text: qsTr("Move"),
                        tooltip: qsTr("Move selected model (Alt+M)"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            if (SelectionBridge.hasTranslateWidget) {
                                ActionManager.triggerAction("model.remove_translate_widget", {});
                            } else {
                                ActionManager.triggerAction("model.translate", {
                                    "modelId": SelectionBridge.selectedId
                                });
                            }
                        }
                    },
                    {
                        icon: FluentIcons.graph_Rotate,
                        text: qsTr("Rotate"),
                        tooltip: qsTr("Rotate selected model (Alt+R)"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            if (SelectionBridge.hasOrientationWidget) {
                                ActionManager.triggerAction("model.remove_orientation_widget", {});
                            } else {
                                ActionManager.triggerAction("model.rotate", {
                                    "modelId": SelectionBridge.selectedId
                                });
                            }
                        }
                    },
                    {
                        icon: FluentIcons.graph_ZoomIn,
                        text: qsTr("Scale"),
                        tooltip: qsTr("Scale selected model (Alt+S)"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            if (SelectionBridge.hasScaleWidget) {
                                ActionManager.triggerAction("model.remove_scale_widget", {});
                            } else {
                                ActionManager.triggerAction("model.scale", {
                                    "modelId": SelectionBridge.selectedId
                                });
                            }
                        }
                    },
                    {
                        icon: FluentIcons.graph_FlickDown,
                        text: qsTr("Mirror X"),
                        tooltip: qsTr("Mirror along X axis"),
                        shortcut: "Ctrl+Shift+X",
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            ActionManager.triggerAction("model.mirror.x", {
                                "keepOriginal": false,
                                "applyToAllSelected": true
                            });
                        }
                    },
                    {
                        icon: FluentIcons.graph_FlickDown,
                        text: qsTr("Mirror Y"),
                        tooltip: qsTr("Mirror along Y axis"),
                        shortcut: "Ctrl+Shift+Y",
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            ActionManager.triggerAction("model.mirror.y", {
                                "keepOriginal": false,
                                "applyToAllSelected": true
                            });
                        }
                    },
                    {
                        icon: FluentIcons.graph_FlickDown,
                        text: qsTr("Mirror Z"),
                        tooltip: qsTr("Mirror along Z axis"),
                        shortcut: "Ctrl+Shift+Z",
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            ActionManager.triggerAction("model.mirror.z", {
                                "keepOriginal": false,
                                "applyToAllSelected": true
                            });
                        }
                    },
                    {
                        icon: FluentIcons.graph_Down,
                        text: qsTr("Align to Bed"),
                        tooltip: qsTr("Move selected models to bed surface (Z=0)"),
                        shortcut: "Ctrl+Shift+B",
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            var success = ActionManager.triggerAction("align.to_bed", {
                                preserveRelative: true
                            });
                            if (!success) {
                                console.error("Failed to execute align.to_bed action");
                            }
                        }
                    },
                    {
                        icon: FluentIcons.graph_AlignCenter,
                        text: qsTr("Center to Bed"),
                        tooltip: qsTr("Move selected models to bed center"),
                        shortcut: "Ctrl+Shift+C",
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            var success = ActionManager.triggerAction("align.center", {});
                            if (!success) {
                                console.error("Failed to execute align.center action");
                            }
                        }
                    },
                    {
                        icon: FluentIcons.graph_Move,
                        text: qsTr("Smart Place"),
                        tooltip: qsTr("Align to bed and center automatically"),
                        shortcut: "Ctrl+Shift+S",
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            var success = ActionManager.triggerAction("align.smart", {});
                            if (!success) {
                                console.error("Failed to execute align.smart action");
                            }
                        }
                    }
                ]
                expandDirection: 0
                hoverDelay: 300
                hideDelay: 100
            }

            // 分隔线
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            // ==================== Edit 分组 ====================
            HoverExpandableButton {
                objectName: "print.toolbar.edit"
                icon.name: FluentIcons.graph_Edit
                enabled: SelectionBridge.hasSelection
                tooltip: qsTr("Edit")
                expandItems: [
                    {
                        icon: FluentIcons.graph_Repair,
                        text: qsTr("Auto Repair"),
                        tooltip: qsTr("Automatically repair mesh errors"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            console.log("Auto Repair clicked");
                        }
                    },
                    {
                        icon: FluentIcons.graph_Processing,
                        text: qsTr("Simplify Mesh"),
                        tooltip: qsTr("Reduce mesh polygon count"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            console.log("Simplify Mesh clicked");
                        }
                    },
                    {
                        icon: FluentIcons.graph_Scan,
                        text: qsTr("Analyze Mesh"),
                        tooltip: qsTr("Analyze mesh statistics"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            console.log("Analyze Mesh clicked");
                        }
                    },
                    {
                        icon: FluentIcons.graph_Ruler,
                        text: qsTr("Measure"),
                        tooltip: qsTr("Measure dimensions"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            console.log("Measure clicked");
                        }
                    },
                    {
                        targetName: "print.toolbar.edit.cut",
                        icon: FluentIcons.graph_Cut,
                        text: qsTr("Cut"),
                        tooltip: qsTr("Cut model (C)"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            CutToolBridge.enterSelectedModel()
                        }
                    },
                    {
                        targetName: "print.toolbar.edit.paint",
                        icon: FluentIcons.graph_Color,
                        text: qsTr("Paint Model"),
                        tooltip: qsTr("Paint colors on model surfaces"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            ModelColorPaintBridge.enterSelectedModel()
                        }
                    },
                    {
                        icon: FluentIcons.graph_Split20,
                        text: qsTr("Split to Objects"),
                        tooltip: qsTr("Split model into objects"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            console.log("Split to Objects clicked");
                        }
                    },
                    {
                        icon: FluentIcons.graph_Split20,
                        text: qsTr("Split to Parts"),
                        tooltip: qsTr("Split model into parts"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            console.log("Split to Parts clicked");
                        }
                    },
                    {
                        icon: FluentIcons.graph_MergeCall,
                        text: qsTr("Merge"),
                        tooltip: qsTr("Merge selected models"),
                        enabled: SelectionBridge.selectionCount > 1,
                        action: function() {
                            console.log("Merge clicked");
                        }
                    }
                ]
                expandDirection: 0
                hoverDelay: 300
                hideDelay: 100
            }

            // 分隔线
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.res.dividerStrokeColorDefault
            }

            // ==================== Slice 分组 ====================
            HoverExpandableButton {
                id: sliceGroupButton

                readonly property string sliceSelectedText: qsTr("Slice Selected")
                readonly property string sliceSelectedTooltip: qsTr("Slice selected models")
                readonly property string sliceAllText: qsTr("Slice All")
                readonly property string sliceAllTooltip: qsTr("All models on the build plate")

                icon.name: FluentIcons.graph_MapLayers
                enabled: true  // 始终可用（包含切片所有按钮）
                tooltip: qsTr("Slice")
                expandItems: [
                    {
                        icon: FluentIcons.graph_MapLayers,
                        text: sliceGroupButton.sliceSelectedText,
                        tooltip: sliceGroupButton.sliceSelectedTooltip,
                        shortcut: "Ctrl+Shift+L",
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            if (SelectionBridge.hasSelection) {
                                ActionManager.triggerAction("model.slice", {
                                    "modelId": SelectionBridge.selectedId
                                });
                            }
                        }
                    },
                    {
                        icon: FluentIcons.graph_MapLayers,
                        text: sliceGroupButton.sliceAllText,
                        tooltip: sliceGroupButton.sliceAllTooltip,
                        enabled: true,  // 始终可用
                        action: function() {
                            ActionManager.triggerAction("model.sliceAll", {});
                        }
                    },
                    {
                        icon: FluentIcons.graph_Down,
                        text: qsTr("Lay Flat"),
                        tooltip: qsTr("Lay model flat on bed (F)"),
                        enabled: SelectionBridge.selectionCount === 1,
                        action: function() {
                            ActionManager.triggerAction("model.flatten", {
                                "modelId": SelectionBridge.selectedId
                            });
                        }
                    },
                    {
                        icon: FluentIcons.graph_Leaf,
                        text: qsTr("Auto Support"),
                        tooltip: qsTr("Automatically generate support structures"),
                        enabled: SelectionBridge.hasSelection,
                        action: function() {
                            ActionManager.triggerAction("support.autoGenerate", {});
                        }
                    },
                    {
                        icon: FluentIcons.graph_Color,
                        text: qsTr("Print Settings"),
                        tooltip: qsTr("Open print settings panel"),
                        enabled: true,
                        action: function() {
                            console.log("Print Settings clicked");
                        }
                    }
                ]
                expandDirection: 0
                hoverDelay: 300
                hideDelay: 100
            }
        }
    }

}
