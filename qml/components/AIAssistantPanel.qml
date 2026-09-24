import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0

AppPopup {
    id: control
    objectName: "aiAssistantPopup"

    SmoothUI.theme: Theme.of(control)

    property var inputHistory: []
    property int historyCursor: 0
    property string inputDraft: ""
    property bool userPositioned: false
    property real draggedX: 0
    property real draggedY: 0
    readonly property real edgeMargin: 16
    readonly property real centeredX: parent
                                      ? Math.round((parent.width - width) / 2) : 0
    readonly property real centeredY: parent
                                      ? Math.round((parent.height - height) / 2) : 0

    function boundedX(candidate) {
        if (!parent)
            return candidate
        var maximum = Math.max(edgeMargin, parent.width - width - edgeMargin)
        return Math.max(edgeMargin, Math.min(candidate, maximum))
    }

    function boundedY(candidate) {
        if (!parent)
            return candidate
        var maximum = Math.max(edgeMargin, parent.height - height - edgeMargin)
        return Math.max(edgeMargin, Math.min(candidate, maximum))
    }

    function requestClose() {
        if (!ActionManager.triggerAction("ui.ai.close"))
            AIChatBridge.closePanel()
    }

    function pushInputHistory(text) {
        if (text.length === 0)
            return
        if (inputHistory.length === 0 || inputHistory[inputHistory.length - 1] !== text)
            inputHistory.push(text)
        historyCursor = inputHistory.length
        inputDraft = ""
    }

    function moveInputHistory(step) {
        if (inputHistory.length === 0)
            return false
        if (step < 0 && inputArea.cursorPosition !== 0)
            return false
        if (step > 0 && inputArea.cursorPosition !== inputArea.text.length)
            return false
        if (historyCursor === inputHistory.length)
            inputDraft = inputArea.text

        var target = Math.max(0, Math.min(inputHistory.length, historyCursor + step))
        if (target === historyCursor)
            return false
        historyCursor = target
        inputArea.text = historyCursor === inputHistory.length
                       ? inputDraft : inputHistory[historyCursor]
        inputArea.cursorPosition = inputArea.text.length
        return true
    }

    function sendCurrentInput() {
        var text = inputArea.text.trim()
        if (text.length === 0 || AIChatBridge.streaming)
            return
        pushInputHistory(text)
        AIChatBridge.sendMessage(text)
        inputArea.clear()
    }

    width: Math.min(580, parent ? parent.width - 48 : 580)
    height: Math.max(400, Math.min(600, parent ? parent.height - 116 : 600))
    x: userPositioned
       ? boundedX(draggedX)
       : centeredX
    y: userPositioned
       ? boundedY(draggedY)
       : centeredY
    SmoothUI.radius: 14
    showHeader: false
    surfaceElevation: 10
    surfaceShadowOpacity: 0.30

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: control.opened
        autoRepeat: false
        onActivated: control.requestClose()
    }

    onOpened: {
        if (!AIChatBridge.panelVisible)
            AIChatBridge.openPanel()
        Qt.callLater(function() {
            if (control.opened) {
                if (AIAccountBridge.signedIn)
                    inputArea.forceActiveFocus(Qt.ShortcutFocusReason)
                else
                    popupContent.forceActiveFocus(Qt.PopupFocusReason)
            }
        })
    }

    onClosed: {
        if (AIChatBridge.panelVisible)
            AIChatBridge.closePanel()
    }

    Component.onCompleted: {
        if (AIChatBridge.panelVisible)
            open()
    }

    Connections {
        target: AIChatBridge

        function onPanelVisibleChanged() {
            if (AIChatBridge.panelVisible && !control.opened)
                control.open()
            else if (!AIChatBridge.panelVisible && control.opened)
                control.close()
        }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150 }
            NumberAnimation {
                property: "scale"
                from: 0.97
                to: 1
                duration: 180
                easing.type: Easing.OutCubic
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110 }
            NumberAnimation { property: "scale"; from: 1; to: 0.98; duration: 110 }
        }
    }

    ColumnLayout {
        id: popupContent
        anchors.fill: parent
        spacing: 0
        focus: true

        Pane {
            id: titleBar
            objectName: "aiAssistantTitleBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            padding: 0
            SmoothUI.radius: 0

            contentItem: Item {
                MouseArea {
                    id: titleBarDragArea
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    hoverEnabled: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

                    property real pressHostX: 0
                    property real pressHostY: 0
                    property real startPopupX: 0
                    property real startPopupY: 0

                    onPressed: function(mouse) {
                        var hostPoint = titleBar.mapToItem(
                                    control.parent, mouse.x, mouse.y)
                        pressHostX = hostPoint.x
                        pressHostY = hostPoint.y
                        startPopupX = control.x
                        startPopupY = control.y
                    }

                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        var hostPoint = titleBar.mapToItem(
                                    control.parent, mouse.x, mouse.y)
                        control.draggedX = control.boundedX(
                                    startPopupX + hostPoint.x - pressHostX)
                        control.draggedY = control.boundedY(
                                    startPopupY + hostPoint.y - pressHostY)
                        control.userPositioned = true
                    }
                }

                MenuSeparator {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 10
                    spacing: 10

                    Icon {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        source: FluentIcons.graph_ChatBubbles
                        color: control.SmoothUI.theme.accentColor.defaultBrushFor()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            text: qsTr("AI Assistant")
                            font: Typography.subtitle
                            color: control.SmoothUI.theme.res.textFillColorPrimary
                        }
                        Label {
                            text: AIChatBridge.streaming
                                  ? qsTr("Processing request…")
                                  : qsTr("Press Alt+I to open or collapse anytime")
                            font: Typography.caption
                            color: AIChatBridge.streaming
                                   ? control.SmoothUI.theme.accentColor.defaultBrushFor()
                                   : control.SmoothUI.theme.res.textFillColorSecondary
                        }
                    }

                    IconLabel {
                        text: AIAccountBridge.signedIn ? qsTr("Connected") : qsTr("Not Signed In")
                        spacing: 5
                        font: Typography.caption
                        color: AIAccountBridge.signedIn
                               ? control.SmoothUI.theme.res.systemFillColorSuccess
                               : control.SmoothUI.theme.res.textFillColorSecondary
                        icon.name: AIAccountBridge.signedIn
                                   ? FluentIcons.graph_Completed
                                   : FluentIcons.graph_StatusCircleOuter
                        icon.width: 12
                        icon.height: 12
                    }

                    IconButton {
                        implicitWidth: 30
                        implicitHeight: 30
                        flat: true
                        icon.name: FluentIcons.graph_Delete
                        icon.width: 14
                        icon.height: 14
                        enabled: !AIChatBridge.streaming
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Clear Conversation")
                        onClicked: AIChatBridge.clearMessages()
                    }

                    IconButton {
                        implicitWidth: 30
                        implicitHeight: 30
                        flat: true
                        icon.name: FluentIcons.graph_ChromeClose
                        icon.width: 13
                        icon.height: 13
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Close")
                        onClicked: control.requestClose()
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Item {
                anchors.fill: parent
                visible: !AIAccountBridge.signedIn

                ColumnLayout {
                    width: Math.min(parent.width - 48, 340)
                    anchors.centerIn: parent
                    spacing: 12

                    Icon {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 42
                        source: FluentIcons.graph_ChatBubbles
                        color: control.SmoothUI.theme.accentColor.defaultBrushFor()
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Sign in to use AI Assistant")
                        horizontalAlignment: Text.AlignHCenter
                        font: Typography.subtitle
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Sign in with a ChatGPT/Codex device code. Credentials are managed by Codex App Server.")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font: Typography.caption
                        color: control.SmoothUI.theme.res.textFillColorSecondary
                    }
                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Open Accounts and Services")
                        highlighted: true
                        onClicked: AIAccountBridge.openAccountCenter("ai")
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                visible: AIAccountBridge.signedIn

                ListView {
                    id: chatList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 7
                    clip: true
                    model: AIChatBridge.messages
                    reuseItems: true

                    delegate: Item {
                        width: chatList.width
                        height: messageBubble.implicitHeight + 5

                        property var entry: modelData
                        property string roleName: entry && entry.role ? entry.role : ""
                        property bool isUser: roleName === "user"
                        property bool isToolTrace: roleName === "tool_trace"
                        property string toolStatus: isToolTrace && entry && entry.status
                                                    ? entry.status : ""
                        property bool toolRunning: toolStatus === "running"
                        property bool toolFailed: toolStatus === "failed"
                        property bool toolExpanded: false
                        property var actionItems: roleName === "assistant" && entry && entry.actions
                                                  ? entry.actions : []
                        property int actionCount: actionItems ? actionItems.length : 0
                        property bool hasActions: actionCount > 0
                        property var actionExecution: roleName === "assistant" && entry && entry.actionExecution
                                                      ? entry.actionExecution : null
                        property bool actionsExpanded: false

                        onEntryChanged: {
                            toolExpanded = false
                            actionsExpanded = false
                        }

                        function formatJson(value) {
                            try {
                                return JSON.stringify(value, null, 2)
                            } catch (error) {
                                return "{}"
                            }
                        }

                        function valueOrDefault(value, fallback) {
                            return value === undefined || value === null ? fallback : value
                        }

                        Frame {
                            id: messageBubble
                            width: Math.floor(parent.width * (isToolTrace ? 0.92 : 0.86))
                            implicitHeight: messageColumn.implicitHeight + topPadding + bottomPadding
                            anchors.right: isUser ? parent.right : undefined
                            anchors.left: isUser ? undefined : parent.left
                            padding: 8
                            SmoothUI.radius: 10

                            contentItem: ColumnLayout {
                                id: messageColumn
                                spacing: 5

                                Button {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 28
                                    visible: isToolTrace
                                    flat: true
                                    padding: 0
                                    onClicked: toolExpanded = !toolExpanded

                                    contentItem: RowLayout {
                                        spacing: 7

                                        ProgressRing {
                                            Layout.preferredWidth: 16
                                            Layout.preferredHeight: 16
                                            indeterminate: true
                                            visible: toolRunning
                                        }
                                        Icon {
                                            Layout.preferredWidth: 15
                                            Layout.preferredHeight: 15
                                            visible: !toolRunning
                                            source: toolFailed
                                                    ? FluentIcons.graph_ErrorBadge
                                                    : FluentIcons.graph_Completed
                                            color: toolFailed
                                                   ? control.SmoothUI.theme.res.systemFillColorCritical
                                                   : control.SmoothUI.theme.res.systemFillColorSuccess
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: entry && entry.toolName
                                                  ? qsTr("Tool · %1").arg(entry.toolName)
                                                  : qsTr("Tool Call")
                                            font: Typography.caption
                                            color: control.SmoothUI.theme.res.textFillColorPrimary
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            text: toolRunning ? qsTr("Running")
                                                              : (toolFailed ? qsTr("Failed") : qsTr("Done"))
                                            font: Typography.caption
                                            color: control.SmoothUI.theme.res.textFillColorSecondary
                                        }
                                        Icon {
                                            Layout.preferredWidth: 12
                                            Layout.preferredHeight: 12
                                            source: toolExpanded
                                                    ? FluentIcons.graph_ChevronUp
                                                    : FluentIcons.graph_ChevronDown
                                            color: control.SmoothUI.theme.res.textFillColorSecondary
                                        }
                                    }
                                }

                                Frame {
                                    Layout.fillWidth: true
                                    visible: isToolTrace && toolExpanded
                                    padding: 6
                                    SmoothUI.radius: 7

                                    contentItem: ColumnLayout {
                                        id: toolDetails
                                        spacing: 4

                                        CopyableText {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: contentHeight
                                            text: entry && entry.arguments ? entry.arguments : "{}"
                                            selectByKeyboard: true
                                            wrapMode: TextEdit.WrapAnywhere
                                            textFormat: TextEdit.PlainText
                                            font.pixelSize: 10
                                            color: control.SmoothUI.theme.res.textFillColorSecondary
                                        }
                                        CopyableText {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: contentHeight
                                            text: toolRunning
                                                  ? qsTr("Waiting for tool result…")
                                                  : (entry && entry.resultPayload
                                                     ? entry.resultPayload : "{}")
                                            selectByKeyboard: true
                                            wrapMode: TextEdit.WrapAnywhere
                                            textFormat: TextEdit.PlainText
                                            font.pixelSize: 10
                                            color: toolFailed
                                                   ? control.SmoothUI.theme.res.systemFillColorCritical
                                                   : control.SmoothUI.theme.res.textFillColorSecondary
                                        }
                                    }
                                }

                                CopyableText {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: contentHeight
                                    visible: !isToolTrace
                                    text: entry && entry.text ? entry.text : ""
                                    selectByKeyboard: true
                                    wrapMode: TextEdit.Wrap
                                    textFormat: TextEdit.PlainText
                                    color: control.SmoothUI.theme.res.textFillColorPrimary
                                    font: Typography.body
                                }

                                Button {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 28
                                    visible: !isToolTrace && hasActions
                                    flat: true
                                    padding: 0
                                    onClicked: actionsExpanded = !actionsExpanded

                                    contentItem: RowLayout {
                                        spacing: 6

                                        Icon {
                                            Layout.preferredWidth: 14
                                            Layout.preferredHeight: 14
                                            source: actionExecution && !actionExecution.success
                                                    ? FluentIcons.graph_ErrorBadge
                                                    : FluentIcons.graph_CommandPrompt
                                            color: actionExecution && !actionExecution.success
                                                   ? control.SmoothUI.theme.res.systemFillColorCritical
                                                   : control.SmoothUI.theme.accentColor.defaultBrushFor()
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: qsTr("Apply Changes · %1 items").arg(actionCount)
                                            font: Typography.caption
                                            color: control.SmoothUI.theme.res.textFillColorPrimary
                                        }
                                        Label {
                                            visible: actionExecution !== null
                                            text: actionExecution && actionExecution.success
                                                  ? qsTr("Success") : qsTr("Failed")
                                            font: Typography.caption
                                            color: actionExecution && actionExecution.success
                                                   ? control.SmoothUI.theme.res.systemFillColorSuccess
                                                   : control.SmoothUI.theme.res.systemFillColorCritical
                                        }
                                        Icon {
                                            Layout.preferredWidth: 12
                                            Layout.preferredHeight: 12
                                            source: actionsExpanded
                                                    ? FluentIcons.graph_ChevronUp
                                                    : FluentIcons.graph_ChevronDown
                                            color: control.SmoothUI.theme.res.textFillColorSecondary
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    visible: !isToolTrace && hasActions && actionsExpanded

                                    Repeater {
                                        model: actionItems

                                        delegate: Frame {
                                            required property var modelData
                                            required property int index

                                            Layout.fillWidth: true
                                            padding: 6
                                            SmoothUI.radius: 7

                                            contentItem: ColumnLayout {
                                                id: actionStepColumn
                                                spacing: 3

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: qsTr("%1. %2")
                                                          .arg(index + 1)
                                                          .arg(modelData && modelData.actionCode
                                                               ? modelData.actionCode : "unknown")
                                                    font: Typography.caption
                                                    color: control.SmoothUI.theme.res.textFillColorPrimary
                                                }
                                                CopyableText {
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: contentHeight
                                                    text: modelData && modelData.params
                                                          ? formatJson(modelData.params) : "{}"
                                                    selectByKeyboard: true
                                                    wrapMode: TextEdit.WrapAnywhere
                                                    textFormat: TextEdit.PlainText
                                                    font.pixelSize: 10
                                                    color: control.SmoothUI.theme.res.textFillColorSecondary
                                                }
                                            }
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: actionExecution !== null
                                        text: actionExecution && actionExecution.success
                                              ? qsTr("Completed successfully (%1 steps)")
                                                    .arg(valueOrDefault(actionExecution.executedSteps, 0))
                                              : qsTr("Step %1 failed (%2): %3")
                                                    .arg(actionExecution
                                                         ? valueOrDefault(actionExecution.failedStepIndex, -1) : -1)
                                                    .arg(actionExecution
                                                         ? valueOrDefault(actionExecution.failedActionCode, "") : "")
                                                    .arg(actionExecution
                                                         ? valueOrDefault(actionExecution.error, "") : "")
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 10
                                        color: actionExecution && actionExecution.success
                                               ? control.SmoothUI.theme.res.systemFillColorSuccess
                                               : control.SmoothUI.theme.res.systemFillColorCritical
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: entry && entry.time ? entry.time : ""
                                    horizontalAlignment: isUser ? Text.AlignRight : Text.AlignLeft
                                    font.pixelSize: 10
                                    color: control.SmoothUI.theme.res.textFillColorTertiary
                                }
                            }
                        }
                    }

                    onCountChanged: {
                        if (count > 0)
                            Qt.callLater(positionViewAtEnd)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    visible: AIChatBridge.streaming

                    ProgressRing {
                        Layout.preferredWidth: 17
                        Layout.preferredHeight: 17
                        indeterminate: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("AI is processing your request…")
                        font: Typography.caption
                        color: control.SmoothUI.theme.res.textFillColorSecondary
                    }
                    Button {
                        text: qsTr("Stop")
                        flat: true
                        onClicked: AIChatBridge.cancelStreamingRequest("user_stop")
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    padding: 8
                    SmoothUI.radius: 10

                    contentItem: ColumnLayout {
                        id: composer
                        spacing: 6

                        TextArea {
                            id: inputArea
                            objectName: "aiAssistantInput"
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.max(38, Math.min(72, contentHeight + 14))
                            wrapMode: TextArea.Wrap
                            placeholderText: qsTr("Ask AI or perform an action…")

                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    if (!(event.modifiers & Qt.ShiftModifier)) {
                                        control.sendCurrentInput()
                                        event.accepted = true
                                    }
                                } else if (event.key === Qt.Key_Up) {
                                    if (control.moveInputHistory(-1))
                                        event.accepted = true
                                } else if (event.key === Qt.Key_Down) {
                                    if (control.moveInputHistory(1))
                                        event.accepted = true
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Enter to send · Shift+Enter for a new line · Esc / Alt+I to collapse")
                                font.pixelSize: 10
                                color: control.SmoothUI.theme.res.textFillColorTertiary
                            }
                            Button {
                                text: qsTr("Retry")
                                flat: true
                                visible: !AIChatBridge.streaming
                                onClicked: AIChatBridge.retryTurn()
                            }
                            Button {
                                text: qsTr("Send")
                                highlighted: true
                                enabled: !AIChatBridge.streaming && inputArea.text.trim().length > 0
                                onClicked: control.sendCurrentInput()
                            }
                        }
                    }
                }
            }
        }
    }
}
