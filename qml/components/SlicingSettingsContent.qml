import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import GPlatform 1.0
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl

Item {
    id: root
    SmoothUI.theme: Theme.of(root)
    clip: true
    implicitWidth: preferredWidth
    implicitHeight: preferredHeight
    Layout.minimumWidth: embedded || compactPanel ? 0 : 700
    Layout.minimumHeight: embedded || compactPanel ? 0 : 480

    // Embedded mode delegates the window chrome to the owning popup. The
    // settings component remains reusable in the slicing workspace, where it
    // still renders its own title bar by default.
    property bool embedded: false
    property bool compactPanel: false
    // Reusing this component must not give retained/hidden editors the same
    // automation identity. An empty namespace preserves existing recordings.
    property string automationNamespace: ""
    function automationName(name) {
        return automationNamespace.length > 0 ? automationNamespace + "." + name : name
    }
    readonly property font compactCaptionFont: Qt.font({family: Typography.caption.family, pixelSize: 11})
    property bool fiberAvailable: false
    property string compactCategory: "quality"
    readonly property var compactCatalog: [
        {id:"quality", title:qsTr("Quality"), sections:[
            {title:qsTr("Layer height"), keys:["layer_height","initial_layer_print_height"]},
            {title:qsTr("Line width"), keys:["line_width","initial_layer_line_width"]},
            {title:qsTr("Seam"), keys:["seam_position"]},
            {title:qsTr("Precision"), keys:["resolution"]}]},
        {id:"strength", title:qsTr("Strength"), sections:[
            {title:qsTr("Wall layers"), keys:["wall_loops"]},
            {title:qsTr("Top and bottom surfaces"), keys:["top_shell_layers","bottom_shell_layers"]},
            {title:qsTr("Sparse infill"), keys:["sparse_infill_density","sparse_infill_pattern"]}]},
        {id:"speed", title:qsTr("Speed"), sections:[
            {title:qsTr("Printing speed"), keys:["outer_wall_speed","inner_wall_speed","sparse_infill_speed","top_surface_speed"]},
            {title:qsTr("First layer"), keys:["initial_layer_speed"]},
            {title:qsTr("Travel"), keys:["travel_speed"]},
            {title:qsTr("Acceleration"), keys:["default_acceleration"]}]},
        {id:"support", title:qsTr("Support"), sections:[
            {title:qsTr("Support"), keys:["enable_support","support_type","support_on_build_plate_only"]},
            {title:qsTr("Contact interface"), keys:["support_top_z_distance"]}]},
        {id:"others", title:qsTr("Others"), sections:[
            {title:qsTr("Build plate adhesion"), keys:["brim_type","brim_width"]},
            {title:qsTr("Special modes"), keys:["spiral_mode"]}]},
        {id:"fiber", title:qsTr("Continuous fiber"), sections:[
            {title:qsTr("Reinforced contours"), keys:["generate_reinforced_perimeters","outer_reinforced_perimeters_counts","fiber_width"]},
            {title:qsTr("Reinforced infill"), keys:["generate_reinforced_infills","reinforced_infill_pattern","reinforced_infill_density","fiber_infill_angle_sequence"]},
            {title:qsTr("Turns and spacing"), keys:["fiber_infill_bend_radius","fiber_layer_height_ratio"]}]}
    ]
    readonly property var compactPages: compactCatalog.filter(function(page) { return page.id !== "fiber" || fiberAvailable })
    readonly property var compactDefinitions: {
        if (!compactPanel) return []
        const revision = schemaRevisionToken
        const rows = []
        for (const page of SliceSettingsBridge.getTabPages("process", showAdvancedMode)) {
            for (const section of SliceSettingsBridge.getPageSchema("process", page.id, showAdvancedMode)) {
                for (const row of section.rows || []) {
                    if (fiberAvailable || compactSchemaCategory(row.page) !== "fiber")
                        rows.push(row)
                }
            }
        }
        return rows
    }
    function compactSchemaCategory(page) {
        const id = String(page || "").toLowerCase()
        if (id.indexOf("continuous-fiber") >= 0) return "fiber"
        for (const category of ["quality", "strength", "speed", "support"])
            if (id.indexOf(category) >= 0) return category
        return "others"
    }
    readonly property var compactSections: {
        const category = compactCatalog.find(function(page) { return page.id === compactCategory })
        if (!category) return []
        const definitions = {}
        const placements = {}
        for (const row of compactDefinitions) definitions[row.key] = row
        for (const page of compactCatalog)
            for (const section of page.sections)
                for (const key of section.keys) placements[key] = page.id
        const sections = []
        const included = {}
        for (const section of category.sections) {
            const rows = []
            for (const key of section.keys) {
                const definition = definitions[key]
                if (!definition) continue
                included[key] = true
                // Common fields keep the reviewed ordering. Visibility and
                // access level always come from the full live schema.
                rows.push(Object.assign({}, definition, {autoInit:false,
                    label:key === "line_width" ? qsTr("Default line width") : definition.label}))
            }
            if (rows.length) sections.push({id:section.keys[0], title:section.title, rows:rows})
        }
        const remaining = {}
        for (const definition of compactDefinitions) {
            const placement = placements[definition.key] || compactSchemaCategory(definition.page)
            if (placement !== compactCategory || included[definition.key]) continue
            const id = definition.page || "other"
            if (!remaining[id]) {
                remaining[id] = {id:id, title:localizedSchemaText(definition.pageTitle, category.title), rows:[]}
                sections.push(remaining[id])
            }
            remaining[id].rows.push(Object.assign({}, definition, {autoInit:false}))
        }
        return sections
    }

    property bool showParamKeys: true
    property int workspaceIndex: 0
    property bool showAdvancedMode: false
    property string searchText: ""
    property string focusedSettingKey: ""
    property int selectedProcessPage: 0
    property int selectedFilamentPage: 0
    property int selectedPrinterPage: 0
    property var machineModels: SliceSettingsBridge.machineModels()
    property var processPresets: SliceSettingsBridge.compatibleProcessPresets
    property var filamentPresets: SliceSettingsBridge.compatibleFilamentPresets
    property var filamentSlots: SliceSettingsBridge.filamentSlots
    readonly property bool independentDualTools: SliceSettingsBridge.selectedMachine.physicalToolCount === 2
        && !SliceSettingsBridge.selectedMachine.variableFilamentSlots
    readonly property bool fixedPhysicalToolSlots: !!SliceSettingsBridge.selectedMachine.filamentSlotsBoundToPhysicalTools
    readonly property var toolMaterialCapacities: SliceSettingsBridge.selectedMachine.toolMaterialCapacities || []
    readonly property bool perToolMaterials: toolMaterialCapacities.length > 0
    readonly property bool editableToolMapping: independentDualTools && !fixedPhysicalToolSlots && !perToolMaterials
    readonly property var toolOrderedSlots: independentDualTools
        ? filamentSlots.slice().sort(function(a, b) { return a.physicalToolIndex - b.physicalToolIndex })
        : filamentSlots
    readonly property var toolGroups: perToolMaterials
        ? toolMaterialCapacities.map(function(capacity, physical) {
            return {physical: physical, capacity: capacity,
                    slots: root.filamentSlots.filter(function(slot) { return slot.physicalToolIndex === physical })}
        })
        : [{physical: -1, capacity: 0, slots: toolOrderedSlots}]
    property var plateOptions: SliceSettingsBridge.buildPlateOptions()
    property string currentBedType: ""
    signal closeRequested()
    readonly property bool settingsEditingEnabled: !TaskStateNotifier.isBusy

    function physicalToolLabel(slot) {
        var nozzle = qsTr("Nozzle %1").arg(slot.physicalToolIndex)
        if (root.perToolMaterials)
            return nozzle + " · " + (slot.physicalToolRole === "continuous_fiber"
                ? qsTr("Continuous fiber") : qsTr("Base material"))
        if (slot.physicalToolSide === "left")
            nozzle = qsTr("Left nozzle")
        else if (slot.physicalToolSide === "right")
            nozzle = qsTr("Right nozzle")
        if (slot.physicalToolRole === "continuous_fiber")
            return nozzle + " · " + qsTr("Continuous fiber")
        if (slot.physicalToolRole === "substrate" || slot.physicalToolRole === "thermoplastic")
            return nozzle + " · " + qsTr("Base material")
        return nozzle
    }

    readonly property var processPages: (schemaRevisionToken >= 0 && useSchemaDriven)
                                      ? SliceSettingsBridge.getTabPages("process", showAdvancedMode)
                                      : []
    readonly property var filamentPages: (schemaRevisionToken >= 0 && useSchemaDriven)
                                       ? SliceSettingsBridge.getTabPages("filament", showAdvancedMode)
                                       : []
    readonly property var printerPages: (schemaRevisionToken >= 0 && useSchemaDriven)
                                      ? SliceSettingsBridge.getTabPages("printer", showAdvancedMode)
                                      : []

    readonly property var currentSidebarModel: workspaceIndex === 0
                                            ? processPages
                                            : (workspaceIndex === 1 ? filamentPages : printerPages)
    readonly property int currentSidebarIndex: workspaceIndex === 0
                                             ? selectedProcessPage
                                             : (workspaceIndex === 1 ? selectedFilamentPage : selectedPrinterPage)

    readonly property int preferredWidth: workspaceIndex === 0 ? 820 : 780
    readonly property int preferredHeight: 620
    readonly property string localeTranslationToken: AppInfo.locale || ""
    readonly property string currentPageTitle: {
        if (!currentSidebarModel || currentSidebarModel.length === 0) {
            return qsTr("Parameters")
        }
        var safeIndex = Math.max(0, Math.min(currentSidebarIndex, currentSidebarModel.length - 1))
        return localizedSchemaText(currentSidebarModel[safeIndex].title, qsTr("Parameters"))
    }

    readonly property bool canReslice: SlicingPreviewBridge.resliceAvailable
    readonly property var machineVariantOptions: flattenMachineVariants()
    readonly property var plateDisplayOptions: localizedPlateOptions()

    readonly property bool useSchemaDriven: SliceSettingsBridge.schemaLoaded
    readonly property int schemaRevisionToken: SliceSettingsBridge.schemaRevision
    readonly property bool globalSearchMode: searchText.trim().length > 0
    readonly property bool sidebarVisible: !globalSearchMode
                                            && !(workspaceIndex === 1
                                                 && currentSidebarModel.length <= 1)
    readonly property var globalSearchResults: (schemaRevisionToken >= 0 && localeTranslationToken.length >= 0
                                                 && useSchemaDriven && globalSearchMode)
                                              ? SliceSettingsBridge.getGlobalSearchResults(searchText, compactPanel || showAdvancedMode)
                                              : []
    readonly property int globalSearchResultCount: globalSearchResults.length
    readonly property string currentSchemaTab: workspaceIndex === 0 ? "process" : (workspaceIndex === 1 ? "filament" : "printer")
    readonly property string currentSchemaPageId: workspaceIndex === 0
                                                  ? currentProcessPageId()
                                                  : (workspaceIndex === 1 ? currentFilamentPageId() : currentPrinterPageId())
    readonly property var currentSchemaSections: (schemaRevisionToken >= 0 && useSchemaDriven)
                                                 ? SliceSettingsBridge.getPageSchema(currentSchemaTab,
                                                                                         currentSchemaPageId,
                                                                                         showAdvancedMode,
                                                                                         globalSearchMode ? "" : searchText)
                                                 : []

    onShowAdvancedModeChanged: {
        selectedProcessPage = Math.max(0, Math.min(selectedProcessPage, processPages.length - 1))
    }

    function triggerReslice() {
        if (!canReslice) {
            return
        }

        ActionManager.triggerAction("slicing.reslice", {})
    }

    function focusSetting(paramKey) {
        const key = String(paramKey || "").trim()
        if (key.length === 0)
            return false
        // Searching by Orca's exact opt_key is deterministic across schema
        // page reorganizations and leaves the matching editor at the top.
        showAdvancedMode = true
        focusedSettingKey = key
        searchText = key
        return true
    }

    function indexByValue(items, key, value) {
        if (!items)
            return -1
        for (var index = 0; index < items.length; ++index) {
            if (items[index] && items[index][key] === value)
                return index
        }
        return -1
    }

    function flattenMachineVariants() {
        var result = []
        if (!machineModels)
            return result
        for (var machineIndex = 0; machineIndex < machineModels.length; ++machineIndex) {
            var machine = machineModels[machineIndex]
            var variants = machine.variants || []
            for (var variantIndex = 0; variantIndex < variants.length; ++variantIndex) {
                var variant = variants[variantIndex]
                result.push({
                    "modelId": machine.id,
                    "variantId": variant.id,
                    "name": qsTr("%1 · %2 mm").arg(machine.name).arg(variant.nozzleDiameter)
                })
            }
        }
        return result
    }

    function bedTypeLabel(value) {
        var labels = {
            "Cool Plate": qsTr("Smooth Cool Plate"),
            "Smooth Cool Plate": qsTr("Smooth Cool Plate"),
            "Engineering Plate": qsTr("Engineering Plate"),
            "High Temp Plate": qsTr("Smooth High-Temperature Plate"),
            "Smooth High Temp Plate": qsTr("Smooth High-Temperature Plate"),
            "Smooth PEI Plate": qsTr("Smooth PEI Plate"),
            "Textured PEI Plate": qsTr("Textured PEI Plate"),
            "Textured Cool Plate": qsTr("Textured Cool Plate"),
            "Supertack Plate": qsTr("High-Tack Cool Plate"),
            "Default Plate": qsTr("Default Build Plate")
        }
        return labels[value] || value || qsTr("Select Build Plate")
    }

    function localizedPlateOptions() {
        var result = []
        for (var index = 0; index < plateOptions.length; ++index) {
            var option = plateOptions[index]
            result.push({
                "value": option.value,
                "name": bedTypeLabel(option.value),
                "imageUrl": option.imageUrl
            })
        }
        return result
    }

    function currentMachineVariantIndex() {
        for (var index = 0; index < machineVariantOptions.length; ++index) {
            var option = machineVariantOptions[index]
            if (option.modelId === SliceSettingsBridge.selectedMachineModelId
                    && option.variantId === SliceSettingsBridge.selectedMachineVariantId)
                return index
        }
        return -1
    }

    function refreshContext() {
        machineModels = SliceSettingsBridge.machineModels()
        currentBedType = String(SliceSettingsBridge.getSetting("curr_bed_type", ""))
    }

    function syncPresetComboSelection() {
        printerPresetCombo.currentIndex = root.currentMachineVariantIndex()
        processPresetCombo.currentIndex = root.indexByValue(
                    root.processPresets, "id",
                    SliceSettingsBridge.selectedProcessPresetId)
    }

    function selectMachineAfterComboClose(index) {
        if (index < 0 || index >= machineVariantOptions.length)
            return
        var option = machineVariantOptions[index]
        SliceSettingsBridge.selectMachine(option.modelId, option.variantId)
        Qt.callLater(function() {
            root.syncPresetComboSelection()
        })
    }

    function filamentSlotsSummary() {
        if (!filamentSlots || filamentSlots.length === 0)
            return qsTr("No filaments")
        var types = []
        for (var index = 0; index < filamentSlots.length; ++index) {
            var type = filamentSlots[index].materialType || qsTr("Unknown")
            if (types.indexOf(type) < 0)
                types.push(type)
        }
        return qsTr("%1 slots · %2").arg(filamentSlots.length).arg(types.join(" / "))
    }

    function selectProcessAfterComboClose(index) {
        if (index < 0 || index >= processPresets.length)
            return
        var presetId = processPresets[index].id
        SliceSettingsBridge.selectProcessPreset(presetId)
        Qt.callLater(function() {
            root.syncPresetComboSelection()
        })
    }

    function setCurrentSidebarIndex(index) {
        if (workspaceIndex === 0) {
            selectedProcessPage = index
            return
        }
        if (workspaceIndex === 1) {
            selectedFilamentPage = index
            return
        }
        selectedPrinterPage = index
    }

    function currentProcessPageId() {
        if (!processPages || processPages.length === 0) {
            return "quality"
        }
        var safeIndex = Math.max(0, Math.min(selectedProcessPage, processPages.length - 1))
        return processPages[safeIndex].id || "quality"
    }

    function currentFilamentPageId() {
        if (!filamentPages || filamentPages.length === 0) {
            return "filament"
        }
        var safeIndex = Math.max(0, Math.min(selectedFilamentPage, filamentPages.length - 1))
        return filamentPages[safeIndex].id || "filament"
    }

    function currentPrinterPageId() {
        if (!printerPages || printerPages.length === 0) {
            return "basic_info"
        }
        var safeIndex = Math.max(0, Math.min(selectedPrinterPage, printerPages.length - 1))
        return printerPages[safeIndex].id || "basic_info"
    }

    function translateSchemaBaseText(baseText) {
        var base = (baseText === undefined || baseText === null) ? "" : String(baseText)
        if (base.trim().length === 0) {
            return base
        }
        var translated = qsTranslate("LibSlicerSettings", base)
        return translated && translated.length > 0 ? translated : base
    }

    function localizedSchemaText(baseText, fallbackText) {
        var base = (baseText === undefined || baseText === null) ? "" : String(baseText)
        if (base.trim().length > 0) {
            return translateSchemaBaseText(base)
        }
        return fallbackText || ""
    }

    function localizedEnumOptions(options) {
        var result = []
        if (!options) {
            return result
        }
        for (var i = 0; i < options.length; ++i) {
            var option = options[i]
            if (option && option.value !== undefined) {
                result.push({
                    "value": option.value,
                    "label": translateSchemaBaseText(option.label || option.value)
                })
            } else {
                result.push({
                    "value": option,
                    "label": translateSchemaBaseText(option)
                })
            }
        }
        return result
    }

    function displayTabTitle(tabId) {
        var tab = (tabId || "").toLowerCase()
        if (tab === "process") {
            return qsTr("Process")
        }
        if (tab === "filament") {
            return qsTr("Filament")
        }
        if (tab === "printer") {
            return qsTr("Printers")
        }
        return tabId || ""
    }

    ColumnLayout {
        visible: !root.compactPanel
        enabled: !root.compactPanel
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.embedded ? 0 : PrintWorkspaceStyle.titleBarHeight
            Layout.minimumHeight: root.embedded ? 0 : PrintWorkspaceStyle.titleBarHeight
            Layout.maximumHeight: root.embedded ? 0 : PrintWorkspaceStyle.titleBarHeight
            visible: !root.embedded
            topLeftRadius: PrintWorkspaceStyle.menuRadius
            topRightRadius: PrintWorkspaceStyle.menuRadius
            color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.SmoothUI.theme.res.dividerStrokeColorDefault
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 8

                Label {
                    text: qsTr("Slicing Parameters")
                    font: Typography.bodyStrong
                    color: root.SmoothUI.theme.res.textFillColorPrimary
                }

                Item { Layout.fillWidth: true }

                IconButton {
                    implicitWidth: PrintWorkspaceStyle.closeButtonSize
                    implicitHeight: PrintWorkspaceStyle.closeButtonSize
                    radius: PrintWorkspaceStyle.controlRadius
                    flat: true
                    icon.name: FluentIcons.graph_ChromeClose
                    icon.width: 14
                    icon.height: 14
                    onClicked: root.closeRequested()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            Layout.minimumHeight: 58
            Layout.maximumHeight: 58
            color: PrintWorkspaceStyle.surface(root.SmoothUI.dark)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.SmoothUI.theme.res.dividerStrokeColorDefault
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Label {
                        text: qsTr("Printer Preset")
                        font.pixelSize: 10
                        color: root.SmoothUI.theme.res.textFillColorTertiary
                    }
                    ComboBox {
                        id: printerPresetCombo
                        objectName: root.automationName(root.embedded
                                    ? "machineSelectorPrinterPresetCombo"
                                    : "sliceSettingsPrinterPresetCombo")
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        enabled: root.settingsEditingEnabled && root.machineVariantOptions.length > 0
                        model: root.machineVariantOptions
                        textRole: "name"
                        currentIndex: root.currentMachineVariantIndex()
                        onActivated: function(index) {
                            root.selectMachineAfterComboClose(index)
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Label {
                        text: qsTr("Filament Configuration")
                        font.pixelSize: 10
                        color: root.SmoothUI.theme.res.textFillColorTertiary
                    }
                    Button {
                        id: filamentSlotsButton
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        enabled: root.filamentSlots.length > 0
                        Accessible.name: qsTr("Open filament configuration")

                        contentItem: RowLayout {
                            spacing: 4

                            Repeater {
                                model: root.filamentSlots

                                Rectangle {
                                    required property var modelData
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12
                                    radius: 4
                                    color: modelData.color || "#808080"
                                    border.width: 1
                                    border.color: root.SmoothUI.theme.res.controlStrokeColorDefault
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.filamentSlotsSummary()
                                font: Typography.caption
                                color: root.SmoothUI.theme.res.textFillColorPrimary
                                elide: Text.ElideRight
                            }

                            Icon {
                                Layout.preferredWidth: 12
                                Layout.preferredHeight: 12
                                source: FluentIcons.graph_ChevronRightSmall
                                color: root.SmoothUI.theme.res.textFillColorSecondary
                            }
                        }
                        onClicked: {
                            root.workspaceIndex = 1
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Label {
                        text: qsTr("Process Preset")
                        font.pixelSize: 10
                        color: root.SmoothUI.theme.res.textFillColorTertiary
                    }
                    ComboBox {
                        id: processPresetCombo
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        enabled: root.settingsEditingEnabled && root.processPresets.length > 0
                        model: root.processPresets
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: root.indexByValue(root.processPresets, "id",
                                                        SliceSettingsBridge.selectedProcessPresetId)
                        onActivated: function(index) {
                            root.selectProcessAfterComboClose(index)
                        }
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 158
                    spacing: 1

                    Label {
                        text: qsTr("Build Plate")
                        font.pixelSize: 10
                        color: root.SmoothUI.theme.res.textFillColorTertiary
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        enabled: root.settingsEditingEnabled && root.plateDisplayOptions.length > 0
                        model: root.plateDisplayOptions
                        textRole: "name"
                        valueRole: "value"
                        currentIndex: root.indexByValue(root.plateDisplayOptions, "value", root.currentBedType)
                        onActivated: function(index) {
                            var value = root.plateDisplayOptions[index].value
                            SliceSettingsBridge.setSetting("curr_bed_type", value)
                            root.currentBedType = value
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: PrintWorkspaceStyle.titleBarHeight
            Layout.minimumHeight: PrintWorkspaceStyle.titleBarHeight
            Layout.maximumHeight: PrintWorkspaceStyle.titleBarHeight
            color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)
            z: 2

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.SmoothUI.theme.res.dividerStrokeColorDefault
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                TabBar {
                    id: workspaceTabs
                    currentIndex: root.workspaceIndex
                    onCurrentIndexChanged: {
                        root.workspaceIndex = currentIndex
                    }

                    TabButton {
                        text: qsTr("Process")
                        implicitHeight: PrintWorkspaceStyle.controlHeight
                        font: Typography.bodyStrong
                    }
                    TabButton {
                        text: qsTr("Filament")
                        implicitHeight: PrintWorkspaceStyle.controlHeight
                        font: Typography.bodyStrong
                    }
                    TabButton {
                        text: qsTr("Printers")
                        implicitHeight: PrintWorkspaceStyle.controlHeight
                        font: Typography.bodyStrong
                    }
                }

                TextField {
                    objectName: root.automationName(root.embedded
                                ? "machineSelectorSettingsSearchField"
                                : "sliceSettingsSearchField")
                    Layout.fillWidth: true
                    Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                    placeholderText: qsTr("Search Parameters")
                    text: root.searchText
                    selectByMouse: true

                    onTextChanged: {
                        root.searchText = text
                    }
                }

                Switch {
                    checked: root.showAdvancedMode
                    text: checked ? qsTr("Advanced") : qsTr("Normal")
                    onClicked: {
                        root.showAdvancedMode = !root.showAdvancedMode
                    }
                }

                CheckBox {
                    checked: root.showParamKeys
                    text: qsTr("Show Key Names")
                    onToggled: {
                        root.showParamKeys = checked
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: PrintWorkspaceStyle.space12
            z: 1

            Rectangle {
                Layout.preferredWidth: root.sidebarVisible ? 196 : 0
                Layout.fillHeight: true
                visible: root.sidebarVisible
                color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)
                radius: PrintWorkspaceStyle.controlRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Label {
                        text: qsTr("Settings Page")
                        font: Typography.bodyStrong
                        color: root.SmoothUI.theme.res.textFillColorPrimary
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.currentSidebarModel
                        currentIndex: root.currentSidebarIndex

                        delegate: Button {
                            required property int index
                            required property var modelData

                            width: ListView.view.width
                            height: 34
                            padding: 0
                            flat: true

                            background: Rectangle {
                                radius: root.SmoothUI.radius
                                color: parent.ListView.isCurrentItem
                                       ? root.SmoothUI.theme.res.subtleFillColorSecondary
                                       : "transparent"
                            }

                            contentItem: Label {
                                text: root.localizedSchemaText(modelData.title, modelData.id || "")
                                font: Typography.caption
                                color: root.SmoothUI.theme.res.textFillColorPrimary
                                leftPadding: 10
                                verticalAlignment: Text.AlignVCenter
                            }

                            onClicked: {
                                root.setCurrentSidebarIndex(index)
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 34 : 0
                    visible: root.globalSearchMode || root.workspaceIndex !== 1
                    radius: root.SmoothUI.radius
                    color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        text: root.globalSearchMode
                              ? qsTr("Search Results · %1 items").arg(root.globalSearchResultCount)
                              : (root.workspaceIndex === 1
                                 ? qsTr("Filament Configuration")
                                 : ((root.workspaceIndex === 0 ? qsTr("Process · ") : qsTr("Printer · "))
                                    + root.currentPageTitle))
                        font: Typography.bodyStrong
                        color: root.SmoothUI.theme.res.textFillColorPrimary
                    }
                }

                Rectangle {
                    id: filamentSlotPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? (root.perToolMaterials
                        ? 88 + 80 * Math.max.apply(null, root.toolGroups.map(function(group) { return Math.max(1, group.slots.length) }))
                        : 124) : 0
                    visible: !root.globalSearchMode && root.workspaceIndex === 1
                    radius: root.SmoothUI.radius
                    color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ComboBox {
                                objectName: root.automationName("fiberToolMappingCombo")
                                visible: root.editableToolMapping
                                enabled: root.settingsEditingEnabled
                                model: [qsTr("Automatic"), qsTr("Manual")]
                                currentIndex: String(SliceSettingsBridge.getSetting("filament_map_mode", ""))
                                    .indexOf("Manual") >= 0 ? 1 : 0
                                onActivated: function(index) {
                                    SliceSettingsBridge.setFilamentToolMapping(
                                        root.filamentSlots.map(function(slot) {
                                            return slot.physicalToolIndex + 1
                                        }), index === 0)
                                }
                            }

                            Button {
                                objectName: root.automationName("fiberSwapPhysicalToolsButton")
                                visible: root.editableToolMapping
                                enabled: root.settingsEditingEnabled && root.filamentSlots.length === 2
                                text: qsTr("Swap left and right")
                                onClicked: SliceSettingsBridge.setFilamentToolMapping(
                                    root.filamentSlots.map(function(slot) {
                                        return 2 - slot.physicalToolIndex
                                    }), false)
                            }

                            Label {
                                text: qsTr("%1 slots").arg(root.filamentSlots.length)
                                font: Typography.bodyStrong
                                color: root.SmoothUI.theme.res.textFillColorPrimary
                            }

                            Label {
                                Layout.fillWidth: true
                                text: SliceSettingsBridge.selectedMachine.variableFilamentSlots
                                      ? qsTr("Variable slots · %1/%2 active")
                                            .arg(root.filamentSlots.length)
                                            .arg(SliceSettingsBridge.selectedMachine.maxFilamentSlots || 1)
                                      : qsTr("Machine-fixed · %1 nozzles")
                                            .arg(SliceSettingsBridge.selectedMachine.physicalToolCount || 1)
                                font: Typography.caption
                                color: root.SmoothUI.theme.res.textFillColorSecondary
                                elide: Text.ElideRight
                            }

                            Button {
                                objectName: root.automationName(root.embedded
                                            ? "machineSelectorFilamentAddSlotButton"
                                            : "filamentAddSlotButton")
                                visible: SliceSettingsBridge.selectedMachine.variableFilamentSlots
                                         && !root.perToolMaterials
                                         && root.filamentSlots.length
                                            < (SliceSettingsBridge.selectedMachine.maxFilamentSlots || 1)
                                text: qsTr("Add Slot")
                                enabled: root.settingsEditingEnabled
                                Accessible.name: qsTr("Add filament slot")
                                onClicked: SliceSettingsBridge.setFilamentSlotCount(
                                               root.filamentSlots.length + 1)
                            }

                            Label {
                                visible: SliceSettingsBridge.configurationError.length > 0
                                text: SliceSettingsBridge.configurationError
                                font: Typography.caption
                                color: root.SmoothUI.theme.res.systemFillColorCritical
                                elide: Text.ElideRight
                                Layout.maximumWidth: 240
                            }
                        }

                        RowLayout {
                            id: filamentSlotRow
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 8

                            Repeater {
                                model: root.toolGroups

                                delegate: ColumnLayout {
                                    id: physicalToolGroup
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    RowLayout {
                                        visible: root.perToolMaterials
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            text: physicalToolGroup.modelData.slots.length > 0 ? root.physicalToolLabel(physicalToolGroup.modelData.slots[0]) : qsTr("Nozzle %1").arg(physicalToolGroup.modelData.physical)
                                            font: Typography.bodyStrong
                                        }
                                        Button {
                                            objectName: root.automationName((root.embedded ? "machineSelectorFilamentAddToToolButton_" : "filamentAddToToolButton_") + physicalToolGroup.modelData.physical)
                                            text: qsTr("Add Slot")
                                            enabled: root.settingsEditingEnabled && physicalToolGroup.modelData.slots.length < physicalToolGroup.modelData.capacity
                                            onClicked: SliceSettingsBridge.addFilamentToTool(physicalToolGroup.modelData.physical)
                                        }
                                    }
                                    GridLayout {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignTop
                                        columns: root.perToolMaterials ? 1 : Math.max(1, physicalToolGroup.modelData.slots.length)
                                        Repeater {
                                            model: physicalToolGroup.modelData.slots

                                            delegate: Rectangle {
                                                id: filamentSlotCard
                                                required property int index
                                                required property var modelData
                                                readonly property var compatiblePresets: modelData.compatiblePresets || []
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 74
                                                Layout.minimumWidth: 126
                                                radius: root.SmoothUI.radius
                                                color: PrintWorkspaceStyle.surface(root.SmoothUI.dark)

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 6
                                                    spacing: 4

                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 6

                                                        Rectangle {
                                                            Layout.preferredWidth: 22
                                                            Layout.preferredHeight: 22
                                                            radius: 5
                                                            color: filamentSlotCard.modelData.color || "#808080"
                                                            border.width: 1
                                                            border.color: root.SmoothUI.theme.res.controlStrokeColorDefault
                                                        }

                                                        Label {
                                                            Layout.fillWidth: true
                                                            text: (SliceSettingsBridge.selectedMachine.physicalToolCount > 1 ? root.physicalToolLabel(filamentSlotCard.modelData) + " · " : "") + qsTr("Slot %1 · %2").arg(filamentSlotCard.modelData.number).arg(filamentSlotCard.modelData.materialType || qsTr("Unknown"))
                                                            font: Typography.bodyStrong
                                                            color: root.SmoothUI.theme.res.textFillColorPrimary
                                                            elide: Text.ElideRight
                                                        }

                                                        IconButton {
                                                            Layout.preferredWidth: 28
                                                            Layout.preferredHeight: 28
                                                            flat: true
                                                            radius: 5
                                                            icon.name: FluentIcons.graph_Edit
                                                            icon.width: 13
                                                            icon.height: 13
                                                            Accessible.name: qsTr("Edit filament %1 color").arg(filamentSlotCard.modelData.number)
                                                            onClicked: filamentColorDialog.open()

                                                            ToolTip.visible: hovered
                                                            ToolTip.text: Accessible.name
                                                        }

                                                        ColorDialog {
                                                            id: filamentColorDialog
                                                            title: qsTr("Filament %1 Color").arg(filamentSlotCard.modelData.number)
                                                            selectedColor: filamentSlotCard.modelData.color || "#808080"
                                                            onAccepted: SliceSettingsBridge.setFilamentColor(filamentSlotCard.modelData.index, selectedColor)
                                                        }
                                                    }

                                                    ComboBox {
                                                        objectName: root.automationName((root.embedded ? "machineSelectorFilamentSlotPresetCombo_" : "filamentSlotPresetCombo_") + String(filamentSlotCard.modelData.number))
                                                        Layout.fillWidth: true
                                                        Layout.preferredHeight: 32
                                                        enabled: root.settingsEditingEnabled && filamentSlotCard.compatiblePresets.length > 0
                                                        model: filamentSlotCard.compatiblePresets
                                                        textRole: "name"
                                                        valueRole: "id"
                                                        currentIndex: root.indexByValue(filamentSlotCard.compatiblePresets, "id", filamentSlotCard.modelData.presetId)
                                                        onActivated: function (presetIndex) {
                                                            if (presetIndex < 0 || presetIndex >= filamentSlotCard.compatiblePresets.length)
                                                                return;
                                                            SliceSettingsBridge.selectFilamentPresetForSlot(filamentSlotCard.modelData.index, filamentSlotCard.compatiblePresets[presetIndex].id);
                                                        }

                                                        ToolTip {
                                                            visible: parent.hovered
                                                            text: parent.currentText
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ScrollView {
                    id: schemaScrollView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: schemaScrollView.availableWidth
                        spacing: 10

                        Repeater {
                            model: root.compactPanel || root.globalSearchMode ? [] : root.currentSchemaSections

                            delegate: SectionCard {
                                required property var modelData

                                sectionId: modelData.id || ""
                                title: root.localizedSchemaText(modelData.title, sectionId)

                                Repeater {
                                    model: modelData.rows || []

                                    delegate: SettingRow {
                                        id: settingRowDelegate
                                        required property var modelData

                                        title: root.localizedSchemaText(modelData.label, modelData.key || "")
                                        paramKey: modelData.key || ""
                                        autoInit: modelData.autoInit === undefined ? true : !!modelData.autoInit
                                        schemaEnabled: modelData.enabled === undefined ? true : !!modelData.enabled
                                        configRevision: root.schemaRevisionToken
                                        providedControl: pageSettingInput.editorControl
                                        providedControlKind: pageSettingInput.editorKind

                                        SchemaSettingInput {
                                            id: pageSettingInput
                                            definition: modelData
                                        }
                                    }
                                }
                            }
                        }

                        Repeater {
                            model: !root.compactPanel && root.globalSearchMode ? root.globalSearchResults : []

                            delegate: SearchResultCard {}
                        }

                        Label {
                            visible: root.globalSearchMode
                                     ? (root.globalSearchResultCount === 0)
                                     : (root.currentSchemaSections.length === 0)
                            Layout.fillWidth: true
                            text: root.globalSearchMode
                                  ? qsTr("No matching parameters found")
                                  : (root.useSchemaDriven
                                     ? qsTr("No parameters on this page")
                                     : qsTr("Parameter schema not loaded"))
                            horizontalAlignment: Text.AlignHCenter
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                            padding: 12
                        }

                        Item { Layout.preferredHeight: 8 }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            bottomLeftRadius: PrintWorkspaceStyle.menuRadius
            bottomRightRadius: PrintWorkspaceStyle.menuRadius
            color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Button {
                    text: qsTr("Restore Current Preset")
                    enabled: root.settingsEditingEnabled
                    Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                    onClicked: {
                        SliceSettingsBridge.resetToDefaults()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: SliceSettingsBridge.hasPendingReslice
                          ? qsTr("Settings changed; reslicing is required")
                          : qsTr("Current configuration applied")
                    font: Typography.caption
                    color: SliceSettingsBridge.hasPendingReslice
                           ? root.SmoothUI.theme.res.systemFillColorCaution
                           : root.SmoothUI.theme.res.textFillColorSecondary
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }

                Button {
                    visible: SliceSettingsBridge.hasPendingReslice && root.canReslice
                    text: qsTr("Reslice")
                    Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                    highlighted: true
                    onClicked: {
                        root.triggerReslice()
                        root.closeRequested()
                    }
                }

                Button {
                    text: qsTr("Done")
                    Layout.preferredWidth: 76
                    Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                    onClicked: root.closeRequested()
                }
            }
        }
    }

    Component.onCompleted: {
        refreshContext()
        syncPresetComboSelection()
    }

    Connections {
        target: SliceSettingsBridge

        function onMachineSelectionChanged() {
            root.refreshContext()
            root.syncPresetComboSelection()
        }

        function onPresetSelectionChanged() {
            root.syncPresetComboSelection()
        }

        function onSettingChanged(key, value) {
            if (key === "curr_bed_type")
                root.currentBedType = String(value)
        }
    }


    ColumnLayout {
        anchors.fill: parent
        visible: root.compactPanel
        enabled: root.compactPanel
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 8
            spacing: 10
            TextField {
                objectName: root.compactPanel ? "print.configuration.search" : ""
                Layout.fillWidth: true
                implicitHeight: 31
                font.pixelSize: 11
                placeholderText: qsTr("Search Parameters")
                leading: Icon { width: 12; height: 12; source: FluentIcons.graph_Search; color: root.SmoothUI.theme.res.textFillColorTertiary }
                text: root.searchText
                selectByMouse: true
                onTextEdited: root.searchText = text
            }
            CheckBox {
                objectName: root.compactPanel ? "print.configuration.advanced" : ""
                text: qsTr("Advanced")
                font.pixelSize: 10
                indicator.width: 12; indicator.height: 12
                spacing: 5; leftPadding: 0; rightPadding: 0
                checked: root.showAdvancedMode
                onToggled: root.showAdvancedMode = checked
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 8
            spacing: 2
            Repeater {
                model: root.compactPanel ? root.compactPages : []
                delegate: Button {
                    required property int index
                    required property var modelData
                    objectName: "print.configuration.category." + modelData.id
                    height: 27
                    implicitWidth: implicitContentWidth + 16
                    horizontalPadding: 8
                    flat: true
                    font.pixelSize: 11
                    text: modelData.title
                    highlighted: false
                    background: Rectangle {
                        radius: 4
                        color: modelData.id === root.compactCategory
                               ? Qt.alpha(root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark), 0.09) : "transparent"
                    }
                    contentItem: Label {
                        text: modelData.title
                        font.pixelSize: 11
                        color: modelData.id === root.compactCategory
                               ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
                               : root.SmoothUI.theme.res.textFillColorSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: root.compactCategory = modelData.id
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            height: 1
            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
        }
        ScrollView {
            id: compactScroll
            objectName: root.compactPanel ? "print.configuration.parameters" : ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            leftPadding: 16
            rightPadding: 16
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: compactScroll.availableWidth
                spacing: 0
                Repeater {
                    model: root.compactPanel && !root.globalSearchMode ? root.compactSections : []
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            Layout.bottomMargin: 6
                            text: modelData.title
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            color: root.SmoothUI.theme.res.textFillColorSecondary
                        }
                        Repeater {
                            model: modelData.rows || []
                            delegate: SettingRow {
                                required property var modelData
                                title: root.localizedSchemaText(modelData.label, modelData.key)
                                paramKey: modelData.key
                                schemaEnabled: modelData.enabled === undefined || !!modelData.enabled
                                autoInit: false
                                configRevision: root.schemaRevisionToken
                                editorObjectNamePrefix: "print.configuration.editor."
                                providedControl: compactInput.editorControl
                                providedControlKind: compactInput.editorKind
                                SchemaSettingInput { id: compactInput; definition: modelData }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            height: 1
                            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
                        }
                    }
                }
                Repeater {
                    model: root.compactPanel && root.globalSearchMode
                           ? root.globalSearchResults.filter(function(row) {
                               return row.tab === "process" && (root.fiberAvailable || root.compactSchemaCategory(row.page) !== "fiber")
                           }) : []
                    delegate: SearchResultCard {}
                }
            }
        }
    }

    component SearchResultCard: Rectangle {
        required property var modelData

        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        readonly property bool focused: String(modelData.key || "")
                                        === root.focusedSettingKey
        color: focused
               ? Qt.alpha(root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark), 0.10)
               : Qt.rgba(0, 0, 0, 0.06)
        radius: root.SmoothUI.radius
        border.color: focused
                      ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
                      : root.SmoothUI.theme.res.cardStrokeColorDefault
        border.width: focused ? 2 : 1
        implicitHeight: searchCardLayout.implicitHeight + 12

        ColumnLayout {
            id: searchCardLayout
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            spacing: 4

            Label {
                Layout.fillWidth: true
                text: root.displayTabTitle(modelData.tab)
                      + " / "
                      + root.localizedSchemaText(modelData.pageTitle, modelData.page || "")
                      + " / "
                      + root.localizedSchemaText(modelData.sectionTitle, modelData.section || "")
                color: root.SmoothUI.theme.res.textFillColorSecondary
                font.pixelSize: Theme.captionFontSize
                elide: Text.ElideRight
            }

            SettingRow {
                id: searchSettingRow
                Layout.fillWidth: true
                editorObjectNamePrefix: root.automationName("sliceSettingSearchEditor_")
                title: root.localizedSchemaText(modelData.label, modelData.key || "")
                paramKey: modelData.key || ""
                autoInit: modelData.autoInit === undefined ? true : !!modelData.autoInit
                schemaEnabled: modelData.enabled === undefined ? true : !!modelData.enabled
                configRevision: root.schemaRevisionToken
                providedControl: searchSettingInput.editorControl
                providedControlKind: searchSettingInput.editorKind

                SchemaSettingInput {
                    id: searchSettingInput
                    definition: modelData
                }
            }
        }
    }

    component SectionCard: Rectangle {
        property string title: ""
        property string sectionId: ""
        default property alias content: bodyColumn.data

        Layout.fillWidth: true
        Layout.preferredHeight: visible ? implicitHeight : 0
        Layout.maximumHeight: visible ? 16777215 : 0
        color: "transparent"
        radius: 0
        border.width: 0
        implicitHeight: sectionLayout.implicitHeight + 12

        ColumnLayout {
            id: sectionLayout
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: title
                font: Typography.bodyStrong
                color: root.SmoothUI.theme.res.textFillColorPrimary
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: root.SmoothUI.theme.res.dividerStrokeColorDefault
            }

            ColumnLayout {
                id: bodyColumn
                Layout.fillWidth: true
                spacing: 2
            }
        }
    }

    component SettingRow: Item {
        id: settingRow

        property string title: ""
        readonly property bool modified: SliceSettingsBridge.modifiedSettingKeys.indexOf(paramKey) >= 0
        property string paramKey: ""
        property bool autoInit: true
        property bool schemaEnabled: true
        property int configRevision: 0
        property bool syncingBridge: false
        property var providedControl: null
        property string providedControlKind: ""
        property string editorObjectNamePrefix: root.automationName("sliceSettingEditor_")
        property var primaryControl: null
        property string primaryControlKind: ""
        property bool controlConnected: false
        readonly property bool multilineControl: providedControlKind === "multiline"
                                                 || primaryControlKind === "multiline"

        default property alias controls: controlRow.data

        Layout.fillWidth: true
        Layout.preferredHeight: visible ? implicitHeight : 0
        Layout.maximumHeight: visible ? 16777215 : 0
        implicitHeight: visible ? Math.max(root.compactPanel ? 35 : 44, settingLayout.implicitHeight) : 0
        opacity: schemaEnabled ? 1.0 : 0.55

        function comboModelValue(combo, index) {
            if (!combo || index < 0) {
                return ""
            }

            // Schema enum editors deliberately keep the stable libslicer
            // value separate from the translated label shown to the user.
            // ComboBox.model may be exposed as a QQml model proxy rather than
            // a JavaScript Array, so inspecting model and then falling back to
            // textAt() can accidentally submit the translated label.
            if (combo.optionModel !== undefined && combo.optionModel !== null) {
                var optionItem = combo.optionModel[index]
                if (optionItem && optionItem.value !== undefined) {
                    return optionItem.value
                }
            }

            if (combo.model === undefined || combo.model === null) {
                return combo.currentText
            }

            if (Array.isArray(combo.model)) {
                var arrayItem = combo.model[index]
                if (arrayItem && arrayItem.value !== undefined) {
                    return arrayItem.value
                }
                return arrayItem
            }

            if (combo.model.get) {
                var item = combo.model.get(index)
                if (item && item.value !== undefined) {
                    return item.value
                }
                if (item && item.text !== undefined) {
                    return item.text
                }
            }

            if (combo.valueAt) {
                var roleValue = combo.valueAt(index)
                if (roleValue !== undefined && roleValue !== null) {
                    return roleValue
                }
            }

            if (combo.textAt) {
                return combo.textAt(index)
            }

            return combo.currentText
        }

        function extractControlValue(control) {
            if (!control) {
                return undefined
            }

            if (primaryControlKind === "switch") {
                return control.checked
            }
            if (primaryControlKind === "combo") {
                return comboModelValue(control, control.currentIndex)
            }
            if (primaryControlKind === "slider" || primaryControlKind === "number") {
                return control.value
            }
            if (primaryControlKind === "text" || primaryControlKind === "multiline") {
                return control.text
            }

            return undefined
        }

        function applyControlValue(control, value) {
            if (!control) {
                return
            }

            syncingBridge = true

            if (primaryControlKind === "switch") {
                control.checked = !!value
                syncingBridge = false
                return
            }

            if (primaryControlKind === "combo") {
                if (typeof value === "number" && value >= 0 && value < control.count) {
                    control.currentIndex = value
                    syncingBridge = false
                    return
                }

                var target = String(value)
                for (var i = 0; i < control.count; ++i) {
                    if (String(comboModelValue(control, i)) === target) {
                        control.currentIndex = i
                        syncingBridge = false
                        return
                    }
                }

                syncingBridge = false
                return
            }

            if (primaryControlKind === "slider" || primaryControlKind === "number") {
                var numberValue = Number(value)
                if (!isNaN(numberValue)) {
                    control.value = numberValue
                }
                syncingBridge = false
                return
            }

            if (primaryControlKind === "text" || primaryControlKind === "multiline") {
                control.text = String(value)
            }

            syncingBridge = false
        }

        function commitToBridge() {
            if (syncingBridge || !primaryControl || !paramKey) {
                return
            }

            var value = extractControlValue(primaryControl)
            if (value !== undefined) {
                SliceSettingsBridge.setSetting(paramKey, value)
            }
        }

        function syncFromBridge() {
            if (!primaryControl || !paramKey) {
                return
            }

            var currentValue = extractControlValue(primaryControl)
            if (!SliceSettingsBridge.hasSetting(paramKey)) {
                if (autoInit && currentValue !== undefined) {
                    SliceSettingsBridge.ensureSetting(paramKey, currentValue)
                }
                return
            }

            var settingValue = SliceSettingsBridge.getSetting(paramKey, currentValue)
            applyControlValue(primaryControl, settingValue)
        }

        function connectControlSignal() {
            if (controlConnected || !primaryControl) {
                return
            }

            if (primaryControlKind === "switch") {
                primaryControl.checkedChanged.connect(commitToBridge)
                controlConnected = true
                return
            }
            if (primaryControlKind === "slider" || primaryControlKind === "number") {
                primaryControl.valueChanged.connect(commitToBridge)
                controlConnected = true
                return
            }
            if (primaryControlKind === "combo") {
                primaryControl.currentIndexChanged.connect(commitToBridge)
                controlConnected = true
                return
            }
            if (primaryControlKind === "text") {
                primaryControl.editingFinished.connect(commitToBridge)
                controlConnected = true
                return
            }
            if (primaryControlKind === "multiline") {
                primaryControl.textChanged.connect(commitToBridge)
                controlConnected = true
            }
        }

        function bindLoadedEditor(resolvedControl, editorKind) {
            if (!resolvedControl) {
                return
            }
            if (resolvedControl !== primaryControl || editorKind !== primaryControlKind) {
                primaryControl = resolvedControl
                primaryControlKind = editorKind || ""
                controlConnected = false
            }
            if (paramKey)
                resolvedControl.objectName = editorObjectNamePrefix + paramKey
            connectControlSignal()
            syncFromBridge()
        }

        function bindCurrentControlAndSync() {
            if (!primaryControl) {
                return
            }
            bindLoadedEditor(primaryControl, primaryControlKind)
        }

        Component.onCompleted: {
            bindLoadedEditor(providedControl, providedControlKind)
            controlSyncTimer.restart()
        }

        onProvidedControlChanged: {
            bindLoadedEditor(providedControl, providedControlKind)
        }

        onProvidedControlKindChanged: {
            bindLoadedEditor(providedControl, providedControlKind)
        }

        onParamKeyChanged: {
            controlSyncTimer.restart()
        }

        onConfigRevisionChanged: {
            controlSyncTimer.restart()
        }

        Connections {
            target: SliceSettingsBridge

            function onSettingsChanged() {
                controlSyncTimer.restart()
            }
        }

        Timer {
            id: controlSyncTimer
            interval: 0
            repeat: false
            onTriggered: settingRow.bindCurrentControlAndSync()
        }

        GridLayout {
            id: settingLayout
            anchors.fill: parent
            columns: settingRow.multilineControl ? 1 : 2
            columnSpacing: 8
            rowSpacing: settingRow.multilineControl ? 6 : 0

            ColumnLayout {
                Layout.minimumWidth: 0
                Layout.fillWidth: root.compactPanel || settingRow.multilineControl
                Layout.preferredWidth: root.compactPanel || settingRow.multilineControl
                                       ? -1
                                       : Math.max(140, Math.min(180, root.width * 0.24))
                Layout.alignment: root.compactPanel && !settingRow.multilineControl ? Qt.AlignVCenter : Qt.AlignTop
                spacing: 1

                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.compactPanel && settingRow.modified ? "• " + title : title
                    color: root.compactPanel ? root.SmoothUI.theme.res.textFillColorSecondary : root.SmoothUI.theme.res.textFillColorPrimary
                    font: root.compactPanel ? root.compactCaptionFont : Typography.caption
                    elide: Text.ElideRight
                }

                Label {
                    visible: root.showParamKeys
                    text: paramKey
                    color: root.SmoothUI.theme.res.textFillColorTertiary
                    font.pixelSize: Theme.captionFontSize
                    elide: Text.ElideRight
                }
            }

            Item {
                id: controlHost
                Layout.fillWidth: settingRow.multilineControl
                Layout.preferredWidth: settingRow.multilineControl ? -1 : (root.compactPanel ? 117 : 280)
                Layout.preferredHeight: implicitHeight
                Layout.alignment: settingRow.multilineControl
                                  ? Qt.AlignLeft | Qt.AlignTop
                                  : Qt.AlignRight | Qt.AlignVCenter
                enabled: root.settingsEditingEnabled && settingRow.schemaEnabled
                implicitWidth: root.compactPanel ? 117 : 280
                implicitHeight: controlRow.implicitHeight

                RowLayout {
                    id: controlRow
                    anchors.fill: parent
                    spacing: 0

                    Item {
                        visible: !settingRow.multilineControl
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }

    component SchemaSettingInput: Item {
        id: schemaSettingInput
        property var definition: ({})
        readonly property string editorKind: {
            var control = String(definition.control || "").toLowerCase()
            var type = String(definition.type || "").toLowerCase()
            if (control === "switch" || type === "bool")
                return "switch"
            if (control === "combo" || type === "enum")
                return "combo"
            if (control === "slider")
                return "slider"
            if (control === "number" || type === "int" || type === "float")
                return "number"
            if (control === "multiline" || type === "multiline")
                return "multiline"
            return "text"
        }
        readonly property var editorControl: fieldLoader.item
                                             ? fieldLoader.item.editorControl
                                             : null

        Layout.fillWidth: editorKind === "multiline"
        implicitWidth: root.compactPanel ? (editorKind === "multiline" ? root.width - 32 : 117) : (editorKind === "multiline" ? 280 : fieldLoader.implicitWidth)
        implicitHeight: fieldLoader.implicitHeight

        function resolveInitialValue(fallbackValue) {
            if (definition && definition.value !== undefined && definition.value !== null) {
                return definition.value
            }
            if (definition && definition.defaultValue !== undefined && definition.defaultValue !== null) {
                return definition.defaultValue
            }
            if (definition && definition.default !== undefined && definition.default !== null) {
                return definition.default
            }
            return fallbackValue
        }

        function enumCurrentIndex(comboModel, targetValue) {
            if (!comboModel) {
                return 0
            }

            for (var i = 0; i < comboModel.length; ++i) {
                var option = comboModel[i]
                var optionValue = option && option.value !== undefined ? option.value : option
                if (String(optionValue) === String(targetValue)) {
                    return i
                }
            }
            return 0
        }

        Loader {
            id: fieldLoader
            width: schemaSettingInput.editorKind === "multiline"
                   ? schemaSettingInput.width
                   : (root.compactPanel ? 117 : implicitWidth)

            sourceComponent: {
                var control = String(definition.control || "").toLowerCase()
                var type = String(definition.type || "").toLowerCase()

                if (control === "switch" || type === "bool") {
                    return root.compactPanel ? schemaCompactBoolEditor : schemaSwitchEditor
                }
                if (control === "combo" || type === "enum") {
                    return schemaComboEditor
                }
                if (control === "slider") {
                    return schemaSliderEditor
                }
                if (control === "number" || type === "int" || type === "float") {
                    return schemaNumberEditor
                }
                if (control === "multiline" || type === "multiline") {
                    return schemaMultilineEditor
                }
                return schemaTextEditor
            }
        }

        Component {
            id: schemaCompactBoolEditor
            CheckBox {
                id: compactCheck
                readonly property var editorControl: compactCheck
                implicitHeight: 28
                checked: !!resolveInitialValue(false)
            }
        }

        Component {
            id: schemaSwitchEditor

            Switch {
                id: schemaSwitchControl
                readonly property var editorControl: schemaSwitchControl
                objectName: root.automationName("sliceSettingEditor_" + String(definition.key || ""))
                implicitHeight: root.compactPanel ? 28 : 36
                checked: !!resolveInitialValue(false)
            }
        }

        Component {
            id: schemaComboEditor

            ComboBox {
                id: schemaComboControl
                readonly property var editorControl: schemaComboControl
                objectName: root.automationName("sliceSettingEditor_" + String(definition.key || ""))
                property var optionModel: root.localizedEnumOptions(definition.options || [])
                width: root.compactPanel ? 117 : 170
                implicitHeight: root.compactPanel ? 28 : 36
                font.pixelSize: root.compactPanel ? 11 : Theme.bodyFontSize
                model: optionModel
                textRole: "label"
                valueRole: "value"
                currentIndex: enumCurrentIndex(optionModel,
                                               resolveInitialValue(optionModel.length > 0 ? optionModel[0].value : ""))
            }
        }

        Component {
            id: schemaSliderEditor

            RowLayout {
                readonly property var editorControl: sliderInput
                property bool intLike: String(definition.type || "").toLowerCase() === "int"
                property real precision: definition.decimals !== undefined ? Number(definition.decimals) : (intLike ? 0 : 2)

                Slider {
                    id: sliderInput
                    objectName: root.automationName("sliceSettingEditor_" + String(definition.key || ""))
                    from: definition.min !== undefined ? Number(definition.min) : 0
                    to: definition.max !== undefined ? Number(definition.max) : 100
                    value: Number(resolveInitialValue(from))
                    stepSize: definition.step !== undefined ? Number(definition.step) : 1
                    Layout.preferredWidth: root.compactPanel ? 60 : 170
                }

                ValueTag {
                    text: intLike ? String(Math.round(sliderInput.value))
                                  : Number(sliderInput.value).toFixed(precision)
                }

                UnitLabel {
                    visible: !!definition.unit
                    text: root.translateSchemaBaseText(definition.unit || "")
                }
            }
        }

        Component {
            id: schemaNumberEditor
            Item {
                readonly property var editorControl: numberEditorInput
                implicitWidth: root.compactPanel ? 117 : numberRow.implicitWidth
                implicitHeight: root.compactPanel ? 28 : numberRow.implicitHeight
                Rectangle {
                    anchors.fill: parent
                    visible: root.compactPanel
                    radius: 4
                    color: root.SmoothUI.dark ? root.SmoothUI.theme.res.controlFillColorDefault : "#ffffff"
                    border.width: 1
                    border.color: numberEditorInput.activeFocus
                        ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
                        : (root.SmoothUI.dark ? root.SmoothUI.theme.res.controlStrokeColorDefault : "#e0e5ec")
                }
                RowLayout {
                    id: numberRow
                    anchors.fill: parent
                    spacing: root.compactPanel ? 0 : 5
                    NumberInput {
                        id: numberEditorInput
                        objectName: root.automationName("sliceSettingEditor_" + String(definition.key || ""))
                        Layout.fillWidth: root.compactPanel
                        background.opacity: root.compactPanel ? 0 : 1
                        value: Number(resolveInitialValue(0))
                        minValue: definition.min !== undefined ? Number(definition.min) : -99999
                        maxValue: definition.max !== undefined ? Number(definition.max) : 99999
                        decimals: definition.decimals !== undefined ? Number(definition.decimals)
                            : (String(definition.type || "").toLowerCase() === "int" ? 0 : 2)
                    }
                    UnitLabel {
                        visible: !!definition.unit
                        Layout.rightMargin: root.compactPanel ? 7 : 0
                        text: root.translateSchemaBaseText(definition.unit || "")
                    }
                }
            }
        }

        Component {
            id: schemaTextEditor

            TextField {
                id: schemaTextControl
                readonly property var editorControl: schemaTextControl
                objectName: root.automationName("sliceSettingEditor_" + String(definition.key || ""))
                width: root.compactPanel ? 117 : 190
                implicitHeight: root.compactPanel ? 28 : 36
                font.pixelSize: root.compactPanel ? 11 : Theme.bodyFontSize
                text: String(resolveInitialValue(""))
                selectByMouse: true
            }
        }

        Component {
            id: schemaMultilineEditor

            MultilineInput {
                id: schemaMultilineControl
                readonly property var editorControl: schemaMultilineControl
                objectName: root.automationName("sliceSettingEditor_" + String(definition.key || ""))
                implicitWidth: root.compactPanel ? Math.max(100, root.width - 32) : 280
                implicitHeight: 96
                text: String(resolveInitialValue(""))
            }
        }
    }

    component NumberInput: TextField {
        id: numberInput
        property real value: 0
        property real minValue: -99999
        property real maxValue: 99999
        property int decimals: 2

        implicitWidth: 84
        implicitHeight: root.compactPanel ? 28 : 30
        font.pixelSize: root.compactPanel ? 11 : Theme.bodyFontSize
        horizontalAlignment: Text.AlignRight
        selectByMouse: true
        text: formattedValue()
        function formattedValue() {
            const full = Number(value).toFixed(decimals)
            if (!root.compactPanel || decimals <= 2) return full
            let end = full.length
            while (end > full.indexOf(".") + 3 && full[end - 1] === "0") --end
            return full.slice(0, end)
        }

        validator: DoubleValidator {
            bottom: numberInput.minValue
            top: numberInput.maxValue
            decimals: numberInput.decimals
        }

        onEditingFinished: {
            var parsed = parseFloat(text)
            if (!isNaN(parsed)) {
                parsed = Math.max(minValue, Math.min(maxValue, parsed))
                value = parsed
            }
            text = Qt.binding(function() { return numberInput.formattedValue() })
        }
    }

    component UnitLabel: Label {
        color: root.SmoothUI.theme.res.textFillColorSecondary
        font: root.compactPanel ? Qt.font({family: Typography.caption.family, pixelSize: 9}) : Typography.caption
    }

    component ValueTag: Rectangle {
        property string text: ""
        implicitWidth: valueText.implicitWidth + 10
        implicitHeight: 24
        radius: 4
        color: Colors.withOpacity(root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark), 0.14)
        border.color: Colors.withOpacity(root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark), 0.35)
        border.width: 1

        Label {
            id: valueText
            anchors.centerIn: parent
            text: parent.text
            color: root.SmoothUI.theme.res.textFillColorPrimary
            font: Typography.caption
        }
    }

    component MultilineInput: TextArea {
        wrapMode: TextArea.Wrap
        selectByMouse: true
        padding: Theme.controlPadding
    }
}
