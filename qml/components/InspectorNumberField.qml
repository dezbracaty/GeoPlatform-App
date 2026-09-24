import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl

RowLayout {
    id: root

    property real value: 0
    property real minimumValue: 0
    property real maximumValue: 100
    property int precision: 2
    property string unit: ""

    signal valueEdited(real newValue)

    SmoothUI.theme: Theme.of(root)

    Layout.preferredWidth: 156
    Layout.minimumWidth: 156
    Layout.maximumWidth: 156
    spacing: 8

    TextField {
        id: editor
        Layout.preferredWidth: 112
        Layout.minimumWidth: 112
        Layout.maximumWidth: 112
        implicitHeight: 36
        horizontalAlignment: Text.AlignRight
        selectByMouse: true
        validator: DoubleValidator {
            bottom: root.minimumValue
            top: root.maximumValue
            decimals: root.precision
        }
        text: root.formattedValue()

        onEditingFinished: root.commitValue()
    }

    Label {
        Layout.preferredWidth: 36
        Layout.minimumWidth: 36
        Layout.maximumWidth: 36
        horizontalAlignment: Text.AlignLeft
        text: root.unit
        font: Typography.caption
        color: root.SmoothUI.theme.res.textFillColorTertiary
    }

    onValueChanged: {
        if (!editor.activeFocus) {
            editor.text = formattedValue()
        }
    }

    function formattedValue() {
        return Number(root.value).toFixed(root.precision)
    }

    function commitValue() {
        var parsed = Number(editor.text)
        if (!isNaN(parsed)
                && parsed >= root.minimumValue
                && parsed <= root.maximumValue) {
            root.valueEdited(parsed)
        } else {
            editor.text = formattedValue()
        }
    }
}
