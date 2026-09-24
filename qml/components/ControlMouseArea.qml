import QtQuick
import QtQuick.Controls
import GPlatform

MouseArea {
    id: root
    required property var rendererItem

    // 确保能接收键盘焦点
    focus: true
    Keys.enabled: true

    function pushKeyEvent(event, eventType) {
        if (rendererItem && rendererItem.viewId)
            ViewportInputRouter.pushKeyEvent(rendererItem.viewId, event.accepted, event.count, event.isAutoRepeat, event.key, event.modifiers, event.nativeScanCode, event.text, eventType);
    }
    
    function pushMouseEvent(mouse, type) {
        var screenPoint = rendererItem.mapToGlobal(mouse.x, mouse.y);
        if (rendererItem && rendererItem.viewId)
            ViewportInputRouter.pushMouseEvent(rendererItem.viewId, type
            , mouse.button, mouse.buttons, mouse.flags, mouse.modifiers, mouse.wasHeld, mouse.x, mouse.y       // local position
            , rendererItem.x, rendererItem.y     // window position
            , screenPoint.x, screenPoint.y  // screen position
            );
    }
    
    function pushWheelEvent(wheel) {
        if (rendererItem && rendererItem.viewId) {
            var globalPos = rendererItem.mapToGlobal(wheel.x, wheel.y);
            ViewportInputRouter.pushWheelEvent(rendererItem.viewId, Qt.point(wheel.x, wheel.y)
            , Qt.point(globalPos.x, globalPos.y) // 全局位置
            , Qt.point(wheel.pixelDelta.x, wheel.pixelDelta.y) // 像素增量
            , Qt.point(wheel.angleDelta.x, wheel.angleDelta.y) // 角度增量
            , wheel.buttons                 // 鼠标按钮状态
            , wheel.modifiers               // 键盘修饰符
            , wheel.phase                   // 滚动阶段
            , wheel.inverted                // 是否反转
            , Qt.MouseEventNotSynthesized    // 事件源（默认值）
            );
        }
    }

    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
    anchors.fill: parent
    hoverEnabled: true

    Keys.onPressed: function (event) {
        pushKeyEvent(event, 0);
    }
    Keys.onReleased: function (event) {
        pushKeyEvent(event, 1);
    }
    onEntered: {
        parent.focus = true;
        root.focus = true;
        root.forceActiveFocus();
    }
    onExited: {
        if (rendererItem && rendererItem.viewId)
            ViewportInputRouter.pushPointerExited(rendererItem.viewId);
        parent.focus = false;
    }
    onPositionChanged: function (mouse) {
        pushMouseEvent(mouse, "move");
    }
    onPressed: function (mouse) {
        if (rendererItem && rendererItem.activateView)
            rendererItem.activateView();
        pushMouseEvent(mouse, "press");
    }
    onReleased: function (mouse) {
        pushMouseEvent(mouse, "release");
    }
    onWheel: function (wheel) {
        pushWheelEvent(wheel);
    }
}
