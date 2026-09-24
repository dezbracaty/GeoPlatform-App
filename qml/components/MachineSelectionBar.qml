import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0

Button {
    id: root
    objectName: "machineSelectorButton"

    SmoothUI.theme: Theme.of(root)

    property var machineOptions: SliceSettingsBridge.machineModels()
    // popupHost constrains the popover to the 3D viewport. popupAnchor includes
    // both the slicing-machine bar and the physical-device status bar so the
    // popover never obscures either context.
    property Item popupHost: null
    property Item popupAnchor: root
    property Item popupVerticalAnchor: null
    property bool compact: false
    property bool embedded: false
    property bool popupAbove: false
    // Chosen by the host, independent of the width calculated from this content.
    property bool condensedPresetSummary: false
    readonly property var selectedMachine: SliceSettingsBridge.selectedMachine
    readonly property var processOptions: SliceSettingsBridge.compatibleProcessPresets
    readonly property var filamentOptions: SliceSettingsBridge.compatibleFilamentPresets
    readonly property var filamentSlots: SliceSettingsBridge.filamentSlots
    readonly property var bedTypeOptions: SliceSettingsBridge.buildPlateOptions()
    readonly property bool selectionEnabled: !TaskStateNotifier.isBusy
    readonly property var bedTypeDisplayOptions: {
        var options = []
        for (var index = 0; index < bedTypeOptions.length; ++index) {
            options.push({
                "value": bedTypeOptions[index].value,
                "label": bedTypeLabel(bedTypeOptions[index].value),
                "imageUrl": bedTypeOptions[index].imageUrl
            })
        }
        return options
    }

    property int draftMachineIndex: 0
    property int draftNozzleIndex: 0
    property int draftBedTypeIndex: 0
    property string currentBedType: ""
    property string applyError: ""
    property bool detailedSettingsVisible: false
    signal popupOpening()

    readonly property bool draftMachineChanged: {
        var machine = currentDraftMachine()
        var variant = currentDraftVariant()
        return machine !== null && variant !== null
                && (machine.id !== SliceSettingsBridge.selectedMachineModelId
                    || variant.id !== SliceSettingsBridge.selectedMachineVariantId)
    }
    readonly property bool draftBedTypeChanged: {
        var plate = currentDraftPlate()
        return plate !== null && plate.value !== currentBedType
    }
    readonly property bool hasDraftChanges: draftMachineChanged || draftBedTypeChanged
    readonly property bool hasValidDraft: currentDraftMachine() !== null
                                          && currentDraftVariant() !== null
                                          && currentDraftPlate() !== null
    readonly property string unavailableMessage: {
        if (!machineOptions || machineOptions.length === 0)
            return qsTr("No slicing machine presets available")
        if (currentDraftMachine() === null)
            return qsTr("The current machine is unavailable")
        if (currentDraftVariant() === null)
            return qsTr("No nozzles are available for the current machine")
        if (!bedTypeDisplayOptions || bedTypeDisplayOptions.length === 0)
            return qsTr("No build plates available")
        if (currentDraftPlate() === null)
            return qsTr("The current build plate is unavailable")
        return ""
    }

    implicitWidth: compact ? contentRow.implicitWidth + leftPadding + rightPadding
                           : PrintWorkspaceStyle.machineSelectorWidth
    readonly property real minimumContentWidth: contentRow.Layout.minimumWidth + leftPadding + rightPadding
    implicitHeight: compact ? PrintWorkspaceStyle.controlHeight
                            : PrintWorkspaceStyle.machineSelectorHeight
    padding: 0
    leftPadding: 8
    rightPadding: compact ? 8 : 12
    flat: true
    enabled: root.selectionEnabled
    activeFocusOnTab: root.selectionEnabled

    function indexByValue(items, key, value) {
        if (!items)
            return -1
        for (var index = 0; index < items.length; ++index) {
            if (items[index] && items[index][key] === value)
                return index
        }
        return -1
    }

    function filamentSummary() {
        if (!filamentSlots || filamentSlots.length === 0)
            return SliceSettingsBridge.selectedFilamentPresetName || qsTr("Not Selected")
        var firstName = filamentSlots[0].materialType
                        || filamentSlots[0].presetName || filamentSlots[0].presetId
        var allSame = true
        for (var index = 1; index < filamentSlots.length; ++index) {
            var currentName = filamentSlots[index].materialType
                              || filamentSlots[index].presetName || filamentSlots[index].presetId
            if (currentName !== firstName) {
                allSame = false
                break
            }
        }
        if (allSame)
            return filamentSlots.length > 1
                    ? qsTr("%1× %2").arg(filamentSlots.length).arg(firstName)
                    : firstName
        var names = []
        for (var filamentIndex = 0; filamentIndex < filamentSlots.length; ++filamentIndex)
            names.push(filamentSlots[filamentIndex].materialType
                       || filamentSlots[filamentIndex].presetName
                       || filamentSlots[filamentIndex].presetId)
        return names.join(", ")
    }

    function currentDraftMachine() {
        if (!machineOptions || draftMachineIndex < 0 || draftMachineIndex >= machineOptions.length)
            return null
        return machineOptions[draftMachineIndex]
    }

    function currentDraftVariant() {
        var machine = currentDraftMachine()
        if (machine === null || !machine.variants
                || draftNozzleIndex < 0 || draftNozzleIndex >= machine.variants.length)
            return null
        return machine.variants[draftNozzleIndex]
    }

    function currentDraftPlate() {
        if (!bedTypeDisplayOptions || draftBedTypeIndex < 0
                || draftBedTypeIndex >= bedTypeDisplayOptions.length)
            return null
        return bedTypeDisplayOptions[draftBedTypeIndex]
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

    function refreshDraft() {
        machineOptions = SliceSettingsBridge.machineModels()
        draftMachineIndex = indexByValue(machineOptions, "id", SliceSettingsBridge.selectedMachineModelId)
        var machine = currentDraftMachine()
        draftNozzleIndex = machine !== null
                           ? indexByValue(machine.variants || [], "id",
                                          SliceSettingsBridge.selectedMachineVariantId)
                           : -1
        var activeBedType = SliceSettingsBridge.getSetting("curr_bed_type")
        currentBedType = activeBedType === undefined || activeBedType === null
                         ? "" : String(activeBedType)
        draftBedTypeIndex = indexByValue(bedTypeDisplayOptions, "value", currentBedType)
    }

    function applyDraftSelection(closeAfter) {
        var shouldClose = closeAfter === undefined ? true : closeAfter
        applyError = ""
        if (!selectionEnabled) {
            applyError = qsTr("Machine settings cannot be changed while slicing")
            return false
        }
        var machine = currentDraftMachine()
        var variant = currentDraftVariant()
        var plate = currentDraftPlate()
        if (machine === null || variant === null || plate === null) {
            applyError = unavailableMessage
            return false
        }

        if (!hasDraftChanges) {
            if (shouldClose)
                selectorPopup.close()
            return true
        }

        var machineChanged = draftMachineChanged
        var requestedBedType = plate.value
        if (machineChanged
                && !SliceSettingsBridge.selectMachine(machine.id, variant.id)) {
            applyError = qsTr("Failed to apply machine configuration. Check the application log.")
            return false
        }

        if (machineChanged || requestedBedType !== currentBedType) {
            SliceSettingsBridge.setSetting("curr_bed_type", requestedBedType)
            var appliedBedType = SliceSettingsBridge.getSetting("curr_bed_type")
            if (appliedBedType === undefined || appliedBedType === null
                    || String(appliedBedType) !== requestedBedType) {
                applyError = machineChanged
                             ? qsTr("Machine changed, but the build plate could not be applied. Check the application log.")
                             : qsTr("Failed to apply build plate. Check the application log.")
                return false
            }
        }

        if (shouldClose)
            selectorPopup.close()
        else
            refreshDraft()
        return true
    }

    function openDetailedSettings() {
        applyError = ""
        detailedSettingsVisible = true
        if (!selectorPopup.opened) {
            popupOpening()
            selectorPopup.open()
        }
        Qt.callLater(function() {
            detailedSettingsPage.forceActiveFocus()
        })
        return true
    }

    function openFilamentSettings() {
        detailedSettingsPage.workspaceIndex = 1
        return openDetailedSettings()
    }

    function returnToQuickSettings() {
        detailedSettingsVisible = false
        refreshDraft()
        Qt.callLater(function() {
            if (machineCombo.enabled)
                machineCombo.forceActiveFocus()
        })
    }

    function closePopup() {
        selectorPopup.close()
    }

    function syncPresetComboSelection() {
        processPresetCombo.currentIndex = root.indexByValue(
                    root.processOptions, "id",
                    SliceSettingsBridge.selectedProcessPresetId)
    }

    function selectProcessPresetAfterComboClose(index) {
        if (index < 0 || index >= processOptions.length)
            return
        var presetId = processOptions[index].id
        root.applyError = ""
        if (!SliceSettingsBridge.selectProcessPreset(presetId))
            root.applyError = qsTr("Failed to apply process preset")
        Qt.callLater(function() {
            root.syncPresetComboSelection()
        })
    }

    function popupAnchorPosition() {
        var anchorItem = popupAnchor ? popupAnchor : root
        return anchorItem.mapToItem(selectorPopup.parent, 0,
                                    anchorItem.height + PrintWorkspaceStyle.machinePopoverOffset)
    }

    function togglePopup() {
        if (!selectionEnabled)
            return
        if (selectorPopup.opened) {
            selectorPopup.close()
            return
        }
        detailedSettingsVisible = false
        popupOpening()
        selectorPopup.open()
    }

    Component.onCompleted: {
        refreshDraft()
    }

    background: Rectangle {
        anchors.fill: parent
        radius: root.embedded ? PrintWorkspaceStyle.controlRadius : 10
        color: root.embedded
               ? (root.hovered || selectorPopup.opened
                  ? root.SmoothUI.theme.res.subtleFillColorSecondary
                  : "transparent")
               : root.SmoothUI.theme.res.solidBackgroundFillColorBase
        border.width: root.embedded ? 0 : 1
        border.color: root.hovered || root.activeFocus || selectorPopup.opened
                      ? root.SmoothUI.theme.accentColor.defaultBrushFor(root.SmoothUI.dark)
                      : root.SmoothUI.theme.res.cardStrokeColorDefault

        Behavior on border.color { ColorAnimation { duration: 140 } }
    }

    contentItem: RowLayout {
        id: contentRow
        spacing: root.compact ? 6 : 10
        opacity: root.selectionEnabled ? 1.0 : 0.55

        Rectangle {
            Layout.preferredWidth: 42
            Layout.preferredHeight: 36
            visible: !root.compact
            radius: 8
            color: root.SmoothUI.theme.res.subtleFillColorSecondary

            Image {
                anchors.fill: parent
                anchors.margins: 3
                source: root.selectedMachine.coverImageUrl || ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 1

            Label {
                id: compactMachineName
                Layout.fillWidth: true
                text: {
                    var modelName = root.selectedMachine.printerModel || qsTr("Select Printer")
                    if (root.compact) {
                        if (modelName.indexOf("Flashforge ") === 0)
                            modelName = modelName.substring(11)
                        return qsTr("Print Configuration · %1").arg(modelName)
                    }
                    return modelName
                }
                elide: Text.ElideRight
                font: Typography.bodyStrong
                color: root.SmoothUI.theme.res.textFillColorPrimary

                HoverHandler { id: compactMachineNameHover }
                ToolTip {
                    visible: compactMachineNameHover.hovered && compactMachineName.truncated
                    text: compactMachineName.text
                }
            }

            Label {
                id: machineDetailText
                Layout.fillWidth: true
                text: root.compact
                      ? (root.condensedPresetSummary
                         ? qsTr("%1 mm · %2 · %3")
                               .arg(root.selectedMachine.nozzleDiameter || "—")
                               .arg(root.filamentSummary())
                               .arg(SliceSettingsBridge.selectedProcessPresetName || qsTr("No Process Selected"))
                         : qsTr("Nozzle %1 mm · Bed %2")
                               .arg(root.selectedMachine.nozzleDiameter || "—")
                               .arg(root.bedTypeLabel(root.currentBedType)))
                      : (root.selectedMachine.width
                         ? qsTr("Build volume %1 × %2 × %3 mm")
                            .arg(Math.round(root.selectedMachine.width))
                            .arg(Math.round(root.selectedMachine.height))
                            .arg(Math.round(root.selectedMachine.printHeight))
                         : qsTr("No machine profile loaded"))
                elide: Text.ElideRight
                font: Typography.caption
                color: root.SmoothUI.theme.res.textFillColorTertiary

                HoverHandler { id: machineDetailHover }
                ToolTip {
                    visible: machineDetailHover.hovered && machineDetailText.truncated
                    text: machineDetailText.text
                }
            }
        }

        Rectangle {
            visible: root.compact && !root.condensedPresetSummary
            Layout.preferredWidth: 1
            Layout.preferredHeight: 26
            color: PrintWorkspaceStyle.divider(root.SmoothUI.dark)
        }

        ColumnLayout {
            visible: root.compact && !root.condensedPresetSummary
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 1

            Label {
                id: filamentConfigurationTitle
                Layout.fillWidth: true
                text: qsTr("Filament Configuration")
                elide: Text.ElideRight
                font.pixelSize: 10
                color: root.SmoothUI.theme.res.textFillColorTertiary

                HoverHandler { id: filamentConfigurationTitleHover }
                ToolTip {
                    visible: filamentConfigurationTitleHover.hovered && filamentConfigurationTitle.truncated
                    text: filamentConfigurationTitle.text
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Accessible.role: Accessible.StaticText
                Accessible.name: root.filamentSummary()

                Repeater {
                    model: root.filamentSlots || []

                    Rectangle {
                        required property var modelData
                        Layout.preferredWidth: 12
                        Layout.preferredHeight: 12
                        radius: 4
                        color: modelData.color || "transparent"
                        border.width: 1
                        border.color: root.SmoothUI.theme.res.controlStrokeColorDefault
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.filamentSummary()
                    elide: Text.ElideRight
                    font: Typography.caption
                    color: root.SmoothUI.theme.res.textFillColorPrimary
                }

                HoverHandler { id: filamentSummaryHover }

                ToolTip {
                    visible: filamentSummaryHover.hovered
                    text: root.filamentSummary()
                }
            }
        }

        Rectangle {
            visible: root.compact && !root.condensedPresetSummary
            Layout.preferredWidth: 1
            Layout.preferredHeight: 26
            color: PrintWorkspaceStyle.divider(root.SmoothUI.dark)
        }

        ColumnLayout {
            visible: root.compact && !root.condensedPresetSummary
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 1

            Label {
                id: processConfigurationTitle
                Layout.fillWidth: true
                text: qsTr("Process")
                elide: Text.ElideRight
                font.pixelSize: 10
                color: root.SmoothUI.theme.res.textFillColorTertiary

                HoverHandler { id: processConfigurationTitleHover }
                ToolTip {
                    visible: processConfigurationTitleHover.hovered && processConfigurationTitle.truncated
                    text: processConfigurationTitle.text
                }
            }

            Label {
                Layout.fillWidth: true
                text: SliceSettingsBridge.selectedProcessPresetName || qsTr("Not Selected")
                elide: Text.ElideRight
                font: Typography.caption
                color: root.SmoothUI.theme.res.textFillColorPrimary

                HoverHandler { id: processSummaryHover }

                ToolTip {
                    visible: processSummaryHover.hovered && parent.truncated
                    text: parent.text
                }
            }
        }

        Rectangle {
            visible: !root.compact
            Layout.preferredWidth: nozzleText.implicitWidth + 16
            Layout.preferredHeight: 26
            radius: 7
            color: root.SmoothUI.theme.res.subtleFillColorSecondary

            Label {
                id: nozzleText
                anchors.centerIn: parent
                text: "%1 mm".arg(root.selectedMachine.nozzleDiameter || "—")
                font: Typography.caption
                color: root.SmoothUI.theme.res.textFillColorSecondary
            }
        }

        Image {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            source: {
                var index = root.indexByValue(root.bedTypeDisplayOptions,
                                              "value", root.currentBedType)
                return index >= 0
                       ? root.bedTypeDisplayOptions[index].imageUrl : ""
            }
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            smooth: true
            visible: !root.compact
        }

        Label {
            Layout.maximumWidth: 104
            text: root.bedTypeLabel(root.currentBedType)
            elide: Text.ElideRight
            font: Typography.caption
            color: root.SmoothUI.theme.res.textFillColorSecondary
            visible: !root.compact
        }

        Icon {
            source: selectorPopup.opened ? FluentIcons.graph_ChevronUp : FluentIcons.graph_ChevronDown
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            color: root.SmoothUI.theme.res.textFillColorSecondary
        }
    }

    onClicked: root.togglePopup()

    AppPopup {
        id: selectorPopup
        objectName: "machineSelectorPopup"
        windowEscapeEnabled: true
        windowEscapeOnlyOutsideFocus: true
        parent: root.popupHost ? root.popupHost : root.parent
        popupAnchor: root.popupAnchor ? root.popupAnchor : root
        popupVerticalAnchor: root.popupVerticalAnchor
        centerHorizontally: root.detailedSettingsVisible
        popupAbove: root.popupAbove
        parentRelativeVerticalAnchor: true
        preferredWidth: root.detailedSettingsVisible
                        ? PrintWorkspaceStyle.slicingSettingsPopoverWidth
                        : PrintWorkspaceStyle.machineSelectorWidth
        preferredHeight: root.detailedSettingsVisible
                         ? PrintWorkspaceStyle.slicingSettingsPopoverHeight
                         : PrintWorkspaceStyle.machinePopoverHeight
        title: root.detailedSettingsVisible ? qsTr("Slicing Parameters") : qsTr("Print Configuration")
        showBack: root.detailedSettingsVisible
        onBackRequested: root.returnToQuickSettings()
        // The anchor is outside the Popup. CloseOnPressOutside would close on
        // press and let the anchor click reopen it immediately, breaking toggle.
        onOpened: {
            root.applyError = ""
            root.refreshDraft()
            root.syncPresetComboSelection()
            Qt.callLater(function() {
                if (selectorPopup.opened)
                    selectorContent.forceActiveFocus(Qt.PopupFocusReason)
            })
        }
        onClosed: {
            root.applyError = ""
            root.detailedSettingsVisible = false
            root.refreshDraft()
            root.forceActiveFocus()
        }

        Behavior on width {
            NumberAnimation {
                duration: PrintWorkspaceStyle.settingsTransitionDuration
                easing.type: Easing.OutCubic
            }
        }
        Behavior on height {
            NumberAnimation {
                duration: PrintWorkspaceStyle.settingsTransitionDuration
                easing.type: Easing.OutCubic
            }
        }
        Behavior on x {
            NumberAnimation {
                duration: PrintWorkspaceStyle.settingsTransitionDuration
                easing.type: Easing.OutCubic
            }
        }
        Behavior on y {
            NumberAnimation {
                duration: PrintWorkspaceStyle.settingsTransitionDuration
                easing.type: Easing.OutCubic
            }
        }

        Item {
            id: selectorContent
            anchors.fill: parent
            focus: true

                Item {
                    id: quickSettingsPage
                    x: root.detailedSettingsVisible ? -24 : 0
                    y: 0
                    width: parent.width
                    height: parent.height
                    opacity: root.detailedSettingsVisible ? 0.0 : 1.0
                    visible: opacity > 0.001
                    enabled: !root.detailedSettingsVisible

                    Behavior on x {
                        NumberAnimation {
                            duration: PrintWorkspaceStyle.settingsTransitionDuration
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on opacity {
                        NumberAnimation {
                            duration: PrintWorkspaceStyle.settingsTransitionDuration - 40
                            easing.type: Easing.OutCubic
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            Layout.topMargin: 14
                            Layout.bottomMargin: 14
                            spacing: 12

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 12
                                rowSpacing: 10

                                Label {
                                    Layout.preferredWidth: 64
                                    text: qsTr("Machine")
                                    font: Typography.body
                                    color: root.SmoothUI.theme.res.textFillColorSecondary
                                }
                                ComboBox {
                                    id: machineCombo
                                    objectName: "machineModelCombo"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                                    enabled: root.selectionEnabled && root.machineOptions.length > 0
                                    model: root.machineOptions
                                    textRole: "name"
                                    currentIndex: root.draftMachineIndex
                                    onActivated: {
                                        root.applyError = ""
                                        root.draftMachineIndex = currentIndex
                                        var machine = root.currentDraftMachine()
                                        root.draftNozzleIndex = machine !== null && machine.variants
                                                                && machine.variants.length > 0 ? 0 : -1
                                        root.applyDraftSelection(false)
                                    }

                                    ToolTip {
                                        visible: parent.hovered && parent.currentText.length > 28
                                        text: parent.currentText
                                    }
                                }

                                Label {
                                    Layout.preferredWidth: 64
                                    text: qsTr("Nozzle")
                                    font: Typography.body
                                    color: root.SmoothUI.theme.res.textFillColorSecondary
                                }
                                ComboBox {
                                    id: nozzleCombo
                                    objectName: "machineNozzleCombo"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                                    enabled: root.selectionEnabled && root.currentDraftMachine() !== null
                                             && root.currentDraftMachine().variants
                                             && root.currentDraftMachine().variants.length > 0
                                    model: root.currentDraftMachine() !== null
                                           ? root.currentDraftMachine().variants : []
                                    textRole: "name"
                                    currentIndex: root.draftNozzleIndex
                                    onActivated: {
                                        root.applyError = ""
                                        root.draftNozzleIndex = currentIndex
                                        root.applyDraftSelection(false)
                                    }

                                    ToolTip {
                                        visible: parent.hovered && parent.currentText.length > 28
                                        text: parent.currentText
                                    }
                                }

                                Label {
                                    Layout.preferredWidth: 64
                                    text: qsTr("Build Plate")
                                    font: Typography.body
                                    color: root.SmoothUI.theme.res.textFillColorSecondary
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Rectangle {
                                        Layout.preferredWidth: PrintWorkspaceStyle.controlHeight
                                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                                        radius: PrintWorkspaceStyle.controlRadius
                                        color: PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)

                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 3
                                            source: root.currentDraftPlate() !== null
                                                    ? root.currentDraftPlate().imageUrl : ""
                                            fillMode: Image.PreserveAspectFit
                                            asynchronous: true
                                            smooth: true
                                        }
                                    }

                                    ComboBox {
                                        id: bedTypeCombo
                                        objectName: "machineBuildPlateCombo"
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                                        enabled: root.selectionEnabled && root.bedTypeDisplayOptions.length > 0
                                        model: root.bedTypeDisplayOptions
                                        textRole: "label"
                                        valueRole: "value"
                                        currentIndex: root.draftBedTypeIndex
                                        onActivated: function(index) {
                                            root.applyError = ""
                                            root.draftBedTypeIndex = index
                                            root.applyDraftSelection(false)
                                        }

                                        ToolTip {
                                            visible: parent.hovered && parent.currentText.length > 28
                                            text: parent.currentText
                                        }
                                    }
                                }

                                Label {
                                    Layout.preferredWidth: 64
                                    text: qsTr("Filaments")
                                    font: Typography.body
                                    color: root.SmoothUI.theme.res.textFillColorSecondary
                                }
                                Button {
                                    objectName: "machineFilamentSettingsButton"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                                    enabled: root.selectionEnabled && root.filamentSlots.length > 0
                                    text: qsTr("%1 slot(s) · %2 · Edit…")
                                          .arg(root.filamentSlots.length)
                                          .arg(root.filamentSummary())
                                    onClicked: root.openFilamentSettings()

                                    ToolTip {
                                        visible: parent.hovered
                                        text: qsTr("Edit each filament preset and color")
                                    }
                                }

                                Label {
                                    Layout.preferredWidth: 64
                                    text: qsTr("Process")
                                    font: Typography.body
                                    color: root.SmoothUI.theme.res.textFillColorSecondary
                                }
                                ComboBox {
                                    id: processPresetCombo
                                    objectName: "machineProcessPresetCombo"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: PrintWorkspaceStyle.controlHeight
                                    enabled: root.selectionEnabled && root.processOptions.length > 0
                                    model: root.processOptions
                                    textRole: "name"
                                    valueRole: "id"
                                    currentIndex: root.indexByValue(root.processOptions, "id",
                                                                    SliceSettingsBridge.selectedProcessPresetId)
                                    onActivated: function(index) {
                                        root.selectProcessPresetAfterComboClose(index)
                                    }

                                    ToolTip {
                                        visible: parent.hovered && parent.currentText.length > 28
                                        text: parent.currentText
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 52
                                radius: PrintWorkspaceStyle.controlRadius
                                color: root.applyError !== "" || root.unavailableMessage !== ""
                                       ? root.SmoothUI.theme.res.systemFillColorCriticalBackground
                                       : PrintWorkspaceStyle.secondarySurface(root.SmoothUI.dark)

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 8

                                    Icon {
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        source: root.applyError !== "" || root.unavailableMessage !== ""
                                                ? FluentIcons.graph_Error : FluentIcons.graph_Info
                                        color: root.applyError !== "" || root.unavailableMessage !== ""
                                               ? root.SmoothUI.theme.res.systemFillColorCritical
                                               : root.SmoothUI.theme.res.textFillColorSecondary
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        Label {
                                            Layout.fillWidth: true
                                            visible: root.hasValidDraft
                                                     && root.applyError === ""
                                                     && root.unavailableMessage === ""
                                            text: {
                                                var variant = root.currentDraftVariant()
                                                return variant !== null
                                                       ? qsTr("Build volume %1 × %2 × %3 mm")
                                                             .arg(Math.round(variant.printableWidth))
                                                             .arg(Math.round(variant.printableDepth))
                                                             .arg(Math.round(variant.printableHeight))
                                                       : ""
                                            }
                                            font: Typography.caption
                                            color: root.SmoothUI.theme.res.textFillColorSecondary
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: root.applyError !== ""
                                                  ? root.applyError
                                                  : (root.unavailableMessage !== ""
                                                     ? root.unavailableMessage
                                                     : (root.draftMachineChanged
                                                        ? qsTr("Applying will reload compatible process and filament presets")
                                                        : (root.draftBedTypeChanged
                                                           ? qsTr("Applying will update the current build plate")
                                                           : qsTr("Switch the printer, filament, process, and build plate here"))))
                                            elide: Text.ElideRight
                                            font: Typography.caption
                                            color: root.applyError !== "" || root.unavailableMessage !== ""
                                                   ? root.SmoothUI.theme.res.systemFillColorCritical
                                                   : root.SmoothUI.theme.res.textFillColorTertiary
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            Layout.topMargin: 12
                            Layout.bottomMargin: 12
                            spacing: 8

                            Button {
                                objectName: "machineSelectorSlicingParametersButton"
                                implicitHeight: PrintWorkspaceStyle.controlHeight
                                text: qsTr("Slicing Parameters…")
                                enabled: root.selectionEnabled
                                onClicked: root.openDetailedSettings()
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                objectName: "machineSelectorDoneButton"
                                implicitWidth: 72
                                implicitHeight: PrintWorkspaceStyle.controlHeight
                                text: qsTr("Done")
                                highlighted: true
                                onClicked: selectorPopup.close()
                            }
                        }
                    }
                }

                SlicingSettingsContent {
                    id: detailedSettingsPage
                    x: root.detailedSettingsVisible ? 0 : 24
                    y: 0
                    width: parent.width
                    height: parent.height
                    embedded: true
                    opacity: root.detailedSettingsVisible ? 1.0 : 0.0
                    visible: opacity > 0.001
                    enabled: root.detailedSettingsVisible
                    onCloseRequested: selectorPopup.close()

                    Behavior on x {
                        NumberAnimation {
                            duration: PrintWorkspaceStyle.settingsTransitionDuration
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on opacity {
                        NumberAnimation {
                            duration: PrintWorkspaceStyle.settingsTransitionDuration - 40
                            easing.type: Easing.OutCubic
                        }
                    }
                }
        }
    }

    Connections {
        target: TaskStateNotifier
        function onIsBusyChanged(isBusy) {
            if (isBusy)
                selectorPopup.close()
        }
    }

    Connections {
        target: SliceSettingsBridge
        function onMachineSelectionChanged() {
            root.refreshDraft()
        }
        function onPresetSelectionChanged() {
            root.syncPresetComboSelection()
        }
        function onSettingChanged(key, value) {
            if (key === "curr_bed_type") {
                root.currentBedType = String(value)
                root.draftBedTypeIndex = root.indexByValue(
                            root.bedTypeDisplayOptions, "value", root.currentBedType)
            }
        }
    }

    function openSliceSettings(params) {
        if (params && params.workspaceIndex !== undefined)
            detailedSettingsPage.workspaceIndex = Math.max(0, Math.min(2, params.workspaceIndex))
        root.openDetailedSettings()
        if (params && params.settingKey) {
            Qt.callLater(function() {
                detailedSettingsPage.focusSetting(params.settingKey)
            })
        }
        return true
    }
}
