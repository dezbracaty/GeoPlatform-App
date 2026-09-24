import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl

/*!
    \brief 带数值显示的滑块。

    Slider 旁边的数值 Label 用 Layout.preferredWidth/minimumWidth 强制固定宽度，
    避免文本字符数变化（如 "9%" → "10%" → "100%"）引起 RowLayout 重排，
    从而避免 Slider 视觉位置漂移和拖动回弹。

    用法:
        LabeledSlider {
            from: 0; to: 1
            value: SomeBridge.opacity
            valueFormat: "percent"
            onMoved: (newValue) => SomeBridge.opacity = newValue
        }
*/
RowLayout {
    id: root

    SmoothUI.theme: Theme.of(root)

    property real from: 0
    property real to: 1
    property real value: 0
    // "percent" | "int" | "decimal1" | "decimal2"
    property string valueFormat: "decimal2"
    property int sliderWidth: 100
    property int labelWidth: 35
    property int fieldSpacing: 4
    property string suffix: ""

    signal moved(real newValue)

    spacing: root.fieldSpacing

    Slider {
        id: slider
        Layout.preferredWidth: root.sliderWidth
        from: root.from
        to: root.to
        value: root.value
        onMoved: root.moved(value)
    }

    Label {
        Layout.preferredWidth: root.labelWidth
        Layout.minimumWidth: root.labelWidth
        horizontalAlignment: Text.AlignRight
        text: root.formatValue(slider.value)
        font: Typography.caption
        color: root.SmoothUI.theme.res.textFillColorSecondary
    }

    function formatValue(v) {
        var formatted = ""
        switch (valueFormat) {
            case "percent":  formatted = Math.round(v * 100) + "%"; break
            case "int":      formatted = Math.round(v).toString(); break
            case "decimal1": formatted = v.toFixed(1); break
            case "decimal2":
            default:         formatted = v.toFixed(2); break
        }
        return formatted + root.suffix
    }
}
