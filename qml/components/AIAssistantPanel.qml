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
    property bool guideMode: false
    focus: !guideMode
    closePolicy: guideMode || inputArea.inputMethodComposing ? Popup.NoAutoClose : Popup.CloseOnEscape

    readonly property Item guidePanelItem: popupContent
    function guideSummary() {
        return { visible: AIChatBridge.panelVisible, guideMode: guideMode,
                 draftHash: Qt.md5(inputArea.text), historyCount: AIChatBridge.messages.length,
                 historyHash: Qt.md5(JSON.stringify(AIChatBridge.messages)),
                 streaming: AIChatBridge.streaming, turn: AIChatBridge.activeTurnId() }
    }
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
        if (text.length === 0 || AIChatBridge.streaming || !AIAccountBridge.signedIn || inputArea.inputMethodComposing)
            return
        pushInputHistory(text)
        chatList.showLatest()
        AIChatBridge.sendMessage(text)
        inputArea.clear()
    }

    // Bridge snapshots replace the QVariantList on each token. Apply only changed
    // rows so existing message delegates keep their selection and detail state.
    function syncMessageRows() {
        var source = AIChatBridge.messages
        var byTurn = {}
        var assistantTurns = {}
        for (var i = 0; i < source.length; ++i) {
            var record = source[i]
            if (record.role === "assistant" && record.turnId)
                assistantTurns[record.turnId] = true
            if (record.role === "tool_trace" && record.turnId) {
                if (!byTurn[record.turnId]) byTurn[record.turnId] = []
                byTurn[record.turnId].push(record)
            }
        }
        var messages = []
        for (var i = 0; i < source.length; ++i) {
            var record = source[i]
            if (record.role === "tool_trace") {
                if (!record.turnId || !assistantTurns[record.turnId]) {
                    // Preserve legacy/unassigned records without guessing a turn.
                    messages.push({role: "assistant", text: "", unassigned: true,
                                      toolTraces: [record], time: record.time})
                }
            } else {
                var row = Object.assign({}, record)
                if (row.role === "assistant") row.toolTraces = byTurn[row.turnId] || []
                messages.push(row)
            }
        }
        for (var i = 0; i < messages.length; ++i) {
            var payload = JSON.stringify(messages[i])
            if (i >= messageRows.count)
                messageRows.append({ entryPayload: payload })
            else if (messageRows.get(i).entryPayload !== payload)
                messageRows.setProperty(i, "entryPayload", payload)
        }
        if (messageRows.count > messages.length)
            messageRows.remove(messages.length, messageRows.count - messages.length)
        if (messages.length === 0) {
            chatList.following = true
            chatList.historyY = 0
        }
        Qt.callLater(chatList.restorePosition)
    }

    function describedActionTitle(actionCode) {
        if (!actionCode) return ""
        // Reuse descriptions already returned by the real tool protocol. Looking
        // up a label must not invoke an action or run another admission check.
        var messages = AIChatBridge.messages
        for (var i = messages.length - 1; i >= 0; --i) {
            var message = messages[i]
            if (message.role !== "tool_trace" || message.toolName !== "action_describe") continue
            var actionCodeInRequest = message.actionCode || ""
            if (!actionCodeInRequest) {
                try { actionCodeInRequest = JSON.parse(message.arguments || "{}").actionCode } catch (error) {}
            }
            if (actionCodeInRequest === actionCode && message.actionTitle)
                return qsTranslate("AIAction", message.actionTitle)
            try {
                var result = JSON.parse(message.resultPayload || "{}")
                var descriptor = result.descriptor || {}
                if (result.actionCode === actionCode && descriptor.title)
                    return qsTranslate("AIAction", descriptor.title)
            } catch (error) {}
        }
        return ""
    }

    ListModel { id: messageRows }

    width: Math.max(0, Math.min(580, parent ? parent.width - 32 : 580))
    height: Math.max(0, Math.min(600, parent ? parent.height - 32 : 600))
    x: userPositioned
       ? boundedX(draggedX)
       : centeredX
    y: userPositioned
       ? boundedY(draggedY)
       : centeredY
    SmoothUI.radius: Theme.dialogRadius
    showHeader: false
    surfaceElevation: 10
    surfaceShadowOpacity: 0.30

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: control.opened && !control.guideMode && !inputArea.inputMethodComposing && !clearConfirmation.opened
        autoRepeat: false
        onActivated: control.requestClose()
    }

    onOpened: {
        if (!AIChatBridge.panelVisible)
            AIChatBridge.openPanel()
        Qt.callLater(function() {
            if (control.opened && !control.guideMode) {
                if (AIAccountBridge.signedIn)
                    inputArea.forceActiveFocus(Qt.ShortcutFocusReason)
                else
                    popupContent.forceActiveFocus(Qt.PopupFocusReason)
            }
        })
    }

    onClosed: {
        conversationMenu.close()
        clearConfirmation.close()
        if (AIChatBridge.panelVisible)
            AIChatBridge.closePanel()
    }

    Component.onCompleted: {
        syncMessageRows()
        if (AIChatBridge.panelVisible)
            open()
    }

    Connections {
        target: AIChatBridge

        function onMessagesChanged() { control.syncMessageRows() }

        function onPanelVisibleChanged() {
            if (AIChatBridge.panelVisible && !control.opened)
                control.open()
            else if (!AIChatBridge.panelVisible && control.opened)
                control.close()
        }
    }

    AppPopup {
        id: clearConfirmation
        parent: control.parent
        preferredWidth: 320
        preferredHeight: 160
        title: qsTr("Clear conversation?")
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            Label { text: qsTr("This removes the current chat history."); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: qsTr("Keep"); onClicked: clearConfirmation.close() }
                Button {
                    text: qsTr("Clear")
                    enabled: !AIChatBridge.streaming
                    onClicked: { AIChatBridge.clearMessages(); chatList.showLatest(); clearConfirmation.close() }
                }
            }
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
        objectName: "aiAssistantContent"
        anchors.fill: parent
        spacing: 0
        focus: true

        Pane {
            id: titleBar
            objectName: "aiAssistantTitleBar"
            background: null
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            padding: 0
            SmoothUI.radius: 0

            contentItem: Item {
                MouseArea {
                    id: titleBarDragArea
                    objectName: "aiAssistantDragHandle"
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
                    anchors.leftMargin: 15
                    anchors.rightMargin: 15
                    spacing: 9

                    Rectangle {
                        Layout.minimumWidth: 33
                        Layout.maximumWidth: 33
                        Layout.minimumHeight: 33
                        Layout.maximumHeight: 33
                        radius: 8
                        color: Qt.alpha(control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark), 0.10)
                        Icon {
                            anchors.centerIn: parent
                            width: 16
                            height: 16
                            source: FluentIcons.graph_Message
                            color: control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark)
                        }
                    }

                    ColumnLayout {
                        Layout.maximumWidth: implicitWidth
                        spacing: 1

                        Label {
                            text: qsTr("AI Assistant")
                            font.pixelSize: Typography.subtitle.pixelSize
                            font.weight: Font.Bold
                            color: control.SmoothUI.theme.res.textFillColorPrimary
                        }
                        Label {
                            text: qsTr("Alt+I to open or collapse")
                            font.pixelSize: 11
                            color: control.SmoothUI.theme.res.textFillColorTertiary
                        }
                    }

                    Item { Layout.fillWidth: true }

                    RowLayout {
                        Layout.maximumWidth: implicitWidth
                        spacing: 5
                        Rectangle {
                            Layout.minimumWidth: 6
                            Layout.maximumWidth: 6
                            Layout.minimumHeight: 6
                            Layout.maximumHeight: 6
                            radius: 3
                            color: AIAccountBridge.signedIn
                                   ? control.SmoothUI.theme.res.systemFillColorSuccess
                                   : control.SmoothUI.theme.res.textFillColorTertiary
                        }
                        Label {
                            text: AIAccountBridge.signedIn ? qsTr("Connected") : qsTr("Not Signed In")
                            font: Typography.caption
                            color: control.SmoothUI.theme.res.textFillColorTertiary
                        }
                    }

                    IconButton {
                        Layout.minimumWidth: 32
                        Layout.maximumWidth: 32
                        Layout.minimumHeight: 32
                        Layout.maximumHeight: 32
                        padding: 0
                        flat: true
                        objectName: "aiAssistantMenu"
                        icon.name: FluentIcons.graph_More
                        icon.width: 14
                        icon.height: 14
                        enabled: !AIChatBridge.streaming
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Clear Conversation")
                        onClicked: conversationMenu.open()

                        Menu {
                            id: conversationMenu
                            x: parent.width - width
                            y: parent.height + 2
                            width: clearConversationItem.implicitContentWidth
                                   + clearConversationItem.leftPadding + clearConversationItem.rightPadding
                                   + leftPadding + rightPadding
                            MenuItem {
                                id: clearConversationItem
                                objectName: "aiAssistantClearConversation"
                                font.pixelSize: 12
                                padding: 8
                                horizontalPadding: 11
                                height: implicitContentHeight + topPadding + bottomPadding
                                text: qsTr("Clear Conversation")
                                enabled: !AIChatBridge.streaming
                                onTriggered: clearConfirmation.open()
                            }
                        }
                    }

                    IconButton {
                        Layout.minimumWidth: 32
                        Layout.maximumWidth: 32
                        Layout.minimumHeight: 32
                        Layout.maximumHeight: 32
                        padding: 0
                        flat: true
                        objectName: "aiAssistantClose"
                        icon.name: FluentIcons.graph_ChromeClose
                        icon.width: 11
                        icon.height: 11
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

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Item {
                        anchors.fill: parent
                        visible: !AIAccountBridge.signedIn

                        ColumnLayout {
                            width: Math.min(parent.width - 32, 282)
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: 16
                            spacing: 0

                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 45
                                Layout.preferredHeight: 45
                                Layout.bottomMargin: 13
                                radius: 13
                                color: Qt.alpha(control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark), 0.10)
                                border.color: Qt.alpha(control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark), 0.25)
                                Icon {
                                    anchors.centerIn: parent
                                    width: 19
                                    height: 19
                                    source: FluentIcons.graph_Message
                                    color: control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark)
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.bottomMargin: 7
                                text: qsTr("Sign in to start chatting")
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 21
                                font.weight: Font.Bold
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.bottomMargin: 15
                                text: qsTr("Connect your account to ask about slicing settings or perform actions in the workspace.")
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                font.pixelSize: 13
                                lineHeightMode: Text.FixedHeight
                                lineHeight: 21.45
                                color: control.SmoothUI.theme.res.textFillColorSecondary
                            }
                            Button {
                                objectName: "aiAssistantSignIn"
                                Layout.alignment: Qt.AlignHCenter
                                Layout.minimumHeight: 32
                                Layout.maximumHeight: 32
                                text: qsTr("Sign In")
                                font.pixelSize: 13
                                horizontalPadding: 10
                                SmoothUI.textColor: control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark)
                                onClicked: AIAccountBridge.openAccountCenter("ai")
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(320, parent.width - 32)
                        spacing: 12
                        visible: AIAccountBridge.signedIn && AIChatBridge.messages.length === 0
                        Icon {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                            source: FluentIcons.graph_ChatBubbles
                            color: control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark)
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("How can I help?")
                            font: Typography.subtitle
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Ask about your model, review slicing settings, or request an action.")
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            color: control.SmoothUI.theme.res.textFillColorSecondary
                        }
                        Repeater {
                            model: [qsTr("Review current slicing settings"), qsTr("Find and import a model"), qsTr("What should I check before slicing?")]
                            Button {
                                required property string modelData
                                Layout.fillWidth: true
                                text: modelData
                                onClicked: {
                                    inputArea.text = modelData
                                    inputArea.forceActiveFocus()
                                }
                            }
                        }
                    }
                    Flickable {
                        id: chatList
                        objectName: "aiAssistantTranscript"
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        anchors.topMargin: 16
                        anchors.bottomMargin: 2
                        visible: AIAccountBridge.signedIn && AIChatBridge.messages.length > 0
                        clip: true
                        contentWidth: width
                        contentHeight: transcriptColumn.height
                        boundsBehavior: Flickable.StopAtBounds
                        property bool following: true
                        property real historyY: 0
                        property bool restoringPosition: false
                        readonly property real endY: Math.max(0, contentHeight - height)
                        function showLatest() {
                            following = true;
                            contentY = endY;
                        }
                        function restorePosition() {
                            if (moving || dragging || scrollBar.pressed || scrollIntent.active)
                                return
                            restoringPosition = true
                            contentY = following ? endY : Math.min(historyY, endY)
                            restoringPosition = false
                        }
                        onContentHeightChanged: Qt.callLater(restorePosition)
                        onHeightChanged: Qt.callLater(restorePosition)
                        onContentYChanged: {
                            if (!restoringPosition && (moving || dragging || scrollBar.pressed || scrollIntent.active)) {
                                following = endY - contentY < 24;
                                historyY = Math.max(0, contentY);
                            }
                        }
                        WheelHandler {
                            id: scrollIntent
                            target: null
                            onWheel: function(event) {
                                // Consume the wheel here so streaming relayout cannot interrupt
                                // Flickable's deferred wheel motion and resume following history.
                                var delta = event.pixelDelta.y || event.angleDelta.y / 120
                                        * Qt.styleHints.wheelScrollLines * Typography.body.pixelSize
                                chatList.cancelFlick()
                                chatList.contentY = Math.max(0, Math.min(chatList.endY, chatList.contentY - delta))
                                chatList.historyY = chatList.contentY
                                chatList.following = chatList.endY - chatList.contentY < 24
                                event.accepted = true
                            }
                            onActiveChanged: {
                                if (!active)
                                    Qt.callLater(chatList.restorePosition)
                            }
                        }
                        ScrollBar.vertical: ScrollBar {
                            id: scrollBar
                        }
                        Column {
                            id: transcriptColumn
                            width: chatList.width
                            spacing: 14
                            Repeater {
                                model: messageRows

                                delegate: Item {
                                    required property string entryPayload
                                    required property int index
                                    width: transcriptColumn.width
                                    height: messageBubble.implicitHeight + 5

                                    property var entry: JSON.parse(entryPayload)
                                    property string roleName: entry && entry.role ? entry.role : ""
                                    readonly property string turnKey: entry && entry.turnId ? entry.turnId : "unassigned_" + index
                                    property bool isUser: roleName === "user"
                                    property bool turnFailed: !isUser && entry && typeof entry.turnStatus === "string"
                                                              && entry.turnStatus.indexOf("failed") === 0
                                                              && entry.turnError !== "request canceled"
                                    property bool operationsExpanded: false
                                    property var actionItems: entry && entry.actions ? entry.actions : []
                                    property var actionExecution: entry && entry.actionExecution ? entry.actionExecution : null
                                    property var traces: entry && entry.toolTraces ? entry.toolTraces : []
                                    // Tool return and action receipt states are display projections only.
                                    // In particular, acceptance/dispatch is not completion.
                                    function parsedObject(text) {
                                        try { return JSON.parse(text || "{}") } catch (error) { return {} }
                                    }
                                    readonly property bool hasActionTraces: traces.some(function(trace) {
                                        return trace.toolName === "action_invoke" || trace.toolName === "db_patch"
                                    })
                                    readonly property var operations: {
                                        var rows = traces.slice()
                                        // Current Agent actionItems/receipt mirror action_invoke/db_patch traces.
                                        // Only legacy responses without those traces need a fallback row.
                                        if (!hasActionTraces) {
                                            for (var i = 0; i < actionItems.length; ++i)
                                                rows.push({toolName: "action_invoke", action: actionItems[i],
                                                           receipt: i === actionItems.length - 1 ? actionExecution : null,
                                                           status: "unknown"})
                                            if (!actionItems.length && actionExecution)
                                                rows.push({toolName: "action_receipt", receipt: actionExecution, status: "unknown"})
                                        }
                                        return rows
                                    }
                                    function operationState(operation) {
                                        var receipt = operation.receipt || parsedObject(operation.resultPayload)
                                        // FileImportHandler completes routing synchronously. Its concrete
                                        // tracked import outcome owns the business completion state.
                                        var result = receipt.result && receipt.result.importOutcome
                                                     ? receipt.result.importOutcome : receipt
                                        if (receipt.pending) return "waiting"
                                        if (result.pending || result.status === "running" || result.status === "dispatched"
                                                || result.status === "accepted" || (result.accepted && result.status !== "completed")) return "waiting"
                                        if (result.status === "cancelled" || result.status === "canceled") return "stopped"
                                        if (operation.status === "failed" || result.success === false) return "failed"
                                        if (operation.status === "running")
                                            return entry.turnStatus === "running" ? "waiting"
                                                   : entry.turnError === "request canceled" ? "stopped" : "unknown"
                                        if (result.status === "completed") return "completed"
                                        if (operation.status === "success")
                                            return operation.toolName === "action_invoke" ? "unknown" : "completed"
                                        // A legacy aggregate receipt only proves its own reported result.
                                        if (operation.receipt && operation.receipt.success === true) return "completed"
                                        return "unknown"
                                    }
                                    function operationTitle(operation) {
                                        var args = operation.action || parsedObject(operation.arguments)
                                        var title = control.describedActionTitle(operation.receipt && operation.receipt.actionCode
                                                                                 ? operation.receipt.actionCode : args.actionCode)
                                        if (operation.toolName === "action_invoke" && title) return title
                                        if (operation.toolName === "action_describe" && title)
                                            return qsTr("Review instructions: %1").arg(title)
                                        switch (operation.toolName) {
                                        case "action_list": return qsTr("Review available operations")
                                        case "action_describe": return qsTr("Review operation instructions")
                                        case "action_invoke": return qsTr("Run workspace operation")
                                        case "db_query": return qsTr("Query workspace data")
                                        case "db_patch": return qsTr("Update workspace data")
                                        case "action_receipt": return qsTr("Operation result")
                                        default: return qsTr("Use workspace tool")
                                        }
                                    }
                                    function operationStateText(state) {
                                        switch (state) {
                                        case "completed": return qsTr("Completed")
                                        case "waiting": return qsTr("Waiting for result")
                                        case "failed": return qsTr("Failed")
                                        case "stopped": return qsTr("Stopped")
                                        default: return qsTr("No result received")
                                        }
                                    }
                                    readonly property int completedOperations: {
                                        var count = 0
                                        for (var i = 0; i < operations.length; ++i)
                                            if (operationState(operations[i]) === "completed") ++count
                                        return count
                                    }
                                    readonly property string operationStatus: completedOperations === operations.length
                                        ? qsTr("%1 completed").arg(completedOperations)
                                        : qsTr("%1 / %2 completed").arg(completedOperations).arg(operations.length)
                                    ListModel { id: toolRows }
                                    function syncToolRows() {
                                        for (var i = 0; i < operations.length; ++i) {
                                            var payload = JSON.stringify(operations[i])
                                            if (i >= toolRows.count) toolRows.append({tracePayload: payload})
                                            else if (toolRows.get(i).tracePayload !== payload)
                                                toolRows.setProperty(i, "tracePayload", payload)
                                        }
                                        if (toolRows.count > operations.length) toolRows.remove(operations.length, toolRows.count - operations.length)
                                    }
                                    onOperationsChanged: syncToolRows()
                                    Component.onCompleted: syncToolRows()

                                    function formatJson(value) {
                                        try {
                                            return JSON.stringify(value, null, 2);
                                        } catch (error) {
                                            return "{}";
                                        }
                                    }

                                    Frame {
                                        id: messageBubble
                                        width: Math.floor(parent.width * (isUser ? 0.91 : 1.0))
                                        implicitHeight: messageColumn.implicitHeight + topPadding + bottomPadding
                                        anchors.right: isUser ? parent.right : undefined
                                        anchors.left: isUser ? undefined : parent.left
                                        padding: isUser ? 12 : 0
                                        background: Rectangle {
                                            radius: 11
                                            color: isUser ? Qt.alpha(control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark), 0.10) : "transparent"
                                            border.width: isUser ? 1 : 0
                                            border.color: isUser ? Qt.alpha(control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark), 0.25) : control.SmoothUI.theme.res.cardStrokeColorDefault
                                        }
                                        SmoothUI.radius: Theme.panelRadius

                                        contentItem: ColumnLayout {
                                            id: messageColumn
                                            spacing: isUser ? 7 : 5

                                            RowLayout {
                                                Layout.fillWidth: true
                                                visible: !isUser
                                                spacing: 6
                                                Rectangle {
                                                    Layout.minimumWidth: 19
                                                    Layout.maximumWidth: 19
                                                    Layout.minimumHeight: 19
                                                    Layout.maximumHeight: 19
                                                    radius: 5
                                                    color: Qt.alpha(control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark), 0.10)
                                                    Icon {
                                                        anchors.centerIn: parent
                                                        width: 11
                                                        height: 11
                                                        source: FluentIcons.graph_Message
                                                        color: control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark)
                                                    }
                                                }
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: qsTr("AI Assistant")
                                                    font.pixelSize: 11
                                                    font.weight: Font.DemiBold
                                                    color: control.SmoothUI.theme.res.textFillColorPrimary
                                                }
                                            }
                                            RowLayout {
                                                id: waitingLine
                                                objectName: "aiAssistantWaiting_" + index
                                                Layout.leftMargin: 25
                                                Layout.minimumHeight: 24
                                                spacing: 7
                                                visible: !isUser && entry && entry.turnStatus === "running" && !entry.text
                                                Item {
                                                    Layout.minimumWidth: 17
                                                    Layout.maximumWidth: 17
                                                    Layout.preferredHeight: 24
                                                    Repeater {
                                                        model: 3
                                                        Rectangle {
                                                            id: waitingDot
                                                            required property int index
                                                            objectName: "aiAssistantWaitingDot_" + index
                                                            width: 3.75
                                                            height: 3.75
                                                            radius: width / 2
                                                            x: index * (3.75 + 3)
                                                            y: (parent.height - height) / 2 + lift
                                                            color: control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark)
                                                            property real lift: 0
                                                            opacity: 0.34
                                                            // CSS ease-in-out is cubic-bezier(.42, 0, .58, 1).
                                                            SequentialAnimation {
                                                                objectName: "aiAssistantWaitingAnimation_" + waitingDot.index
                                                                running: control.opened && waitingLine.visible && waitingDot.visible
                                                                onRunningChanged: if (!running) { waitingDot.lift = 0; waitingDot.opacity = 0.34 }
                                                                PauseAnimation { duration: waitingDot.index * 130 }
                                                                ParallelAnimation {
                                                                    loops: Animation.Infinite
                                                                    SequentialAnimation {
                                                                        NumberAnimation { target: waitingDot; property: "lift"; from: 0; to: -2; duration: 480; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1] }
                                                                        NumberAnimation { target: waitingDot; property: "lift"; from: -2; to: 0; duration: 480; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1] }
                                                                        PauseAnimation { duration: 240 }
                                                                    }
                                                                    SequentialAnimation {
                                                                        NumberAnimation { target: waitingDot; property: "opacity"; from: 0.34; to: 1; duration: 480; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1] }
                                                                        NumberAnimation { target: waitingDot; property: "opacity"; from: 1; to: 0.34; duration: 480; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1] }
                                                                        PauseAnimation { duration: 240 }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                                Label {
                                                    text: operations.some(function(operation) { return operationState(operation) === "waiting" })
                                                          ? qsTr("Waiting for operation results…") : qsTr("Preparing reply…")
                                                    font: Typography.body
                                                    color: control.SmoothUI.theme.res.textFillColorSecondary
                                                }
                                            }
                                            CopyableText {
                                                id: replyText
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: isUser ? contentHeight : Math.max(24, contentHeight)
                                                Layout.leftMargin: isUser ? 0 : 25
                                                objectName: "aiAssistantMessageBody_" + index
                                                visible: entry && entry.text && entry.text.length > 0 && (!turnFailed || entry.text !== entry.turnError)
                                                text: index === 0 && roleName === "assistant" && entry && !entry.turnId && entry.text === "AI Assistant ready. You can start chatting now." ? qsTr("AI Assistant ready. You can start chatting now.") : (entry && entry.text ? entry.text : "")
                                                selectByKeyboard: true
                                                wrapMode: TextEdit.WrapAnywhere
                                                textFormat: TextEdit.PlainText
                                                color: control.SmoothUI.theme.res.textFillColorPrimary
                                                font: Typography.body
                                                AIChatTextLayout {
                                                    document: replyText.textDocument
                                                    lineHeight: isUser ? 0 : replyText.font.pixelSize * 1.58
                                                }
                                            }

                                            Frame {
                                                Layout.fillWidth: true
                                                Layout.leftMargin: 25
                                                Layout.topMargin: 3
                                                visible: !isUser && operations.length > 0
                                                padding: 0
                                                background: Rectangle {
                                                    radius: 9
                                                    color: control.SmoothUI.theme.res.controlAltFillColorSecondary
                                                    border.width: 1
                                                    border.color: control.SmoothUI.theme.res.cardStrokeColorDefault
                                                }
                                                contentItem: ColumnLayout {
                                                    spacing: 0
                                                    Button {
                                                        objectName: "aiAssistantOperations_" + index
                                                        Layout.fillWidth: true
                                                        Layout.preferredHeight: 32
                                                        flat: true
                                                        horizontalPadding: 9
                                                        verticalPadding: 7
                                                        onClicked: {
                                                            chatList.following = false
                                                            chatList.historyY = chatList.contentY
                                                            operationsExpanded = !operationsExpanded
                                                        }
                                                        contentItem: RowLayout {
                                                            spacing: 5
                                                            Label {
                                                                text: entry.unassigned ? qsTr("Unassigned operation") : qsTr("This turn's operations")
                                                                font.pixelSize: 11
                                                                font.weight: Font.DemiBold
                                                            }
                                                            Label {
                                                                Layout.fillWidth: true
                                                                text: operationStatus
                                                                font.pixelSize: 11
                                                                color: control.SmoothUI.theme.res.textFillColorSecondary
                                                                horizontalAlignment: Text.AlignHCenter
                                                                elide: Text.ElideRight
                                                            }
                                                            Icon {
                                                                Layout.preferredWidth: 13
                                                                Layout.preferredHeight: 13
                                                                source: operationsExpanded ? FluentIcons.graph_ChevronUp : FluentIcons.graph_ChevronDown
                                                                color: control.SmoothUI.theme.res.textFillColorTertiary
                                                            }
                                                        }
                                                    }
                                                    ColumnLayout {
                                                        visible: operationsExpanded
                                                        Layout.fillWidth: true
                                                        Layout.leftMargin: 8
                                                        Layout.rightMargin: 8
                                                        Layout.topMargin: 2
                                                        Layout.bottomMargin: 7
                                                        spacing: 0
                                                        Repeater {
                                                            model: toolRows
                                                            delegate: ColumnLayout {
                                                                id: operationRow
                                                                required property string tracePayload
                                                                required property int index
                                                                property var trace: JSON.parse(tracePayload)
                                                                readonly property string state: operationState(trace)
                                                                property bool detailsExpanded: false
                                                                objectName: "aiAssistantToolDetails_" + turnKey + "_" + (trace.traceId || index)
                                                                Layout.fillWidth: true
                                                                spacing: 4
                                                                Button {
                                                                    objectName: "aiAssistantOperationStep_" + turnKey + "_" + operationRow.index
                                                                    Layout.fillWidth: true
                                                                    Layout.preferredHeight: 22
                                                                    flat: true
                                                                    padding: 2
                                                                    onClicked: {
                                                                        chatList.following = false
                                                                        chatList.historyY = chatList.contentY
                                                                        operationRow.detailsExpanded = !operationRow.detailsExpanded
                                                                    }
                                                                    ToolTip.text: operationRow.detailsExpanded ? qsTr("Hide operation details") : qsTr("Show operation details")
                                                                    ToolTip.visible: hovered
                                                                    contentItem: RowLayout {
                                                                        spacing: 7
                                                                        Label {
                                                                            Layout.minimumWidth: 13
                                                                            Layout.maximumWidth: 13
                                                                            horizontalAlignment: Text.AlignHCenter
                                                                            text: operationRow.state === "completed" ? "✓" : operationRow.state === "failed" ? "!" : operationRow.state === "waiting" ? "●" : "−"
                                                                            font.pixelSize: 11
                                                                            font.weight: Font.Bold
                                                                            color: operationRow.state === "completed" ? control.SmoothUI.theme.res.systemFillColorSuccess
                                                                                : operationRow.state === "failed" ? control.SmoothUI.theme.res.systemFillColorCritical
                                                                                : operationRow.state === "waiting" ? control.SmoothUI.theme.accentColor.defaultBrushFor(control.SmoothUI.dark)
                                                                                : control.SmoothUI.theme.res.textFillColorSecondary
                                                                        }
                                                                        Label {
                                                                            objectName: "aiAssistantOperationName_" + turnKey + "_" + operationRow.index
                                                                            Layout.fillWidth: true
                                                                            text: operationTitle(operationRow.trace) + (operationRow.state === "completed" ? "" : " · " + operationStateText(operationRow.state))
                                                                            font.pixelSize: 11
                                                                            color: control.SmoothUI.theme.res.textFillColorSecondary
                                                                            elide: Text.ElideRight
                                                                        }
                                                                    }
                                                                }
                                                                ColumnLayout {
                                                                    visible: operationRow.detailsExpanded
                                                                    Layout.fillWidth: true
                                                                    Layout.leftMargin: 22
                                                                    Layout.bottomMargin: 4
                                                                    spacing: 4
                                                                    CopyableText {
                                                                        Layout.fillWidth: true
                                                                        Layout.preferredHeight: contentHeight
                                                                        text: operationRow.trace.action ? formatJson(operationRow.trace.action) : operationRow.trace.toolName || ""
                                                                        wrapMode: TextEdit.WrapAnywhere
                                                                        textFormat: TextEdit.PlainText
                                                                        selectByKeyboard: true
                                                                        font.pixelSize: 10
                                                                    }
                                                                    CopyableText {
                                                                        Layout.fillWidth: true
                                                                        Layout.preferredHeight: contentHeight
                                                                        visible: !!operationRow.trace.arguments
                                                                        text: operationRow.trace.arguments || ""
                                                                        wrapMode: TextEdit.WrapAnywhere
                                                                        textFormat: TextEdit.PlainText
                                                                        selectByKeyboard: true
                                                                        font.pixelSize: 10
                                                                    }
                                                                    CopyableText {
                                                                        objectName: "aiAssistantOperationReceipt_" + turnKey + "_" + operationRow.index
                                                                        Layout.fillWidth: true
                                                                        Layout.preferredHeight: contentHeight
                                                                        text: operationRow.trace.receipt ? formatJson(operationRow.trace.receipt) : operationRow.trace.resultPayload || operationRow.trace.error || ""
                                                                        wrapMode: TextEdit.WrapAnywhere
                                                                        textFormat: TextEdit.PlainText
                                                                        selectByKeyboard: true
                                                                        font.pixelSize: 10
                                                                    }
                                                                    Label {
                                                                        text: operationRow.trace.time || ""
                                                                        visible: text.length > 0
                                                                        font.pixelSize: 10
                                                                        color: control.SmoothUI.theme.res.textFillColorTertiary
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            Frame {
                                                Layout.fillWidth: true
                                                Layout.leftMargin: 25
                                                visible: !isUser && (turnFailed || (entry && entry.turnError === "request canceled"))
                                                padding: turnFailed ? 9 : 0
                                                background: Rectangle {
                                                    radius: 9
                                                    color: turnFailed ? Qt.alpha(control.SmoothUI.theme.res.systemFillColorCritical, 0.06) : "transparent"
                                                    border.width: turnFailed ? 1 : 0
                                                    border.color: Qt.alpha(control.SmoothUI.theme.res.systemFillColorCritical, 0.30)
                                                }
                                                contentItem: ColumnLayout {
                                                    spacing: 7
                                                    Label {
                                                        objectName: "aiAssistantErrorTitle_" + index
                                                        Layout.fillWidth: true
                                                        text: turnFailed ? qsTr("Request failed") : qsTr("Stopped")
                                                        font.pixelSize: 12
                                                        font.weight: Font.DemiBold
                                                        color: turnFailed ? control.SmoothUI.theme.res.systemFillColorCritical : control.SmoothUI.theme.res.textFillColorSecondary
                                                    }
                                                    CopyableText {
                                                        Layout.fillWidth: true
                                                        Layout.preferredHeight: contentHeight
                                                        visible: turnFailed
                                                        text: entry && entry.turnError ? entry.turnError : qsTr("No error details were provided.")
                                                        wrapMode: TextEdit.WrapAnywhere
                                                        textFormat: TextEdit.PlainText
                                                        selectByKeyboard: true
                                                        font.pixelSize: 12
                                                        color: control.SmoothUI.theme.res.textFillColorSecondary
                                                    }
                                                    Button {
                                                        objectName: "aiAssistantRetry_" + index
                                                        visible: entry && entry.retryable === true
                                                        text: entry && entry.turnError === "request canceled" ? qsTr("Retry stopped request") : qsTr("Retry")
                                                        enabled: !AIChatBridge.streaming && AIAccountBridge.signedIn
                                                        onClicked: AIChatBridge.retryTurn(entry.turnId)
                                                    }
                                                }
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                visible: isUser
                                                text: entry && entry.time ? entry.time : ""
                                                horizontalAlignment: isUser ? Text.AlignRight : Text.AlignLeft
                                                font.pixelSize: 10
                                                color: control.SmoothUI.theme.res.textFillColorTertiary
                                            }
                                        }
                                    }
                                }
                            } // Repeater
                        } // transcriptColumn
                    } // Flickable
                } // transcript region

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 26
                    Button {
                        objectName: "aiAssistantLatest"
                        anchors.centerIn: parent
                        height: 24
                        visible: !chatList.following && chatList.endY - chatList.contentY > 24
                        text: qsTr("↓ Back to latest")
                        onClicked: chatList.showLatest()
                    }
                }

                MenuSeparator { Layout.fillWidth: true; Layout.preferredHeight: 1 }

                Frame {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    Layout.topMargin: 10
                    Layout.bottomMargin: 14
                    padding: 1
                    SmoothUI.radius: 9

                    contentItem: ColumnLayout {
                        id: composer
                        spacing: 0

                        ScrollView {
                            id: inputScroll
                            objectName: "aiAssistantComposerViewport"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            contentWidth: availableWidth
                            clip: true
                            TextArea {
                                id: inputArea
                                objectName: "aiAssistantInput"
                                width: inputScroll.availableWidth
                                enabled: AIAccountBridge.signedIn
                                background: null
                                padding: 10
                                topPadding: 8
                                bottomPadding: 2
                                clip: true
                                wrapMode: TextArea.Wrap
                                placeholderText: AIAccountBridge.signedIn ? qsTr("Ask about models, slicing settings, or request an action…") : qsTr("Sign in to write a message")

                                Keys.onPressed: function(event) {
                                    if (inputMethodComposing || event.isAutoRepeat)
                                        return
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
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                            Layout.rightMargin: 7
                            Layout.topMargin: 4
                            Layout.bottomMargin: 7
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Enter to send · Shift+Enter for a new line")
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                color: control.SmoothUI.theme.res.textFillColorTertiary
                            }
                            Button {
                                objectName: "aiAssistantSend"
                                text: AIChatBridge.streaming ? qsTr("Stop") : qsTr("Send")
                                highlighted: !AIChatBridge.streaming && enabled
                                icon.name: AIChatBridge.streaming ? FluentIcons.graph_Stop : FluentIcons.graph_Up
                                icon.width: 13
                                icon.height: 13
                                font.pixelSize: 13
                                Layout.minimumWidth: 76
                                Layout.maximumWidth: 76
                                Layout.minimumHeight: 33
                                Layout.maximumHeight: 33
                                enabled: AIChatBridge.streaming || (AIAccountBridge.signedIn && inputArea.text.trim().length > 0 && !inputArea.inputMethodComposing)
                                onClicked: {
                                    if (AIChatBridge.streaming)
                                        AIChatBridge.cancelStreamingRequest("user_stop")
                                    else
                                        control.sendCurrentInput()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
