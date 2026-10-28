import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QtControls
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform
import "CalibrationLabels.js" as Calib

Item {
    id: root
    property var printHost: null
    property var context: ({})
    property var form: ({mode:3})
    property var fieldDefinitions: ({})
    property var fieldKeys: []
    property string formError: ""
    property string information: ""
    property bool showCompletion: false
    property bool resultWasApplied: false
    property bool resultSubmitted: false
    property bool formContextValid: true
    property bool updatingForm: false

    function applyForm(view) {
        formError=Calib.message(view.error)
        if(!view.values)return false
        updatingForm=true
        var fields={},keys=[]
        view.fields.forEach(function(field) {
            field.label=Calib.label(field.label)
            field.choices=field.choices.map(function(choice) {choice.label=Calib.choiceLabel(choice.label);return choice})
            const previous=fieldDefinitions[field.key]
            fields[field.key]=JSON.stringify(previous)===JSON.stringify(field)?previous:field
            keys.push(field.key)
        })
        form=view.values
        // Keep field rows and unchanged option groups alive across edits and
        // visibility changes. Metadata and values have separate bindings.
        if(JSON.stringify(fields)!==JSON.stringify(fieldDefinitions))fieldDefinitions=fields
        if(JSON.stringify(keys)!==JSON.stringify(fieldKeys))fieldKeys=keys
        updatingForm=false
        return true
    }
    function setValue(key, value, focusOut) {
        var view=CalibrationBridge.updateParameter(key,value,!!focusOut)
        applyForm(view)
        if(focusOut&&view.warnings&&view.warnings.length) {
            information=view.warnings.map(Calib.message).join("\n");temperatureWarning.open()
        }
    }
    function open(mode, values) {
        context=CalibrationBridge.currentContext()
        formContextValid=applyForm(CalibrationBridge.parameterForm(mode,values||{}))
        showCompletion=false
        parametersPopup.open()
    }
    function begin() {
        // Match Orca's temperature kill-focus handler before validating OK.
        parametersPopup.contentItem.forceActiveFocus()
        if (temperatureWarning.visible) return
        const validation=CalibrationBridge.validateParameterForm()
        formError=Calib.message(validation.error)
        if(formError)return
        information=(validation.warnings||[]).map(Calib.message).join("\n")
        if(information){warningDialog.open();return}
        submit()
    }
    function submit() {
        if(CalibrationBridge.prepare(form)) {
            parametersPopup.close();statusPopup.open()
        } else formError=Calib.message(CalibrationBridge.error)
    }
    function edit() {
        if(CalibrationBridge.busy)return
        open(CalibrationBridge.parameters.mode,CalibrationBridge.parameters)
    }
    function record() {
        showCompletion=false;resultSubmitted=false;resultInput.text="";resultY.text="";resultNotes.text=""
        observed.checked=false;enablePA.checked=false;overrideRetraction.checked=false
        resultPopup.open()
    }
    Connections {
        target: CalibrationBridge
        function onParametersRequested(mode) { root.open(mode) }
        function onCompleted(success) {
            if(success)statusPopup.close()
        }
        function onChanged() {
            if(CalibrationBridge.busy&&["preparing","slicing","cancelling"].indexOf(CalibrationBridge.state)>=0)
                statusPopup.open()
        }
        function onResultAccepted(applied,path) { root.resultWasApplied=applied;root.showCompletion=true }
    }
    Connections {
        target: SliceSettingsBridge
        function contextChanged() {
            if(parametersPopup.visible) {
                root.formContextValid=false
                root.formError="打印配置已改变，请重新载入配置后核对参数。"
            }
        }
        function onSettingsChanged() { contextChanged() }
        function onMachineSelectionChanged() { contextChanged() }
        function onPresetSelectionChanged() { contextChanged() }
    }

    AppPopup {
        id:parametersPopup
        objectName:"calibrationParametersPopup"
        parent:QtControls.Overlay.overlay
        preferredWidth:600
        preferredHeight:Math.min(parent?parent.height-32:680,
            [1,2,3].indexOf(root.form.mode)>=0?(root.form.mode===2?674:558):root.form.mode===5?466:[10,11].indexOf(root.form.mode)>=0?650:root.form.mode===13?320:560)
        title:Calib.title(root.form.mode)+"校准"
        windowEscapeEnabled:true
        ScrollView {
            anchors.fill:parent;anchors.margins:Theme.space16
            contentWidth:availableWidth
            clip:true
            ColumnLayout {
                width:parent.width
                spacing:Theme.space12
                Label {
                    Layout.fillWidth:true
                    text:(root.context.printer||SliceSettingsBridge.selectedMachineModelId)+" / "+(root.context.material||SliceSettingsBridge.selectedFilamentPresetName)+" · 材料槽 1"
                    font:Typography.caption;color:Theme.res.textFillColorSecondary;wrapMode:Text.Wrap
                }
                Repeater {
                    model:root.fieldKeys
                    ColumnLayout {
                        id:fieldRow
                        required property var modelData
                        readonly property var definition:root.fieldDefinitions[modelData]
                        readonly property bool horizontalRadio:["extruder_type","pa_method"].indexOf(modelData)>=0
                        readonly property bool verticalRadio:modelData==="flow_type"
                        readonly property bool radio:horizontalRadio||verticalRadio
                        Layout.fillWidth:true
                        visible:definition.visible
                        enabled:definition.enabled
                        spacing:8
                        Label {visible:fieldRow.radio;text:fieldRow.definition.label;font:Typography.bodyStrong}
                        RowLayout {
                            visible:fieldRow.horizontalRadio
                            Repeater {
                                model:fieldRow.horizontalRadio?fieldRow.definition.choices:[]
                                RadioButton {
                                    required property var modelData
                                    objectName:fieldRow.definition.key==="extruder_type"?(modelData.value==="0"?"calibrationDDE":"calibrationBowden"):
                                        "calibrationPA"+modelData.label.replace("PA ","")
                                    text:modelData.label
                                    checked:String(root.form[fieldRow.definition.key])===modelData.value
                                    onClicked:root.setValue(fieldRow.definition.key,modelData.value)
                                }
                            }
                        }
                        ColumnLayout {
                            visible:fieldRow.verticalRadio
                            Repeater {
                                model:fieldRow.verticalRadio?fieldRow.definition.choices:[]
                                RadioButton {
                                    required property var modelData
                                    objectName:"calibrationFlowType"+modelData.value
                                    text:modelData.label
                                    checked:String(root.form[fieldRow.definition.key])===modelData.value
                                    onClicked:root.setValue(fieldRow.definition.key,modelData.value)
                                }
                            }
                        }
                        RowLayout {
                            visible:!fieldRow.radio
                            Layout.fillWidth:true
                            Label {visible:fieldRow.definition.type!=="boolean";text:fieldRow.definition.label;Layout.fillWidth:true}
                            ComboBox {
                                visible:fieldRow.definition.type==="enum"
                                objectName:"calibrationChoice_"+fieldRow.definition.key
                                Layout.preferredWidth:220
                                model:fieldRow.definition.choices
                                textRole:"label"
                                currentIndex:{
                                    const current=String(root.form[fieldRow.definition.key])
                                    return fieldRow.definition.choices.findIndex(function(c){return c.value===current})
                                }
                                onActivated:root.setValue(fieldRow.definition.key,fieldRow.definition.choices[currentIndex].value)
                            }
                            TextField {
                                visible:fieldRow.definition.type==="text"
                                objectName:"calibrationField_"+fieldRow.definition.key
                                Layout.preferredWidth:["speeds","accelerations"].indexOf(fieldRow.definition.key)>=0?220:150
                                horizontalAlignment:Text.AlignRight
                                text:String(root.form[fieldRow.definition.key]===undefined?"":root.form[fieldRow.definition.key])
                                onTextEdited:root.setValue(fieldRow.definition.key,text)
                                onActiveFocusChanged:{
                                    if(!activeFocus&&parametersPopup.visible&&!root.updatingForm)
                                        root.setValue(fieldRow.definition.key,text,true)
                                }
                            }
                            CheckBox {
                                visible:fieldRow.definition.type==="boolean"
                                text:fieldRow.definition.label
                                checked:!!root.form[fieldRow.definition.key]
                                onClicked:root.setValue(fieldRow.definition.key,checked)
                            }
                            Label {visible:fieldRow.definition.type==="text";text:fieldRow.definition.unit;Layout.preferredWidth:48}
                        }
                    }
                }
                Label {
                    Layout.fillWidth:true;wrapMode:Text.Wrap
                    text:Calib.guidance(root.form.mode)
                    font:Typography.caption;color:Theme.res.textFillColorSecondary
                }
                Label {
                    visible:!!root.fieldDefinitions.flow_type&&root.fieldDefinitions.flow_type.visible
                    Layout.fillWidth:true
                    text:"本轮流量基准："+(root.context.flowRatio||"—")
                    font:Typography.caption;color:Theme.res.textFillColorSecondary
                }
                Label {
                    visible:root.formError.length>0;Layout.fillWidth:true
                    text:root.formError;wrapMode:Text.Wrap;color:Theme.res.systemFillColorCritical
                }
            }
        }
        footerData:RowLayout {
            anchors.fill:parent;anchors.leftMargin:16;anchors.rightMargin:16
            Button {flat:true;text:"校准指南";onClicked:Qt.openUrlExternally("https://www.orcaslicer.com/wiki/"+({1:"pressure_advance_calib",2:"pressure_advance_calib",3:"pressure_advance_calib",5:"flow_ratio_calib",6:"temp_calib",7:"volumetric_speed_calib",8:"vfa_calib",9:"retraction_calib",10:"input_shaping_calib",11:"input_shaping_calib",12:"cornering_calib"}[root.form.mode]||"calibration_guide"))}
            Item {Layout.fillWidth:true}
            Button {visible:!root.formContextValid;text:"重新载入配置";onClicked:root.open(root.form.mode)}
            Button {text:"取消";onClicked:parametersPopup.close()}
            Button {objectName:"calibrationPrepareButton";text:"准备并切片";highlighted:true;enabled:!TaskStateNotifier.isBusy&&root.formContextValid;onClicked:root.begin()}
        }
    }
    ContentDialog {
        id:warningDialog
        title:"转角数值提示"
        standardButtons:Dialog.Ok
        Label {width:warningDialog.availableWidth;wrapMode:Text.Wrap;text:root.information}
        onAccepted:root.submit()
    }
    ContentDialog {
        id:temperatureWarning
        title:"温度范围提示"
        standardButtons:Dialog.Ok
        Label {text:root.information}
    }

    AppPopup {
        id:statusPopup
        objectName:"calibrationStatusPopup"
        parent:QtControls.Overlay.overlay
        preferredWidth:560;preferredHeight:Math.min(parent?parent.height-32:400,400)
        title:"校准 · 准备与切片"
        showHeader:false
        closePolicy:Popup.NoAutoClose
        closeOnEscape:false
        onEscapeRequested: { if(CalibrationBridge.busy)CalibrationBridge.cancel();else statusPopup.close() }
        ColumnLayout {
            anchors.fill:parent;spacing:0
            PrintPopoverHeader {
                title:statusPopup.title
                Layout.fillWidth:true;Layout.preferredHeight:PrintWorkspaceStyle.titleBarHeight
                cornerRadius:PrintWorkspaceStyle.menuRadius
                onCloseRequested:{if(CalibrationBridge.busy)CalibrationBridge.cancel();else statusPopup.close()}
            }
            ColumnLayout {
                Layout.fillWidth:true;Layout.fillHeight:true;Layout.margins:20;spacing:16
                Label {Layout.fillWidth:true;text:CalibrationBridge.message;wrapMode:Text.Wrap;font:Typography.bodyStrong}
                ProgressBar {Layout.fillWidth:true;indeterminate:CalibrationBridge.progress<0&&CalibrationBridge.busy;value:Math.max(0,CalibrationBridge.progress);visible:CalibrationBridge.busy}
                ScrollView {
                    Layout.fillWidth:true;Layout.fillHeight:true
                    contentWidth:availableWidth;clip:true
                    ColumnLayout {
                        width:parent.width
                        Label {
                            Layout.fillWidth:true
                            text:CalibrationBridge.error||CalibrationBridge.details||(CalibrationBridge.state==="cancelling"?"模型裁剪或排盘可能需要完成当前步骤后才能响应取消。":"使用原版校准任务，逐盘处理并加载预览。")
                            wrapMode:Text.Wrap
                            color:CalibrationBridge.error?Theme.res.systemFillColorCritical:Theme.res.textFillColorSecondary
                            font:Typography.caption
                        }
                        Repeater {
                            model:CalibrationBridge.plates
                            Button {
                                required property var modelData
                                visible:!CalibrationBridge.busy&&modelData.ready
                                text:"预览已完成的"+modelData.name
                                onClicked:CalibrationBridge.selectPlate(modelData.index)
                            }
                        }
                    }
                }
            }
        }
        footerData:RowLayout {
            anchors.fill:parent;anchors.leftMargin:16;anchors.rightMargin:16
            Item {Layout.fillWidth:true}
            Button {visible:!CalibrationBridge.busy;text:"返回参数";onClicked:{statusPopup.close();root.edit()}}
            Button {visible:!CalibrationBridge.busy&&CalibrationBridge.configurationValid;text:"重试未完成盘";onClicked:CalibrationBridge.retry()}
            Button {visible:CalibrationBridge.busy;text:CalibrationBridge.state==="cancelling"?"正在取消…":"取消任务";enabled:CalibrationBridge.state!=="cancelling";onClicked:CalibrationBridge.cancel()}
            Button {visible:!CalibrationBridge.busy;text:"关闭";onClicked:statusPopup.close()}
        }
    }

    AppPopup {
        id:resultPopup
        objectName:"calibrationResultPopup"
        parent:QtControls.Overlay.overlay
        title:CalibrationBridge.resultContext.title+" · 记录结果"
        preferredWidth:600
        preferredHeight:Math.min(parent?parent.height-32:560,
            root.showCompletion?330:axes||result.key==="filament_retraction_length"||result.key==="pressure_advance"&&!result.pressureAdvanceEnabled?560:500)
        property var result:CalibrationBridge.resultContext
        property bool axes:[10,11].indexOf(result.mode)>=0
        ScrollView {
            anchors.fill:parent;anchors.margins:20;contentWidth:availableWidth;clip:true
            ColumnLayout {
                width:parent.width;spacing:16
                Label {Layout.fillWidth:true;wrapMode:Text.Wrap;text:root.showCompletion?(root.resultWasApplied?"当前材料配置已更新，材料预设尚未保存。":[10,11,12].indexOf(resultPopup.result.mode)>=0?"观察结果已保存。设备参数的应用与保存请在设备设置中处理。":"观察结果已保存，未自动修改切片参数。"):"完成试打印后，观察实物并填写结果。";font:Typography.bodyStrong}
                ColumnLayout {
                    visible:!root.showCompletion;Layout.fillWidth:true;spacing:12
                    RowLayout {
                        Label {text:resultPopup.axes?"X 轴结果":resultPopup.result.key==="filament_flow_ratio"?"最终流量比例":"本次结果";Layout.fillWidth:true}
                        TextField {id:resultInput;objectName:"calibrationResultValue";Layout.preferredWidth:220;placeholderText:"根据实物填写"}
                        Label {text:resultPopup.result.unit||""}
                    }
                    RowLayout {
                        visible:resultPopup.axes
                        Label {text:"Y 轴结果";Layout.fillWidth:true}
                        TextField {id:resultY;objectName:"calibrationResultY";Layout.preferredWidth:220;placeholderText:"根据实物填写"}
                    }
                    Label {text:"观察备注";font:Typography.caption;color:Theme.res.textFillColorSecondary}
                    TextArea {id:resultNotes;Layout.fillWidth:true;Layout.preferredHeight:76;placeholderText:"实物表现、选中的标记或速度区间";wrapMode:TextEdit.Wrap}
                    Label {
                        Layout.fillWidth:true;wrapMode:Text.Wrap
                        text:resultPopup.result.appliesToMaterial?"应用位置：材料槽 1 / "+resultPopup.result.material+"\n当前值："+(resultPopup.result.oldValue==="nil"?"继承打印机（"+resultPopup.result.inheritedRetraction+" mm）":resultPopup.result.oldValue):"本次仅记录观察，不自动写入设备或推断切片参数。"
                        font:Typography.caption;color:Theme.res.textFillColorSecondary
                    }
                    CheckBox {id:enablePA;visible:resultPopup.result.key==="pressure_advance"&&!resultPopup.result.pressureAdvanceEnabled;text:"同时启用当前材料的压力提前"}
                    CheckBox {id:overrideRetraction;visible:resultPopup.result.key==="filament_retraction_length";text:"覆盖当前材料继承的回抽长度"}
                    CheckBox {id:observed;objectName:"calibrationResultObserved";text:"已完成试打印，并检查实物"}
                }
                Label {visible:root.showCompletion;Layout.fillWidth:true;wrapMode:Text.Wrap;text:CalibrationBridge.recordPath;font:Typography.caption;color:Theme.res.textFillColorSecondary}
                Label {visible:!CalibrationBridge.configurationValid&&!root.showCompletion;Layout.fillWidth:true;wrapMode:Text.Wrap;text:"配置已改变，请重新校准后采纳结果。";color:Theme.res.systemFillColorCaution}
                Label {visible:root.resultSubmitted&&CalibrationBridge.error.length>0;Layout.fillWidth:true;wrapMode:Text.Wrap;text:CalibrationBridge.error;color:Theme.res.systemFillColorCritical}
            }
        }
        footerData:RowLayout {
            anchors.fill:parent;anchors.leftMargin:16;anchors.rightMargin:16
            Item {Layout.fillWidth:true}
            Button {text:root.showCompletion?"完成":"暂不采纳";onClicked:resultPopup.close()}
            Button {
                visible:!root.showCompletion
                objectName:"calibrationAcceptResult"
                text:resultPopup.result.appliesToMaterial?"应用到当前材料":"保存观察记录"
                enabled:observed.checked&&CalibrationBridge.configurationValid&&!TaskStateNotifier.isBusy
                highlighted:true
                onClicked:{root.resultSubmitted=true;CalibrationBridge.acceptResult({observed:observed.checked,value:resultInput.text,valueY:resultY.text,notes:resultNotes.text,enablePressureAdvance:enablePA.checked,overrideRetraction:overrideRetraction.checked})}
            }
            Button {
                visible:root.showCompletion&&CalibrationBridge.resultContext.canRefine
                text:"继续精调";highlighted:true
                onClicked:{resultPopup.close();root.open(CalibrationBridge.parameters.mode,CalibrationBridge.parameters);root.setValue("next_pass","")}
            }
            Button {visible:root.showCompletion&&root.resultWasApplied;text:"前往参数设置";onClicked:{resultPopup.close();if(root.printHost)root.printHost.openSliceSettings({})}}
        }
    }
    ContentDialog {
        id:informationDialog
        title:"校准准备提示"
        width:Math.min(600,root.width-32)
        standardButtons:Dialog.Ok
        Label {width:informationDialog.availableWidth;wrapMode:Text.Wrap;text:CalibrationBridge.details}
    }
    function showDetails() { informationDialog.open() }
}
