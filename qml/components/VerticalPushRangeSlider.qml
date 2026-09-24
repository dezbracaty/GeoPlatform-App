import QtQuick

Item {
    id: control

    property int minimumValue: 0
    property int maximumValue: 100
    property int startValue: minimumValue
    property int endValue: maximumValue
    property int stepSize: 1
    property real padding: 6
    property real handleWidth: 28
    property real handleHeight: 20
    property real handleGap: 2
    property color railColor: "#808080"
    property color rangeColor: "#008573"
    property Component firstHandleContent
    property Component secondHandleContent

    readonly property bool firstPressed: firstMouseArea.pressed
    readonly property bool secondPressed: secondMouseArea.pressed
    readonly property bool firstHovered: firstMouseArea.containsMouse
    readonly property bool secondHovered: secondMouseArea.containsMouse
    readonly property alias firstHandleItem: firstHandle
    readonly property alias secondHandleItem: secondHandle
    readonly property real travelLength: Math.max(
                                             0,
                                             height - 2 * padding - handleHeight)

    signal rangeMoved(int start, int end)

    function boundedValue(value) {
        return Math.max(minimumValue, Math.min(maximumValue, value))
    }

    function snappedValue(value) {
        var step = Math.max(1, stepSize)
        var snapped = minimumValue
                + Math.round((value - minimumValue) / step) * step
        return Math.round(boundedValue(snapped))
    }

    function ratioForValue(value) {
        var span = maximumValue - minimumValue
        if (span <= 0)
            return 0
        return (boundedValue(value) - minimumValue) / span
    }

    function centerYForValue(value) {
        return padding + handleHeight / 2
                + (1 - ratioForValue(value)) * travelLength
    }

    function beginDrag(endpoint, pointerY) {
        _dragEndpoint = endpoint
        _pressPointerY = pointerY
        _pressValue = endpoint === 0 ? startValue : endValue
    }

    function updateDrag(endpoint, pointerY) {
        if (_dragEndpoint !== endpoint || travelLength <= 0)
            return

        var span = maximumValue - minimumValue
        var candidate = snappedValue(
                    _pressValue
                    - (pointerY - _pressPointerY) * span / travelLength)

        if (endpoint === 0) {
            // OrcaSlicer behavior: when the lower/start endpoint is dragged
            // above the upper/end endpoint, it pushes the end along with it.
            if (candidate > endValue)
                requestRange(candidate, candidate)
            else
                requestRange(candidate, endValue)
        } else {
            // Symmetrically, the upper/end endpoint pushes the start endpoint
            // when it is dragged below it.
            if (candidate < startValue)
                requestRange(candidate, candidate)
            else
                requestRange(startValue, candidate)
        }
    }

    function endDrag(endpoint) {
        if (_dragEndpoint === endpoint)
            _dragEndpoint = -1
    }

    function requestRange(start, end) {
        var validStart = snappedValue(start)
        var validEnd = snappedValue(end)
        if (validStart > validEnd)
            validStart = validEnd
        if (validStart === startValue && validEnd === endValue)
            return
        rangeMoved(validStart, validEnd)
    }

    property int _dragEndpoint: -1
    property real _pressPointerY: 0
    property int _pressValue: 0

    Rectangle {
        id: rail
        anchors.horizontalCenter: parent.horizontalCenter
        y: control.padding
        width: 4
        height: Math.max(0, control.height - 2 * control.padding)
        radius: width / 2
        color: control.railColor
    }

    Rectangle {
        anchors.horizontalCenter: rail.horizontalCenter
        y: control.centerYForValue(control.endValue)
        width: 6
        height: Math.max(0,
                         control.centerYForValue(control.startValue) - y)
        radius: width / 2
        color: control.rangeColor
    }

    Item {
        id: firstHandle
        objectName: control.objectName + ".firstHandle"
        x: control.width / 2 - width - control.handleGap
        y: control.centerYForValue(control.startValue) - height / 2
        width: control.handleWidth
        height: control.handleHeight
        z: firstMouseArea.pressed ? 3 : 2

        Loader {
            anchors.fill: parent
            sourceComponent: control.firstHandleContent
        }

        MouseArea {
            id: firstMouseArea
            z: 100
            anchors.fill: parent
            enabled: control.enabled && control.maximumValue > control.minimumValue
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeVerCursor

            onPressed: function(mouse) {
                var point = mapToItem(control, mouse.x, mouse.y)
                control.beginDrag(0, point.y)
                mouse.accepted = true
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                var point = mapToItem(control, mouse.x, mouse.y)
                control.updateDrag(0, point.y)
            }
            onReleased: function(mouse) {
                control.endDrag(0)
                mouse.accepted = true
            }
            onCanceled: control.endDrag(0)
        }
    }

    Item {
        id: secondHandle
        objectName: control.objectName + ".secondHandle"
        x: control.width / 2 + control.handleGap
        y: control.centerYForValue(control.endValue) - height / 2
        width: control.handleWidth
        height: control.handleHeight
        z: secondMouseArea.pressed ? 3 : 2

        Loader {
            anchors.fill: parent
            sourceComponent: control.secondHandleContent
        }

        MouseArea {
            id: secondMouseArea
            z: 100
            anchors.fill: parent
            enabled: control.enabled && control.maximumValue > control.minimumValue
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeVerCursor

            onPressed: function(mouse) {
                var point = mapToItem(control, mouse.x, mouse.y)
                control.beginDrag(1, point.y)
                mouse.accepted = true
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                var point = mapToItem(control, mouse.x, mouse.y)
                control.updateDrag(1, point.y)
            }
            onReleased: function(mouse) {
                control.endDrag(1)
                mouse.accepted = true
            }
            onCanceled: control.endDrag(1)
        }
    }
}
