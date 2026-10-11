import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0

Item {
    id: root
    objectName: "fiberMaskPanel"
    anchors.fill: parent
    clip: true
    visible: FiberMaskBridge.active
    readonly property int editorPage: editorTabs.currentIndex
    SmoothUI.theme: Theme.of(root)
    readonly property var state: FiberMaskBridge.state
    readonly property int stage: Number(state.stage || 0)
    property var parameterValues: ({})
    property var parameterDrafts: ({})
    property bool flushingInputs: false
    readonly property string editIdentity: String(state.modelId) + ":" + String(state.selectedMaskId)
    onEditIdentityChanged: parameterDrafts = ({})
    onVisibleChanged: if (!visible) parameterDrafts = ({})
    ListModel { id: parameterKeys }
    function refreshParameters() {
        const values = {};
        const items = FiberMaskBridge.parameters;
        for (let i = 0; i < items.length; ++i) {
            const key = items[i].key;
            values[key] = items[i];
            if (i < parameterKeys.count && parameterKeys.get(i).settingKey === key)
                continue;
            let found = -1;
            for (let j = i + 1; j < parameterKeys.count; ++j)
                if (parameterKeys.get(j).settingKey === key) { found = j; break; }
            if (found >= 0) parameterKeys.move(found, i, 1);
            else parameterKeys.insert(i, {settingKey: key});
        }
        if (parameterKeys.count > items.length)
            parameterKeys.remove(items.length, parameterKeys.count - items.length);
        parameterValues = values;
    }
    function editParameter(key, text) {
        const drafts = Object.assign({}, parameterDrafts);
        drafts[key] = text;
        parameterDrafts = drafts;
    }
    function submitParameter(key) {
        if (!Object.prototype.hasOwnProperty.call(parameterDrafts, key)) return;
        const text = parameterDrafts[key];
        const drafts = Object.assign({}, parameterDrafts);
        delete drafts[key];
        parameterDrafts = drafts;
        send("parameter", {key: key, value: text});
    }
    function flushEditorInputs() {
        if (flushingInputs) return;
        flushingInputs = true;
        for (let i = 0; i < dimensionFields.count; ++i)
            dimensionFields.itemAt(i).input.submit();
        depthInput.submit();
        sectionHeightBox.submit();
        for (const key of Object.keys(parameterDrafts)) submitParameter(key);
        flushingInputs = false;
    }
    Component.onCompleted: refreshParameters()
    Connections {
        target: FiberMaskBridge
        function onParametersChanged() { root.refreshParameters(); }
        function onCommitInputsRequested() { root.flushEditorInputs(); }
    }
    function send(operation, values) {
        FiberMaskBridge.request(operation, values || {});
    }

    component EditorNumberBox: NumberBox {
        id: numberInput
        property real projectedValue: 0
        property bool userEdited: false
        signal edited(real newValue)
        Binding on value { value: numberInput.projectedValue; when: !numberInput.userEdited }
        onTextEdited: userEdited = true
        onValueChanged: {
            if (value === null) return;
            if ((userEdited || activeFocus) && Number(value) !== projectedValue)
                edited(Number(value));
            else if (!userEdited)
                text = Number(value).toFixed(precision);
        }
        function submit() {
            if (userEdited) editingFinished();
        }
        Connections {
            target: numberInput
            function onEditingFinished() { numberInput.userEdited = false; }
        }
    }

    component PanelButton: Button {
        id: button
        Layout.minimumWidth: 0
        ToolTip.visible: hovered && contentItem.truncated
        ToolTip.text: text
        ToolTip.delay: 500
        contentItem: Label {
            text: button.text
            font: button.font
            color: button.SmoothUI.textColor
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component PanelTab: TabButton {
        id: tab
        width: Math.max(0, (editorTabs.width - editorTabs.spacing) / 2)
        implicitHeight: Math.max(PrintWorkspaceStyle.controlHeight, implicitContentHeight + 12)
        font: Typography.bodyStrong
        ToolTip.visible: hovered && contentItem.truncated
        ToolTip.text: text
        ToolTip.delay: 500
        contentItem: Label {
            text: tab.text
            font: tab.font
            color: tab.SmoothUI.textColor
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Label {
                text: qsTr("Fiber masks")
                font: Typography.bodyStrong
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            PanelButton {
                objectName: "fiberMaskDoneButton"
                text: qsTr("Done")
                onClicked: FiberMaskBridge.finish()
            }
        }
        ScrollView {
            id: editorScroll
            objectName: "fiberMaskEditorScroll"
            contentWidth: availableWidth
            contentHeight: editorContent.implicitHeight
            Layout.minimumHeight: 0
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.fillHeight: true
            clip: true
            leftPadding: 0
            rightPadding: 12
            topPadding: 0
            bottomPadding: 8
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: editorContent
                objectName: "fiberMaskEditorContent"
                width: editorScroll.availableWidth
                height: implicitHeight
                spacing: 12
                FiberMaskListPanel {
                    id: maskList
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: Math.min(180, Math.max(60, contentHeight + 8), root.height * 0.25)
                }
                TabBar {
                    id: editorTabs
                    objectName: "fiberMaskTabs"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 8
                    currentIndex: 0
                    PanelTab {
                        objectName: "fiberMaskGeometryTab"
                        text: qsTr("Position and dimensions")
                        onClicked: editorTabs.setCurrentIndex(0)
                    }
                    PanelTab {
                        objectName: "fiberMaskParametersTab"
                        text: qsTr("Fiber parameters")
                        onClicked: editorTabs.setCurrentIndex(1)
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Model: %1").arg(root.state.modelName || "")
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: root.editorPage === 0
                    text: [qsTr("Hold Ctrl and drag a rectangle. New masks default to the model's full height; adjust position and dimensions after creation."), qsTr("Release the mouse to save, then drag another rectangle to create the next mask."), qsTr("Hold Ctrl and drag the depth arrow, or enter a signed depth. Negative values reverse direction."), qsTr("Drag the mask to move it, or hold Ctrl and drag in empty space to create another.")][root.stage]
                    wrapMode: Text.Wrap
                }
                Label {
                    visible: root.editorPage === 0
                    text: qsTr("Drag masks to move them. Drag empty space to rotate the camera. Hold Ctrl to draw; Alt disables drawing snaps.")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Label {
                        text: qsTr("Box")
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    PanelButton {
                        text: qsTr("New")
                        onClicked: root.send("new", {
                            shape: 0
                        })
                    }
                }
                Label {
                    text: root.state.snapped ? qsTr("Snapped to model bounds") : ""
                    visible: root.state.snapped === true
                }
                Label {
                    text: qsTr("Ctrl drawing enabled")
                    visible: root.state.drawingEnabled === true
                }
                TextField {
                    objectName: "fiberMaskNameField"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    placeholderText: qsTr("Mask name")
                    text: root.state.name || ""
                    onTextEdited: root.send("name", {
                        value: text
                    })
                }
                Column {
                    spacing: 12
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: editorScroll.availableWidth
                    visible: root.editorPage === 0
                    Label {
                        width: editorScroll.availableWidth
                        text: qsTr("Position and dimensions are in model coordinates.")
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        id: dimensionFields
                        model: [
                            {
                                key: "width",
                                label: qsTr("Width / diameter"),
                                operation: "dimensions"
                            },
                            {
                                key: "length",
                                label: qsTr("Length"),
                                operation: "dimensions"
                            },
                            {
                                key: "height",
                                label: qsTr("Height"),
                                operation: "dimensions"
                            },
                            {
                                key: "x",
                                label: "X",
                                operation: "position"
                            },
                            {
                                key: "y",
                                label: "Y",
                                operation: "position"
                            },
                            {
                                key: "z",
                                label: "Z",
                                operation: "position"
                            },
                            {
                                key: "rx",
                                label: qsTr("Rotation X"),
                                operation: "rotation"
                            },
                            {
                                key: "ry",
                                label: qsTr("Rotation Y"),
                                operation: "rotation"
                            },
                            {
                                key: "rz",
                                label: qsTr("Rotation Z"),
                                operation: "rotation"
                            }
                        ]
                        delegate: Item {
                            property alias input: dimensionInput
                            width: editorScroll.availableWidth
                            implicitHeight: Math.max(dimensionLabel.implicitHeight, dimensionInput.implicitHeight)
                            required property var modelData
                            visible: (modelData.key !== "length" || Number(root.state.shape) === 0) && (modelData.key !== "height" || Number(root.state.shape) !== 2)
                            enabled: root.stage === 3 || ((root.stage === 1 || root.stage === 2) && modelData.operation === "dimensions" && (root.stage !== 1 || modelData.key !== "height"))
                            Label {
                                id: dimensionLabel
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                width: Math.min(125, editorScroll.availableWidth * 0.36)
                                wrapMode: Text.Wrap
                            }
                            EditorNumberBox {
                                id: dimensionInput
                                objectName: "fiberMask_" + modelData.key
                                anchors.verticalCenter: parent.verticalCenter
                                x: dimensionLabel.width + 8
                                width: Math.max(0, editorScroll.availableWidth - x)
                                projectedValue: Number(root.state[modelData.key] || 0)
                                precision: 3
                                smallChange: 0.1
                                placementMode: NumberBoxType.Inline
                                onEdited: newValue => {
                                    let values = {};
                                    values[modelData.key] = newValue;
                                    root.send(modelData.operation, values);
                                }
                            }
                        }
                    }
                    Item {
                        width: editorScroll.availableWidth
                        implicitHeight: Math.max(depthLabel.implicitHeight, depthNext.implicitHeight, depthInput.visible ? depthInput.implicitHeight : 0)
                        visible: Number(root.state.shape) !== 0 && (root.stage === 1 || root.stage === 2)
                        Label {
                            id: depthLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Signed depth")
                            width: Math.min(125, editorScroll.availableWidth * 0.36)
                            wrapMode: Text.Wrap
                        }
                        EditorNumberBox {
                            id: depthInput
                            visible: root.stage === 2
                            objectName: "fiberMaskDepthBox"
                            anchors.verticalCenter: parent.verticalCenter
                            x: depthLabel.width + 8
                            width: Math.max(0, editorScroll.availableWidth - x - depthNext.width - 8)
                            projectedValue: Number(root.state.depth || 0)
                            precision: 3
                            smallChange: 0.1
                            placementMode: NumberBoxType.Inline
                            onEdited: newValue => root.send("depth", {value: newValue})
                        }
                        PanelButton {
                            id: depthNext
                            x: editorScroll.availableWidth - width
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Next")
                            enabled: root.stage === 1 ? Number(root.state.width) > 0 && Number(root.state.length) > 0 : Number(root.state.height) > 0
                            onClicked: root.send("next")
                        }
                    }
                }
                Column {
                    spacing: 12
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: root.editorPage === 1
                    Label {
                        width: editorScroll.availableWidth
                        text: qsTr("Mask fiber parameters take priority over the model settings.")
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: parameterKeys
                        delegate: Column {
                            required property string settingKey
                            readonly property var modelData: root.parameterValues[settingKey] || ({
                                key: settingKey, label: "", value: "", unit: "", choices: [],
                                boolean: false, enabled: false, visible: false
                            })
                            objectName: "fiberMaskParameter_" + modelData.key
                            visible: modelData.visible !== false
                            width: editorScroll.availableWidth
                            spacing: 4
                            Label {
                                width: editorScroll.availableWidth
                                visible: !modelData.boolean
                                text: qsTranslate("LibSlicerSettings", modelData.label)
                                wrapMode: Text.Wrap
                            }
                            Item {
                                id: valueRow
                                width: editorScroll.availableWidth
                                implicitHeight: modelData.boolean ? booleanControl.implicitHeight :
                                                (modelData.choices.length > 0 ? choicesControl.implicitHeight : textControl.implicitHeight)
                                readonly property real valueWidth: Math.max(0, width - (unitLabel.visible ? unitLabel.width + 8 : 0))
                                enabled: modelData.enabled
                                CheckBox {
                                    objectName: "fiberMaskValue_" + modelData.key
                                    visible: modelData.boolean
                                    checked: modelData.value === "1"
                                    id: booleanControl
                                    width: valueRow.valueWidth
                                    text: qsTranslate("LibSlicerSettings", modelData.label)
                                    contentItem: Label {
                                        text: booleanControl.text
                                        font: booleanControl.font
                                        leftPadding: booleanControl.indicator.width + booleanControl.spacing
                                        verticalAlignment: Text.AlignVCenter
                                        wrapMode: Text.Wrap
                                    }
                                    onClicked: root.send("parameter", {
                                        key: modelData.key,
                                        value: checked ? "1" : "0"
                                    })
                                }
                                ComboBox {
                                    id: choicesControl
                                    width: valueRow.valueWidth
                                    objectName: "fiberMaskChoices_" + modelData.key
                                    visible: !modelData.boolean && modelData.choices.length > 0
                                    model: modelData.choices.map(function (option) {
                                        return {
                                            value: option.value,
                                            label: qsTranslate("LibSlicerSettings", option.label)
                                        };
                                    })
                                    textRole: "label"
                                    valueRole: "value"
                                    currentIndex: {
                                        for (let i = 0; i < modelData.choices.length; ++i)
                                            if (modelData.choices[i].value === modelData.value)
                                                return i;
                                        return -1;
                                    }
                                    onActivated: root.send("parameter", {
                                        key: modelData.key,
                                        value: currentValue
                                    })
                                }
                                TextField {
                                    id: textControl
                                    width: valueRow.valueWidth
                                    objectName: "fiberMaskText_" + modelData.key
                                    visible: !modelData.boolean && modelData.choices.length === 0
                                    text: Object.prototype.hasOwnProperty.call(root.parameterDrafts, settingKey) ? root.parameterDrafts[settingKey] : modelData.value
                                    onTextEdited: root.editParameter(settingKey, text)
                                    onEditingFinished: root.submitParameter(settingKey)
                                }
                                Label {
                                    id: unitLabel
                                    width: implicitWidth
                                    x: valueRow.width - width
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: text.length > 0
                                    text: qsTranslate("LibSlicerSettings", modelData.unit)
                                }
                            }
                            Label {
                                objectName: "fiberMaskParameterError_" + modelData.key
                                width: editorScroll.availableWidth
                                color: "#d64242"
                                text: modelData.error || ""
                                visible: text.length > 0
                                wrapMode: Text.Wrap
                            }
                            Button {
                                objectName: "fiberMaskInherit_" + modelData.key
                                width: Math.min(implicitWidth, editorScroll.availableWidth)
                                visible: modelData.overridden === true
                                text: qsTr("Restore inheritance")
                                onClicked: root.send("parameter", {
                                    key: modelData.key,
                                    inherit: true
                                })
                            }
                            Label {
                                width: editorScroll.availableWidth
                                text: !modelData.enabled ? qsTr("Unavailable with the current fiber options") : ""
                                visible: text.length > 0
                                opacity: 0.65
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
                Column {
                    visible: root.state.showSection === true
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 5
                    Label {
                        text: qsTr("Coverage preview")
                    }
                    Item {
                        id: sectionHeightRow
                        width: editorScroll.availableWidth
                        implicitHeight: Math.max(sectionHeightLabel.implicitHeight,
                                                 sectionHeightBox.implicitHeight)
                        height: implicitHeight
                        Label {
                            id: sectionHeightLabel
                            width: Math.min(implicitWidth, editorScroll.availableWidth * 0.45)
                            anchors.verticalCenter: parent.verticalCenter
                            wrapMode: Text.Wrap
                            text: qsTr("Section height")
                        }
                        EditorNumberBox {
                            id: sectionHeightBox
                            objectName: "fiberMaskSectionHeightBox"
                            x: sectionHeightLabel.width + 8
                            width: Math.max(0, Math.min(sectionHeightRow.width,
                                                     editorScroll.availableWidth) - x)
                            anchors.verticalCenter: parent.verticalCenter
                            projectedValue: Number(root.state.sectionZ || 0)
                            precision: 2
                            smallChange: 0.2
                            placementMode: NumberBoxType.Inline
                            onEdited: newValue => root.send("section", {z: newValue})
                        }
                    }
                    Canvas {
                        id: sectionCanvas
                        objectName: "fiberMaskSectionCanvas"
                        width: editorScroll.availableWidth
                        height: 200
                        readonly property var section: root.state.section || ({})
                        onSectionChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onPaint: {
                            let ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            if (!section.valid)
                                return;
                            const sx = Math.max(0.001, section.maxX - section.minX);
                            const sy = Math.max(0.001, section.maxY - section.minY);
                            const scale = Math.min((width - 16) / sx, (height - 16) / sy);
                            const ox = (width - sx * scale) / 2;
                            const oy = (height - sy * scale) / 2;
                            function paint(triangles, color) {
                                ctx.fillStyle = color;
                                for (let t of triangles) {
                                    ctx.beginPath();
                                    ctx.moveTo(ox + (t[0] - section.minX) * scale, height - oy - (t[1] - section.minY) * scale);
                                    ctx.lineTo(ox + (t[2] - section.minX) * scale, height - oy - (t[3] - section.minY) * scale);
                                    ctx.lineTo(ox + (t[4] - section.minX) * scale, height - oy - (t[5] - section.minY) * scale);
                                    ctx.closePath();
                                    ctx.fill();
                                }
                            }
                            paint(section.model || [], "#76808c");
                            paint(section.covered || [], "#f4b632");
                        }
                    }
                }
            }
        }
        CheckBox {
            id: sectionToggle
            objectName: "fiberMaskSectionToggle"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("Show section")
            checked: root.state.showSection === true
            enabled: root.stage === 3
            contentItem: Label {
                text: sectionToggle.text
                font: sectionToggle.font
                leftPadding: sectionToggle.indicator.width + sectionToggle.spacing
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
            }
            onClicked: root.send("section", {
                show: checked
            })
        }
        Label {
            objectName: "fiberMaskError"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: String(root.state.error || "").length > 0
            text: root.state.error || ""
            color: "#d64242"
            wrapMode: Text.Wrap
        }
        GridLayout {
            objectName: "fiberMaskActions"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            columns: root.width < 420 ? 2 : 4
            uniformCellWidths: true
            columnSpacing: 8
            rowSpacing: 8
            PanelButton {
                Layout.fillWidth: true
                text: qsTr("Cancel")
                onClicked: FiberMaskBridge.cancel()
            }
            PanelButton {
                Layout.fillWidth: true
                text: qsTr("Copy")
                enabled: root.state.editing === true
                onClicked: root.send("copy")
            }
            PanelButton {
                Layout.fillWidth: true
                text: qsTr("Delete")
                enabled: root.state.editing === true
                onClicked: root.send("remove")
            }
            PanelButton {
                Layout.fillWidth: true
                objectName: "fiberMaskConfirmButton"
                text: root.state.editing === true ? qsTr("Save changes") : qsTr("Confirm")
                enabled: root.state.canConfirm === true
                onClicked: root.send("confirm")
            }
        }
    }
}
