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
    property int activeSlot: 0
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
                    name: variant.userPreset ? variant.name : models[i].name + " · " + variant.nozzleDiameter + " mm"
                });
            }
        }
        return choices;
    }
    readonly property var activeFilament: slots.length ? slots[Math.min(activeSlot, slots.length - 1)] : ({})
    onSlotsChanged: activeSlot = Math.max(0, Math.min(activeSlot, slots.length - 1))
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
                text: qsTr("Slice Settings")
                font.pixelSize: 12
                font.weight: Font.DemiBold
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
            Accessible.name: qsTr("Filaments")
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 6
            Layout.bottomMargin: 6
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Icon {
                    source: FluentIcons.graph_Color
                    Layout.preferredWidth: 13
                    Layout.preferredHeight: 13
                    color: root.secondaryText
                }
                Label {
                    text: qsTr("Filaments")
                    font.pixelSize: 11
                    color: root.secondaryText
                }
                Flickable {
                    id: slotStrip
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 24
                    contentWidth: slotButtons.width
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick
                    function revealActiveSlot() {
                        const button = slotRepeater.itemAt(root.activeSlot);
                        if (!button) return;
                        contentX = Math.max(0, Math.min(button.x, contentWidth - width,
                            Math.max(contentX, button.x + button.width - width)));
                    }
                    onWidthChanged: Qt.callLater(revealActiveSlot)
                    Connections {
                        target: root
                        function onActiveSlotChanged() { Qt.callLater(slotStrip.revealActiveSlot) }
                        function onSlotsChanged() { Qt.callLater(slotStrip.revealActiveSlot) }
                    }
                    Row {
                        id: slotButtons
                        spacing: 4
                        Repeater {
                            id: slotRepeater
                            model: root.slots
                            Button {
                                id: slotButton
                                required property int index
                                required property var modelData
                                objectName: "print.configuration.slot." + index
                                readonly property string slotLabel: {
                                    if (root.machine.filamentSlotsBoundToPhysicalTools || root.machine.physicalToolCount > 1) {
                                        // Multiple materials may share a tool. Keep their slot identities distinct.
                                        const prefix = root.slots.length > root.machine.physicalToolCount
                                            ? String(modelData.number) + "/" : "";
                                        return prefix + "T" + modelData.physicalToolIndex;
                                    }
                                    return String(modelData.number);
                                }
                                width: Math.max(38, implicitContentWidth + 16)
                                height: 24
                                padding: 4
                                font.pixelSize: 10
                                text: slotLabel
                                highlighted: root.activeSlot === index
                                Accessible.name: slotLabel + " · " + modelData.presetName
                                contentItem: Row {
                                    spacing: 4
                                    Rectangle {
                                        width: 8
                                        height: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        radius: 2
                                        color: slotButton.modelData.color || "#929aa3"
                                    }
                                    Label {
                                        text: slotButton.text
                                        font: slotButton.font
                                        color: root.secondaryText
                                    }
                                }
                                onClicked: root.activeSlot = index
                                ToolTip.visible: hovered
                                ToolTip.text: Accessible.name
                            }
                        }
                    }
                }
                Button {
                    objectName: "print.configuration.filaments.edit"
                    text: qsTr("Edit")
                    icon.name: FluentIcons.graph_Edit
                    icon.width: 12
                    icon.height: 12
                    flat: true
                    padding: 0
                    font.pixelSize: 11
                    enabled: root.editingEnabled
                    Layout.preferredHeight: 24
                    onClicked: root.detailedSettingsRequested(1)
                }
            }
            ComboBox {
                objectName: "print.configuration.filament." + (root.activeFilament.number || 0)
                Layout.fillWidth: true
                implicitHeight: 32
                font.pixelSize: 11
                leftPadding: 31
                Rectangle {
                    x: 9
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    radius: 4
                    color: root.activeFilament.color || "#929aa3"
                }
                model: root.activeFilament.compatiblePresets || SliceSettingsBridge.compatibleFilamentPresets
                textRole: "name"
                enabled: root.editingEnabled && root.slots.length > 0
                currentIndex: root.indexOf(model, "id", root.activeFilament.presetId)
                onActivated: function(index) {
                    const preset = model[index].id, slot = root.activeFilament.index;
                    const modelId = SliceSettingsBridge.selectedMachineModelId;
                    const variantId = SliceSettingsBridge.selectedMachineVariantId;
                    Qt.callLater(function() {
                        if (root.editingEnabled && modelId === SliceSettingsBridge.selectedMachineModelId && variantId === SliceSettingsBridge.selectedMachineVariantId)
                            SliceSettingsBridge.selectFilamentPresetForSlot(slot, preset);
                    });
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
