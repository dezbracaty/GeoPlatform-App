import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

PanelSurface {
    id: root
    objectName: "print.configuration.panel"
    property bool shown: true
    property bool collapsed: false
    property bool positioned: false
    property int page: 0
    property int activeSlot: 0
    property Item boundsItem: parent
    property bool workspaceReady: false
    readonly property var machine: SliceSettingsBridge.selectedMachine
    readonly property var slots: SliceSettingsBridge.filamentSlots
    readonly property bool fiberCapable: slots.some(function (slot) {
        return slot.physicalToolRole === "continuous_fiber";
    })
    readonly property bool editingEnabled: !TaskStateNotifier.isBusy
    readonly property color secondaryText: SmoothUI.dark ? SmoothUI.theme.res.textFillColorSecondary : "#7b8795"
    readonly property color lineColor: SmoothUI.dark ? SmoothUI.theme.res.dividerStrokeColorDefault : "#e5e9ee"
    readonly property var machineChoices: {
        const choices = [];
        const models = SliceSettingsBridge.machineModels();
        for (let i = 0; i < models.length; ++i) {
            for (let j = 0; j < models[i].variants.length; ++j) {
                const variant = models[i].variants[j];
                choices.push({
                    modelId: models[i].id,
                    variantId: variant.id,
                    name: models[i].name + " · " + variant.nozzleDiameter + " mm"
                });
            }
        }
        return choices;
    }
    readonly property var visibleSlots: fiberCapable ? slots : (slots.length ? [slots[Math.min(activeSlot, slots.length - 1)]] : [])
    signal detailedSettingsRequested(int workspaceIndex)
    signal addModelRequested

    DBPropertyBridge {
        id: sceneModels
    }
    ColorDialog {
        id: modelColorDialog
        title: qsTr("Model Appearance Color")
        selectedColor: Qt.rgba(ModelPropertyBridge.diffuseColor.x, ModelPropertyBridge.diffuseColor.y, ModelPropertyBridge.diffuseColor.z, 1)
        onAccepted: if (root.editingEnabled && SelectionBridge.selectionCount === 1)
            ModelPropertyBridge.diffuseColor = Qt.vector3d(selectedColor.r, selectedColor.g, selectedColor.b)
    }
    width: Math.min(344, boundsItem ? Math.max(0, boundsItem.width - 32) : 344)
    height: collapsed ? 42 : Math.max(0, Math.min(649, boundsItem ? boundsItem.height * 0.726 : 649))
    visible: shown && workspaceReady
    radius: 9
    border.width: 1
    border.color: SmoothUI.dark ? SmoothUI.theme.res.surfaceStrokeColorDefault : "#b9c4d2"
    color: SmoothUI.dark ? SmoothUI.theme.res.cardBackgroundColor : "#ffffff"
    surfaceRadius: 9
    elevation: 4
    shadowOpacity: 0.16
    clip: false
    z: 900

    function constrainPosition() {
        if (!boundsItem || boundsItem.width <= 0 || boundsItem.height <= 0)
            return;
        if (!positioned) {
            x = Math.max(16, boundsItem.width - width - 16);
            y = Math.round(boundsItem.height * 0.141);
            positioned = true;
        }
        x = Math.max(16, Math.min(x, Math.max(16, boundsItem.width - width - 16)));
        y = Math.max(112, Math.min(y, Math.max(112, boundsItem.height - height - 96)));
    }
    function open() {
        shown = true;
        collapsed = false;
        Qt.callLater(constrainPosition);
    }
    function hide() {
        shown = false;
    }
    function showObjects() {
        open();
        page = 1;
    }
    function focusSetting(key) {
        open();
        page = 0;
        parameterEditor.focusSetting(key);
    }
    function indexOf(items, key, value) {
        for (let i = 0; i < items.length; ++i)
            if (String(items[i][key]) === String(value))
                return i;
        return -1;
    }
    function setVector(kind, axis, value) {
        if (SelectionBridge.selectionCount !== 1 || !editingEnabled)
            return;
        const v = kind === "position" ? ModelPropertyBridge.position : ModelPropertyBridge.rotation;
        const next = Qt.vector3d(axis === 0 ? value : v.x, axis === 1 ? value : v.y, axis === 2 ? value : v.z);
        if (kind === "position")
            ModelPropertyBridge.position = next;
        else
            ModelPropertyBridge.rotation = next;
    }
    onVisibleChanged: if (visible)
        Qt.callLater(constrainPosition)
    onHeightChanged: if (visible)
        Qt.callLater(constrainPosition)
    Connections {
        target: root.boundsItem
        function onWidthChanged() {
            if (root.visible)
                root.constrainPosition();
        }
        function onHeightChanged() {
            if (root.visible)
                root.constrainPosition();
        }
    }
    Connections {
        target: SliceSettingsBridge
        function onMachineSelectionChanged() {
            root.activeSlot = 0;
        }
    }

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        height: 41
        color: root.SmoothUI.dark ? root.SmoothUI.theme.res.solidBackgroundFillColorSecondary : "#f7f9fc"
        topLeftRadius: 8
        topRightRadius: 8
        bottomLeftRadius: root.collapsed ? 8 : 0
        bottomRightRadius: root.collapsed ? 8 : 0
        MouseArea {
            objectName: "print.configuration.drag"
            anchors.fill: parent
            cursorShape: Qt.OpenHandCursor
            drag.target: root
            drag.minimumX: 16
            drag.maximumX: root.boundsItem ? Math.max(16, root.boundsItem.width - root.width - 16) : 16
            drag.minimumY: 112
            drag.maximumY: root.boundsItem ? Math.max(112, root.boundsItem.height - root.height - 96) : 112
            onPressed: root.forceActiveFocus(Qt.MouseFocusReason)
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 9
            spacing: 8
            Icon {
                source: FluentIcons.graph_Equalizer
                Layout.preferredWidth: 15
                Layout.preferredHeight: 15
                color: root.secondaryText
            }
            Label {
                text: qsTr("Print Configuration")
                font.pixelSize: 12
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            IconButton {
                objectName: "print.configuration.collapse"
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                flat: true
                icon.name: root.collapsed ? FluentIcons.graph_ChevronRightSmall : FluentIcons.graph_ChevronDownSmall
                icon.width: 12
                icon.height: 12
                onClicked: {
                    root.forceActiveFocus();
                    root.collapsed = !root.collapsed;
                }
                ToolTip.visible: hovered
                ToolTip.text: root.collapsed ? qsTr("Expand panel") : qsTr("Collapse panel")
            }
            IconButton {
                objectName: "print.configuration.hide"
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                flat: true
                icon.name: FluentIcons.graph_ChromeClose
                icon.width: 12
                icon.height: 12
                onClicked: {
                    root.forceActiveFocus();
                    root.hide();
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Hide panel")
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            visible: !root.collapsed
            color: root.lineColor
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 1
        visible: !root.collapsed
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 10
            Layout.bottomMargin: 9
            spacing: 7
            SectionLabel {
                label: qsTr("Slicing printer")
                iconSource: FluentIcons.graph_Print
                actionText: qsTr("Edit preset")
                onTriggered: root.detailedSettingsRequested(2)
            }
            ComboBox {
                id: machineCombo
                objectName: "print.configuration.machine"
                Layout.fillWidth: true
                implicitHeight: 37
                font.pixelSize: 12
                font.weight: Font.DemiBold
                leftPadding: 34
                Icon {
                    x: 11
                    anchors.verticalCenter: parent.verticalCenter
                    width: 15
                    height: 15
                    source: FluentIcons.graph_Print
                    color: root.secondaryText
                }
                model: root.machineChoices
                textRole: "name"
                enabled: root.editingEnabled
                currentIndex: {
                    const selected = SliceSettingsBridge.selectedMachineVariantId;
                    const modelId = SliceSettingsBridge.selectedMachineModelId;
                    return root.machineChoices.findIndex(function (item) {
                        return item.variantId === selected && item.modelId === modelId;
                    });
                }
                onActivated: function (index) {
                    const choice = root.machineChoices[index];
                    Qt.callLater(function () {
                        if (root.editingEnabled)
                            SliceSettingsBridge.selectMachine(choice.modelId, choice.variantId);
                    });
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Build volume %1 × %2 × %3 mm").arg(Math.round(root.machine.width || 0)).arg(Math.round(root.machine.height || 0)).arg(Math.round(root.machine.printHeight || 0))
                font.pixelSize: 10
                color: root.secondaryText
                elide: Text.ElideRight
            }
        }
        Rule {}
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 9
            Layout.bottomMargin: 9
            spacing: 7
            SectionLabel {
                label: qsTr("Filaments")
                iconSource: FluentIcons.graph_Color
                actionText: qsTr("Edit")
                onTriggered: root.detailedSettingsRequested(1)
            }
            // Many tool/material configurations remain editable without letting
            // their cards displace every process parameter below the viewport.
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(108, slotRows.implicitHeight)
                contentWidth: availableWidth
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    id: slotRows
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: root.visibleSlots
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 30
                                radius: 5
                                color: root.SmoothUI.dark ? root.SmoothUI.theme.res.subtleFillColorSecondary : "#f0f4f8"
                                border.width: 1
                                border.color: root.lineColor
                                Label {
                                    anchors.centerIn: parent
                                    text: root.machine.filamentSlotsBoundToPhysicalTools || root.machine.physicalToolCount > 1 ? "T" + modelData.physicalToolIndex : String(modelData.number)
                                    font.pixelSize: 10
                                    color: root.secondaryText
                                }
                            }
                            ComboBox {
                                objectName: "print.configuration.filament." + modelData.number
                                Layout.fillWidth: true
                                implicitHeight: 34
                                font.pixelSize: 11
                                leftPadding: 31
                                Rectangle {
                                    x: 9
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 14
                                    height: 14
                                    radius: 4
                                    color: modelData.color || "#929aa3"
                                }
                                model: modelData.compatiblePresets || SliceSettingsBridge.compatibleFilamentPresets
                                textRole: "name"
                                enabled: root.editingEnabled
                                currentIndex: root.indexOf(model, "id", modelData.presetId)
                                onActivated: function (index) {
                                    const preset = model[index].id, slot = modelData.index;
                                    const modelId = SliceSettingsBridge.selectedMachineModelId;
                                    const variantId = SliceSettingsBridge.selectedMachineVariantId;
                                    Qt.callLater(function () {
                                        if (root.editingEnabled && modelId === SliceSettingsBridge.selectedMachineModelId && variantId === SliceSettingsBridge.selectedMachineVariantId)
                                            SliceSettingsBridge.selectFilamentPresetForSlot(slot, preset);
                                    });
                                }
                            }
                        }
                    }
                }
            }
            Flow {
                visible: !root.fiberCapable && root.slots.length > 1
                Layout.fillWidth: true
                spacing: 5
                Repeater {
                    model: root.slots
                    Button {
                        required property int index
                        required property var modelData
                        objectName: "print.configuration.slot." + index
                        width: 43
                        height: 23
                        padding: 2
                        font.pixelSize: 10
                        text: root.machine.filamentSlotsBoundToPhysicalTools ? "T" + modelData.physicalToolIndex : String(modelData.number)
                        highlighted: root.activeSlot === index
                        onClicked: root.activeSlot = index
                    }
                }
            }
        }
        Rule {}
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 9
            Layout.bottomMargin: 9
            spacing: 7
            SectionLabel {
                label: qsTr("Process preset")
                iconSource: FluentIcons.graph_Equalizer
                actionText: ""
                actionIcon: FluentIcons.graph_Save
                actionEnabled: false
                actionHint: qsTr("The preset library does not support saving process presets yet")
            }
            ComboBox {
                id: processCombo
                objectName: "print.configuration.process"
                Layout.fillWidth: true
                implicitHeight: 35
                font.pixelSize: 11
                leftPadding: 24
                Rectangle {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 5
                    height: 5
                    radius: 3
                    color: "#6b9ab9"
                }
                enabled: root.editingEnabled
                model: SliceSettingsBridge.compatibleProcessPresets
                textRole: "name"
                currentIndex: root.indexOf(model, "id", SliceSettingsBridge.selectedProcessPresetId)
                onActivated: function (index) {
                    const preset = model[index].id;
                    const modelId = SliceSettingsBridge.selectedMachineModelId;
                    const variantId = SliceSettingsBridge.selectedMachineVariantId;
                    Qt.callLater(function () {
                        if (root.editingEnabled && modelId === SliceSettingsBridge.selectedMachineModelId && variantId === SliceSettingsBridge.selectedMachineVariantId)
                            SliceSettingsBridge.selectProcessPreset(preset);
                    });
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 10
            Layout.preferredHeight: 35
            radius: 6
            color: root.SmoothUI.dark ? root.SmoothUI.theme.res.subtleFillColorSecondary : "#f0f3f7"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 3
                Repeater {
                    model: [qsTr("Process parameters"), qsTr("Model objects")]
                    Button {
                        id: contentTab
                        required property int index
                        required property string modelData
                        objectName: index === 0 ? "print.configuration.processTab" : "print.configuration.objectsTab"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.minimumWidth: 0
                        font.pixelSize: 12
                        font.weight: root.page === index ? Font.DemiBold : Font.Normal
                        text: modelData
                        flat: true
                        contentItem: RowLayout {
                            spacing: 6
                            Item {
                                Layout.fillWidth: true
                            }
                            Icon {
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                                source: contentTab.index === 0 ? FluentIcons.graph_Equalizer : FluentIcons.graph_Package
                                color: root.secondaryText
                            }
                            Label {
                                text: contentTab.modelData
                                font: contentTab.font
                                color: root.SmoothUI.dark ? root.SmoothUI.theme.res.textFillColorPrimary : (root.page === contentTab.index ? "#3b5269" : "#8a95a3")
                            }
                            Rectangle {
                                visible: contentTab.index === 1
                                Layout.preferredWidth: 15
                                Layout.preferredHeight: 15
                                radius: 3
                                color: root.SmoothUI.dark ? root.SmoothUI.theme.res.subtleFillColorSecondary : "#e7edf3"
                                Label {
                                    anchors.centerIn: parent
                                    text: sceneModels.models.length
                                    font.pixelSize: 9
                                    color: root.secondaryText
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }
                        background: Rectangle {
                            radius: 4
                            color: root.page === index ? (root.SmoothUI.dark ? root.SmoothUI.theme.res.cardBackgroundColor : "#ffffff") : "transparent"
                        }
                        onClicked: root.page = index
                    }
                }
            }
        }
        SlicingSettingsContent {
            id: parameterEditor
            objectName: "print.configuration.editor"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            visible: root.page === 0
            compactPanel: true
            embedded: true
            showParamKeys: false
            fiberAvailable: root.fiberCapable
        }
        ScrollView {
            id: objectsScroll
            objectName: "print.configuration.objects"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            visible: root.page === 1
            leftPadding: 16
            rightPadding: 16
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: objectsScroll.availableWidth
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Current plate")
                        font.pixelSize: 10
                        color: root.secondaryText
                        Layout.fillWidth: true
                    }
                    IconButton {
                        objectName: "print.configuration.addModel"
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 20
                        flat: true
                        icon.name: FluentIcons.graph_Add
                        icon.width: 12
                        icon.height: 12
                        enabled: root.editingEnabled
                        onClicked: root.addModelRequested()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Add Model")
                    }
                }
                Repeater {
                    model: sceneModels.models
                    Button {
                        id: objectButton
                        required property var modelData
                        objectName: "print.configuration.model." + modelData.id
                        Layout.fillWidth: true
                        implicitHeight: 35
                        font.pixelSize: 12
                        text: modelData.name
                        highlighted: String(SelectionBridge.selectedId) === modelData.id
                        background: Rectangle {
                            radius: 5
                            color: objectButton.highlighted ? Qt.alpha(root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark), 0.09) : "transparent"
                            border.width: 1
                            border.color: objectButton.highlighted ? "#d8eafa" : root.lineColor
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            Icon {
                                Layout.preferredWidth: 15
                                Layout.preferredHeight: 15
                                source: FluentIcons.graph_Package
                                color: root.secondaryText
                            }
                            Label {
                                Layout.fillWidth: true
                                text: objectButton.modelData.name
                                font: objectButton.font
                                elide: Text.ElideRight
                            }
                        }
                        enabled: root.editingEnabled && !FiberMaskBridge.active && !CutToolBridge.active
                        onClicked: ActionManager.triggerAction("selection.pick", {
                            modelId: modelData.id
                        })
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: SelectionBridge.selectionCount !== 1
                    text: SelectionBridge.hasSelection ? qsTr("Select one model to edit its properties") : qsTr("Select a model to view its properties")
                    font.pixelSize: 11
                    color: root.secondaryText
                    wrapMode: Text.Wrap
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: ModelPropertyBridge.hasSelectedModel && SelectionBridge.selectionCount === 1
                    enabled: root.editingEnabled && !FiberMaskBridge.active && !CutToolBridge.active
                    spacing: 10
                    Label {
                        text: qsTr("Dimensions")
                        font.pixelSize: 11
                        color: root.secondaryText
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: [ModelPropertyBridge.boundsSize.x, ModelPropertyBridge.boundsSize.y, ModelPropertyBridge.boundsSize.z]
                            Rectangle {
                                required property int index
                                required property real modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                radius: 5
                                color: root.SmoothUI.dark ? root.SmoothUI.theme.res.subtleFillColorSecondary : "#f5f7fa"
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 3
                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: ["X / mm", "Y / mm", "Z / mm"][index]
                                        font.pixelSize: 9
                                        color: root.secondaryText
                                    }
                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: ModelPropertyBridge.boundsValid ? Number(modelData).toFixed(1) : "—"
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                    Repeater {
                        model: ["position", "rotation"]
                        ColumnLayout {
                            required property string modelData
                            readonly property string kind: modelData
                            Layout.fillWidth: true
                            spacing: 7
                            Label {
                                text: parent.kind === "position" ? qsTr("Position") + " / mm" : qsTr("Rotation") + " / °"
                                font.pixelSize: 11
                                color: root.secondaryText
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: 3
                                    AxisField {
                                        required property int index
                                        fieldName: "print.configuration." + kind + "." + ["x", "y", "z"][index]
                                        label: ["X", "Y", "Z"][index]
                                        readonly property var vectorValue: kind === "position" ? ModelPropertyBridge.position : ModelPropertyBridge.rotation
                                        value: index === 0 ? vectorValue.x : index === 1 ? vectorValue.y : vectorValue.z
                                        onCommitted: function (value) {
                                            root.setVector(kind, index, value);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        text: qsTr("Model material")
                        font.pixelSize: 11
                        color: root.secondaryText
                    }
                    ComboBox {
                        objectName: "print.configuration.modelMaterial"
                        Layout.fillWidth: true
                        implicitHeight: 30
                        font.pixelSize: 11
                        model: [
                            {
                                id: 0,
                                name: qsTr("Use project default material")
                            }
                        ].concat(root.slots.map(function (slot) {
                            return {
                                id: slot.number,
                                name: String(slot.number) + " · " + slot.presetName
                            };
                        }))
                        textRole: "name"
                        currentIndex: root.indexOf(model, "id", ModelPropertyBridge.filamentSlot)
                        onActivated: function (index) {
                            if (model[index].id === 0)
                                ModelPropertyBridge.useProjectDefaultFilament();
                            else
                                ModelPropertyBridge.assignFilamentSlot(model[index].id);
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        Button {
                            objectName: "print.configuration.alignToBed"
                            Layout.fillWidth: true
                            implicitHeight: 30
                            font.pixelSize: 11
                            text: qsTr("Place on build plate")
                            onClicked: ActionManager.triggerAction("align.to_bed", {
                                preserveRelative: true
                            })
                        }
                        Button {
                            objectName: "print.configuration.scaleModel"
                            Layout.fillWidth: true
                            implicitHeight: 30
                            font.pixelSize: 11
                            text: qsTr("Scale model")
                            onClicked: ActionManager.triggerAction("model.scale", {
                                modelId: SelectionBridge.selectedId
                            })
                        }
                    }
                    Label {
                        text: qsTr("Scale") + "  " + [ModelPropertyBridge.scale.x, ModelPropertyBridge.scale.y, ModelPropertyBridge.scale.z].map(function (value) {
                            return Number(value).toFixed(2);
                        }).join(" / ")
                        font.pixelSize: 11
                        color: root.secondaryText
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Geometry") + " · " + qsTr("Parts") + " " + ModelPropertyBridge.partCount + " · " + qsTr("Vertices") + " " + ModelPropertyBridge.vertexCount + " · " + qsTr("Triangles") + " " + ModelPropertyBridge.triangleCount
                        font.pixelSize: 10
                        color: root.secondaryText
                        wrapMode: Text.Wrap
                    }
                    Button {
                        objectName: "print.configuration.modelColor"
                        text: qsTr("Change model appearance color")
                        Layout.fillWidth: true
                        implicitHeight: 30
                        font.pixelSize: 11
                        onClicked: modelColorDialog.open()
                    }
                    Button {
                        objectName: "print.configuration.fiberMask"
                        visible: root.fiberCapable
                        Layout.fillWidth: true
                        implicitHeight: 32
                        text: qsTr("Edit fiber mask")
                        onClicked: {
                            root.hide();
                            ActionManager.triggerAction("model.fiberMask", {});
                        }
                    }
                }
                Item {
                    Layout.preferredHeight: 8
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 35
            color: root.SmoothUI.dark ? root.SmoothUI.theme.res.solidBackgroundFillColorSecondary : "#f8fafc"
            bottomLeftRadius: 8
            bottomRightRadius: 8
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: root.lineColor
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 5
                Icon {
                    source: FluentIcons.graph_CheckMark
                    Layout.preferredWidth: 12
                    Layout.preferredHeight: 12
                    color: root.secondaryText
                }
                Label {
                    Layout.fillWidth: true
                    text: SliceSettingsBridge.modifiedSettingKeys.length > 0 ? qsTr("%1 settings modified").arg(SliceSettingsBridge.modifiedSettingKeys.length) + (SliceSettingsBridge.hasPendingReslice ? qsTr(" · Reslicing required") : "") : qsTr("Process preset unchanged")
                    font.pixelSize: 10
                    color: root.secondaryText
                    elide: Text.ElideRight
                }
                Button {
                    objectName: "print.configuration.reset"
                    visible: SliceSettingsBridge.modifiedSettingKeys.length > 0
                    text: qsTr("Restore changes")
                    flat: true
                    font.pixelSize: 10
                    implicitHeight: 28
                    enabled: root.editingEnabled
                    onClicked: SliceSettingsBridge.restoreProcessSettings()
                }
            }
        }
    }

    component Rule: Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: root.lineColor
    }
    component SectionLabel: RowLayout {
        property string label: ""
        property string iconSource: ""
        property string actionText: ""
        property string actionIcon: FluentIcons.graph_Edit
        property string actionHint: ""
        property bool actionEnabled: true
        signal triggered
        Layout.fillWidth: true
        spacing: 6
        Icon {
            source: iconSource
            Layout.preferredWidth: 13
            Layout.preferredHeight: 13
            color: root.secondaryText
        }
        Label {
            text: label
            Layout.fillWidth: true
            font.pixelSize: 11
            color: root.secondaryText
        }
        Item {
            Layout.preferredWidth: actionText.length > 0 ? detailButton.implicitWidth : 20
            Layout.preferredHeight: 20
            Button {
                id: detailButton
                objectName: "print.configuration.details." + label
                anchors.fill: parent
                enabled: actionEnabled && root.editingEnabled
                text: actionText
                icon.name: actionIcon
                icon.width: 12
                icon.height: 12
                flat: true
                padding: 0
                font.pixelSize: 11
                onClicked: triggered()
            }
            HoverHandler {
                id: disabledHint
            }
            ToolTip {
                visible: actionHint.length > 0 && disabledHint.hovered
                text: actionHint
            }
        }
    }
    component AxisField: Rectangle {
        id: axis
        property string label: ""
        property string fieldName: ""
        property real value: 0
        signal committed(real value)
        onValueChanged: if (!axisEditor.activeFocus)
            axisEditor.text = Number(value).toFixed(2)
        Layout.fillWidth: true
        Layout.preferredHeight: 29
        color: root.SmoothUI.dark ? root.SmoothUI.theme.res.controlFillColorDefault : "#ffffff"
        radius: 4
        border.width: 1
        border.color: root.lineColor
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 3
            spacing: 3
            Label {
                text: axis.label
                font.pixelSize: 10
                color: root.secondaryText
            }
            TextField {
                id: axisEditor
                objectName: axis.fieldName
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitHeight: 28
                implicitWidth: 60
                font.pixelSize: 11
                background.opacity: 0
                horizontalAlignment: Text.AlignRight
                selectByMouse: true
                text: Number(axis.value).toFixed(2)
                validator: DoubleValidator {
                    bottom: -1000000
                    top: 1000000
                    decimals: 3
                }
                onActiveFocusChanged: if (!activeFocus)
                    text = Number(axis.value).toFixed(2)
                onEditingFinished: {
                    const number = Number(text);
                    if (acceptableInput && isFinite(number))
                        axis.committed(number);
                    text = Number(axis.value).toFixed(2);
                }
            }
        }
    }
}
