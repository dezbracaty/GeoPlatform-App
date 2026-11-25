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
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Slice Settings")
    property bool shown: true
    property bool collapsed: false
    property bool filamentsCollapsed: false
    property string materialError: ""
    property bool positioned: false
    property int page: 0
    property string localModelId: ""
    readonly property string localPartId: SelectionBridge.activePartId
    readonly property string localTargetName: ModelPropertyBridge.displayName
    readonly property bool modifierSelected: localPartId.length > 0
    readonly property string selectedModelId: SelectionBridge.activeInstanceId
    readonly property bool editingLocalProcess: page === 1 && localModelId.length > 0
        && localModelId === selectedModelId
    onSelectedModelIdChanged: localModelId = ""
    onPageChanged: if (page === 0) localModelId = ""
    property Item boundsItem: parent
    property bool workspaceReady: false
    readonly property var machine: SliceSettingsBridge.selectedMachine
    readonly property var slots: SliceSettingsBridge.filamentSlots
    readonly property bool fiberCapable: slots.some(function (slot) {
        return slot.physicalToolRole === "continuous_fiber"
            || slot.physicalToolRole === "thermoplastic+continuous_fiber";
    })
    readonly property bool editingEnabled: !TaskStateNotifier.isBusy
    readonly property color secondaryText: SmoothUI.dark ? SmoothUI.theme.res.textFillColorSecondary : "#7b8795"
    readonly property color lineColor: SmoothUI.dark ? SmoothUI.theme.res.dividerStrokeColorDefault : "#e5e9ee"
    readonly property var machineChoices: {
        const choices = [];
        const models = SliceSettingsBridge.machineModels();
        if (!root.machine) return choices;
        for (let i = 0; i < models.length; ++i) {
            for (let j = 0; j < models[i].variants.length; ++j) {
                const variant = models[i].variants[j];
                choices.push({
                    modelId: models[i].id,
                    variantId: variant.id,
                    presetId: variant.printerPresetId,
                    name: variant.userPreset || variant.projectPreset ? variant.name : models[i].name + " · " + variant.nozzleDiameter + " mm"
                });
            }
        }
        return choices;
    }
    readonly property int sectionActionSize: 26
    readonly property int sectionActionIconSize: 12
    readonly property int sectionInset: PrintWorkspaceStyle.space16
    readonly property var materialControls: SliceSettingsBridge.filamentSlotControls
    signal detailedSettingsRequested(int workspaceIndex, int slotIndex, var restoreFocus)
    signal presetSaveRequested(string domain, int filamentSlot, string sourceName, bool duplicate)
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
    Menu {
        id: presetContextMenu
        property string presetDomain: ""
        property int filamentSlot: -1
        property string presetId: ""

        MenuItem {
            objectName: "print.configuration.duplicatePreset"
            text: qsTr("Duplicate preset")
            icon.name: FluentIcons.graph_Copy
            enabled: root.editingEnabled
            onTriggered: root.presetSaveRequested(presetContextMenu.presetDomain,
                presetContextMenu.filamentSlot, presetContextMenu.presetId, true)
        }
    }
    width: Math.min(344, boundsItem ? Math.max(0, boundsItem.width - 32) : 344)
    height: collapsed ? 42 : Math.max(42, Math.min(649, boundsItem ? boundsItem.height * 0.726 : 649,
                                                 boundsItem ? boundsItem.height - 112 - 96 : 649))
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
    function showObjects() {
        open();
        page = 1;
    }
    function showGlobalProcess() {
        open();
        page = 0;
    }
    function showLocalProcess(partId, name) {
        const params = {modelId: selectedModelId};
        if (partId.length) params.partId = partId;
        if (ActionManager.triggerAction("selection.pick", params))
            localModelId = selectedModelId;
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
        if (!selectedModelId.length || !editingEnabled)
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
                text: qsTr("Slice Settings")
                font: Typography.subtitle
                Layout.fillWidth: true
            }
            PrintPopoverHeader.PanelFoldButton {
                objectName: "print.configuration.collapse"
                folded: root.collapsed
                onToggleRequested: {
                    root.forceActiveFocus();
                    root.collapsed = !root.collapsed;
                }
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
            Layout.leftMargin: root.sectionInset
            Layout.rightMargin: root.sectionInset
            Layout.topMargin: 10
            Layout.bottomMargin: 9
            spacing: 7
            ParameterSectionHeader {
                objectName: "print.configuration.printerHeading"
                label: qsTr("Slicing printer")
                modified: SliceSettingsBridge.printerPresetModified
                iconSource: FluentIcons.graph_Print
                actionName: "print.configuration.details.printer"
                saveActionName: "print.configuration.savePrinterPreset"
                presetDomain: "printer"
                presetId: root.machine.printerPresetId || ""
                onTriggered: root.detailedSettingsRequested(2, -1, null)
            }
            PresetSelector {
                id: machineCombo
                objectName: "print.configuration.machine"
                presetDomain: "printer"
                presetId: root.machine.printerPresetId || ""
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
                displayText: root.machine.projectLocal
                    ? qsTr("Project printer · %1").arg(root.machine.printerModel || qsTr("Unnamed printer"))
                    : currentText
                enabled: root.editingEnabled
                currentIndex: {
                    const selected = SliceSettingsBridge.selectedMachineVariantId;
                    const modelId = SliceSettingsBridge.selectedMachineModelId;
                    return root.machineChoices.findIndex(function (item) {
                        return item.variantId === selected && item.modelId === modelId && item.presetId === root.machine.printerPresetId;
                    });
                }
                onActivated: function (index) {
                    const choice = root.machineChoices[index];
                    Qt.callLater(function () {
                        if (root.editingEnabled)
                            SliceSettingsBridge.selectMachine(choice.modelId, choice.variantId, "", choice.presetId);
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
            id: filamentSection
            objectName: "print.configuration.filaments"
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Project Filaments")
            Layout.fillWidth: true
            Layout.leftMargin: root.sectionInset
            Layout.rightMargin: root.sectionInset
            Layout.topMargin: 6
            Layout.bottomMargin: 6
            spacing: 4
            SectionHeader {
                objectName: "print.configuration.filamentHeading"
                label: filamentSection.Accessible.name
                modified: root.slots.some(slot => slot.presetModified)
                countText: qsTr("%1 slots").arg(root.slots.length)
                iconSource: FluentIcons.graph_Color
                actions: [
                    SectionIconButton {
                        objectName: "print.configuration.filaments.remove"
                        visible: root.materialControls.removeIndex >= 0
                        enabled: root.editingEnabled
                        icon.name: FluentIcons.graph_Remove
                        Accessible.name: qsTr("Remove last filament slot")
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        onClicked: SliceSettingsBridge.removeFilamentSlot(root.materialControls.removeIndex)
                    },
                    SectionIconButton {
                        objectName: "print.configuration.filaments.add"
                        visible: root.materialControls.canResize
                        enabled: root.editingEnabled && root.materialControls.canAdd
                        icon.name: FluentIcons.graph_Add
                        Accessible.name: qsTr("Add filament slot")
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        onClicked: {
                            const tool = root.materialControls.addTool;
                            if (tool >= 0)
                                SliceSettingsBridge.addFilamentToTool(tool);
                            else
                                SliceSettingsBridge.setFilamentSlotCount(root.slots.length + 1);
                        }
                    },
                    SectionIconButton {
                        id: filamentFold
                        objectName: "print.configuration.filaments.fold"
                        icon.name: root.filamentsCollapsed ? FluentIcons.graph_ChevronRightSmall
                                                           : FluentIcons.graph_ChevronDownSmall
                        Accessible.name: root.filamentsCollapsed ? qsTr("Expand filament slots") : qsTr("Collapse filament slots")
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        onClicked: root.filamentsCollapsed = !root.filamentsCollapsed
                    }
                ]
            }
            ScrollView {
                id: materialList
                visible: !root.filamentsCollapsed
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(materialRows.implicitHeight, root.height * 0.23)
                Layout.minimumHeight: 0
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    id: materialRows
                    width: materialList.availableWidth
                    spacing: 4
                    Repeater {
                        id: materialRepeater
                        model: root.slots.length
                        RowLayout {
                            id: materialRow
                            required property int index
                            readonly property var modelData: root.slots[index]
                            property alias editButton: editSlotButton
                            readonly property string slotLabel: String(modelData.number)
                                + (root.machine.physicalToolCount > 1 ? " · T" + modelData.physicalToolIndex : "")
                            Layout.fillWidth: true
                            spacing: 4

                            ColorPicker {
                                id: slotColor
                                property string openingRevision: ""
                                objectName: "print.configuration.slot." + materialRow.index
                                Layout.preferredWidth: root.machine.physicalToolCount > 1 ? 65 : 39
                                Layout.preferredHeight: 30
                                padding: 3
                                flat: true
                                enabled: root.editingEnabled
                                current: materialRow.modelData.color
                                titleText: qsTr("Filament %1 Color").arg(materialRow.modelData.number)
                                Accessible.name: qsTr("Edit filament %1 color").arg(materialRow.slotLabel)
                                ToolTip.visible: hovered
                                ToolTip.text: Accessible.name + " · " + String(current).toUpperCase()
                                contentItem: RowLayout {
                                    spacing: 4
                                    Rectangle {
                                        Layout.preferredWidth: 10
                                        Layout.preferredHeight: 10
                                        radius: 2
                                        color: slotColor.current
                                        border.color: root.lineColor
                                        border.width: 1
                                    }
                                    Label {
                                        text: materialRow.slotLabel
                                        font.pixelSize: 10
                                    }
                                }
                                onClicked: {
                                    openingRevision = SliceSettingsBridge.configurationRevisionToken()
                                    root.materialError = ""
                                }
                                onAccepted: {
                                    const accepted = SliceSettingsBridge.setFilamentColor(
                                        materialRow.index, current, openingRevision)
                                    if (!accepted)
                                        root.materialError = SliceSettingsBridge.configurationError || qsTr("Unable to change filament color")
                                    current = Qt.binding(() => materialRow.modelData.color)
                                    Qt.callLater(() => { if (slotColor.visible && slotColor.enabled) slotColor.forceActiveFocus() })
                                }
                            }
                            PresetSelector {
                                id: preset
                                objectName: "print.configuration.filament." + materialRow.modelData.number
                                presetDomain: "filament"
                                filamentSlot: materialRow.modelData.index
                                presetId: materialRow.modelData.presetId
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredHeight: 30
                                font.pixelSize: 11
                                model: materialRow.modelData.compatiblePresets
                                textRole: "name"
                                currentIndex: root.indexOf(model, "id", materialRow.modelData.presetId)
                                displayText: (materialRow.modelData.presetModified ? "* " : "")
                                    + materialRow.modelData.presetName
                                enabled: root.editingEnabled && model.length > 0
                                onActivated: function(index) {
                                    const slot = materialRow.modelData.index;
                                    const presetId = model[index].id;
                                    const revision = SliceSettingsBridge.configurationRevisionToken();
                                    Qt.callLater(function() {
                                        if (root.editingEnabled && revision === SliceSettingsBridge.configurationRevisionToken())
                                            SliceSettingsBridge.selectFilamentPresetForSlot(slot, presetId);
                                    });
                                }
                            }
                            Button {
                                id: editSlotButton
                                objectName: "print.configuration.filament.details." + materialRow.modelData.number
                                Layout.preferredWidth: 48
                                Layout.preferredHeight: 28
                                flat: true
                                enabled: root.editingEnabled
                                text: qsTr("Edit")
                                font.pixelSize: 11
                                Accessible.name: qsTr("Edit filament %1 parameters").arg(materialRow.slotLabel)
                                ToolTip.visible: hovered
                                ToolTip.text: Accessible.name
                                onClicked: {
                                    const slot = materialRow.index
                                    root.detailedSettingsRequested(1, slot, function() {
                                        const row = materialRepeater.itemAt(slot)
                                        if (row && row.editButton.visible && row.editButton.enabled)
                                            row.editButton.forceActiveFocus()
                                        else if (filamentFold.visible)
                                            filamentFold.forceActiveFocus()
                                    })
                                }
                            }
                            PresetSaveButton {
                                objectName: "print.configuration.saveFilamentPreset." + materialRow.modelData.number
                                visible: materialRow.modelData.presetModified
                                enabled: visible && root.editingEnabled && !!materialRow.modelData.presetId
                                onClicked: root.presetSaveRequested("filament", materialRow.modelData.index,
                                    materialRow.modelData.presetId, false)
                            }
                        }
                    }
                }
            }
            Label {
                visible: root.materialError.length > 0
                Layout.fillWidth: true
                text: root.materialError
                wrapMode: Text.Wrap
                color: root.SmoothUI.theme.res.systemFillColorCritical
                font: Typography.caption
            }
        }
        Rule {}
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.sectionInset
            Layout.rightMargin: root.sectionInset
            Layout.topMargin: 9
            Layout.bottomMargin: 9
            spacing: 7
            ParameterSectionHeader {
                objectName: "print.configuration.processHeading"
                label: qsTr("Process preset")
                modified: SliceSettingsBridge.modifiedSettingKeys.length > 0
                iconSource: FluentIcons.graph_Equalizer
                actionName: "print.configuration.details.process"
                saveActionName: "print.configuration.saveProcessPreset"
                presetDomain: "process"
                presetId: SliceSettingsBridge.selectedProcessPresetId
                onTriggered: root.detailedSettingsRequested(0, -1, null)
            }
            PresetSelector {
                id: processCombo
                objectName: "print.configuration.process"
                presetDomain: "process"
                presetId: SliceSettingsBridge.selectedProcessPresetId
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
                    model: [qsTr("Global process"), qsTr("Model objects")]
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
            automationNamespace: "print.configuration"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            visible: root.page === 0
            compactPanel: true
            embedded: true
            showParamKeys: false
            fiberAvailable: root.fiberCapable
        }
        RowLayout {
            visible: root.editingLocalProcess
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Button {
                objectName: "print.configuration.localBack"
                flat: true
                icon.name: FluentIcons.graph_Back
                Accessible.name: qsTr("Back to model objects")
                onClicked: root.localModelId = ""
            }
            Label {
                Layout.fillWidth: true
                text: root.localPartId.length
                    ? qsTr("Modifier parameters · %1").arg(root.localTargetName)
                    : qsTr("Model parameters · %1").arg(root.localTargetName)
                elide: Text.ElideRight
                font: Typography.bodyStrong
            }
        }
        Label {
            visible: root.editingLocalProcess
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 6
            text: qsTr("Changes apply to the selected model instance.")
            font: Typography.caption
            color: root.secondaryText
            wrapMode: Text.WordWrap
        }
        Loader {
            active: root.editingLocalProcess
            visible: active
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            sourceComponent: SlicingSettingsContent {
                automationNamespace: "print.configuration.local"
                compactPanel: true
                embedded: true
                showParamKeys: false
                fiberAvailable: root.fiberCapable
                localMode: true
                localModelId: root.localModelId
                localPartId: root.localPartId
            }
        }
        ScrollView {
            id: objectsScroll
            objectName: "print.configuration.objects"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            visible: root.page === 1 && !root.editingLocalProcess
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
                    visible: !root.selectedModelId.length
                    text: SelectionBridge.hasSelection ? qsTr("Select one model to edit its properties") : qsTr("Select a model to view its properties")
                    font.pixelSize: 11
                    color: root.secondaryText
                    wrapMode: Text.Wrap
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: ModelPropertyBridge.hasSelectedModel && root.selectedModelId.length > 0
                    enabled: root.editingEnabled && !FiberMaskBridge.active && !CutToolBridge.active
                    spacing: 10
                    Button {
                        objectName: "print.configuration.selectParent"
                        visible: root.modifierSelected
                        Layout.fillWidth: true
                        text: qsTr("Select whole model")
                        onClicked: ActionManager.triggerAction("selection.pick", {modelId: root.selectedModelId})
                    }
                    Button {
                        objectName: "print.configuration.objectSettings"
                        Layout.fillWidth: true
                        text: qsTr("Model parameters")
                        visible: !root.modifierSelected
                        onClicked: root.showLocalProcess("", ModelPropertyBridge.displayName)
                    }
                    Repeater {
                        model: {
                            const models = sceneModels.models
                            return root.selectedModelId.length
                                ? SliceSettingsBridge.parameterModifiers(root.selectedModelId) : []
                        }
                        delegate: Button {
                            required property int index
                            required property var modelData
                            objectName: "print.configuration.modifierSettings." + index
                            Layout.fillWidth: true
                            text: qsTr("Modifier %1 · %2").arg(index + 1).arg(modelData.name)
                            highlighted: root.localPartId === modelData.id
                            onClicked: ActionManager.triggerAction("selection.pick", {
                                modelId: root.selectedModelId, partId: modelData.id
                            })
                        }
                    }
                    Button {
                        objectName: "print.configuration.modifierParameters"
                        visible: root.modifierSelected
                        Layout.fillWidth: true
                        text: qsTr("Modifier parameters")
                        onClicked: root.showLocalProcess(root.localPartId, ModelPropertyBridge.displayName)
                    }
                    Label {
                        visible: root.modifierSelected
                        Layout.fillWidth: true
                        text: qsTr("Modifier") + " · " + ModelPropertyBridge.displayName
                        wrapMode: Text.Wrap
                    }
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
                            visible: !root.modifierSelected || kind === "position"
                            Layout.fillWidth: true
                            spacing: 7
                            Label {
                                text: parent.kind === "position" ? (root.modifierSelected ? qsTr("Position relative to model") : qsTr("Position")) + " / mm" : qsTr("Rotation") + " / °"
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
                        text: root.modifierSelected ? qsTr("Modifier material") : qsTr("Model material")
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
                    Label {
                        visible: root.modifierSelected && ModelPropertyBridge.filamentBindingMode === 2
                        text: qsTr("Inherited from model")
                        font.pixelSize: 11
                        color: root.secondaryText
                    }
                    RowLayout {
                        visible: !root.modifierSelected
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
                        visible: !root.modifierSelected
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
                        visible: !root.modifierSelected
                        text: qsTr("Change model appearance color")
                        Layout.fillWidth: true
                        implicitHeight: 30
                        font.pixelSize: 11
                        onClicked: modelColorDialog.open()
                    }
                    Button {
                        objectName: "print.configuration.fiberMask"
                        visible: root.fiberCapable && !root.modifierSelected
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
                    text: root.page === 0
                        ? (SliceSettingsBridge.modifiedSettingKeys.length > 0
                            ? qsTr("%1 settings modified").arg(SliceSettingsBridge.modifiedSettingKeys.length)
                              + (SliceSettingsBridge.hasPendingReslice ? qsTr(" · Reslicing required") : "")
                            : qsTr("Process preset unchanged"))
                        : (SliceSettingsBridge.hasPendingReslice ? qsTr("Reslicing required") : "")
                    font.pixelSize: 10
                    color: root.secondaryText
                    elide: Text.ElideRight
                }
                Button {
                    objectName: "print.configuration.reset"
                    visible: root.page === 0 && SliceSettingsBridge.modifiedSettingKeys.length > 0
                    text: qsTr("Restore changes")
                    flat: true
                    font.pixelSize: 10
                    implicitHeight: 28
                    enabled: root.editingEnabled
                    onClicked: SliceSettingsBridge.restoreCurrentPreset("process")
                }
            }
        }
    }

    component Rule: Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: root.lineColor
    }
    component SectionHeader: RowLayout {
        id: sectionHeader
        property string label: ""
        property string countText: ""
        property string iconSource: ""
        property bool modified: false
        property alias actions: actionGroup.data
        Layout.fillWidth: true
        Layout.minimumHeight: root.sectionActionSize
        Layout.maximumHeight: root.sectionActionSize
        spacing: 6
        Icon {
            source: sectionHeader.iconSource
            Layout.preferredWidth: 13
            Layout.preferredHeight: 13
            color: root.secondaryText
        }
        Label {
            objectName: sectionHeader.objectName ? sectionHeader.objectName + ".label" : ""
            text: (sectionHeader.modified ? "* " : "") + sectionHeader.label
            Layout.fillWidth: !slotCount.visible
            Layout.minimumWidth: 0
            elide: Text.ElideRight
            font: Typography.bodyStrong
        }
        Label {
            id: slotCount
            text: sectionHeader.countText
            visible: text.length > 0
            Layout.fillWidth: true
            font.pixelSize: 10
            color: root.secondaryText
        }
        RowLayout {
            id: actionGroup
            spacing: 6
        }
    }
    component ParameterSectionHeader: SectionHeader {
        id: parameterHeader
        property string actionName: ""
        property string saveActionName: ""
        property string presetDomain: ""
        property string presetId: ""
        signal triggered
        actions: [
            PresetSaveButton {
                objectName: parameterHeader.saveActionName
                visible: parameterHeader.modified
                enabled: visible && root.editingEnabled && parameterHeader.presetId.length > 0
                onClicked: root.presetSaveRequested(parameterHeader.presetDomain, -1, parameterHeader.presetId, false)
            },
            Button {
                id: parameterButton
                objectName: parameterHeader.actionName
                text: qsTr("View Parameters")
                icon.name: FluentIcons.graph_ChevronRightSmall
                icon.width: root.sectionActionIconSize
                icon.height: root.sectionActionIconSize
                flat: true
                padding: 0
                horizontalPadding: (root.sectionActionSize - root.sectionActionIconSize) / 2
                spacing: 6
                font.pixelSize: 11
                Layout.preferredHeight: root.sectionActionSize
                Layout.maximumWidth: (root.width - 2 * (root.sectionInset + 1)) / 2
                ToolTip.visible: hovered && width < implicitWidth
                ToolTip.text: text
                contentItem: RowLayout {
                    spacing: parameterButton.spacing
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: parameterButton.text
                        font: parameterButton.font
                        color: parameterButton.SmoothUI.textColor
                        elide: Text.ElideRight
                    }
                    Icon {
                        Layout.preferredWidth: parameterButton.icon.width
                        Layout.preferredHeight: parameterButton.icon.height
                        source: parameterButton.icon.name
                        color: parameterButton.icon.color
                    }
                }
                onClicked: parameterHeader.triggered()
            }
        ]
    }
    component SectionIconButton: IconButton {
        Layout.preferredWidth: root.sectionActionSize
        Layout.preferredHeight: root.sectionActionSize
        flat: true
        padding: 0
        icon.width: root.sectionActionIconSize
        icon.height: root.sectionActionIconSize
    }
    component PresetSaveButton: SectionIconButton {
        icon.name: FluentIcons.graph_Save
        Accessible.name: qsTr("Save preset")
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
    }
    component PresetSelector: ComboBox {
        id: presetSelector
        property string presetDomain: ""
        property string presetId: ""
        property int filamentSlot: -1

        function openPresetMenu() {
            if (!enabled || !presetId.length)
                return false
            presetContextMenu.presetDomain = presetDomain
            presetContextMenu.filamentSlot = filamentSlot
            presetContextMenu.presetId = presetId
            presetContextMenu.popup(presetSelector, 0, presetSelector.height)
            return true
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: presetSelector.openPresetMenu()
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier))
                event.accepted = presetSelector.openPresetMenu()
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
