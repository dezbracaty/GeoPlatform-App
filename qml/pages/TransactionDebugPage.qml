import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Item {
    id: root

    property string searchText: ""
    property string typeFilter: "ALL"
    property string dbTypeFilter: "全部数据库"
    property string sourceFilter: "ALL"
    property string selectedTxId: ""
    property int selectedTab: 0

    readonly property var txSource: TransactionDebugBridge.transactions
    readonly property var changeSource: TransactionDebugBridge.changes
    readonly property var typeFilterOptions: [
        { text: qsTr("All Types"), value: "ALL" },
        { text: qsTr("Property Changed"), value: "PROPERTY_CHANGE" },
        { text: qsTr("Object Created"), value: "OBJECT_CREATED" },
        { text: qsTr("Object Deleted"), value: "OBJECT_DELETED" }
    ]
    readonly property var sourceFilterOptions: [
        { text: qsTr("All Sources"), value: "ALL" },
        { text: qsTr("Document Monitor"), value: "DocumentManager" },
        { text: qsTr("Transaction Monitor"), value: "TransactionManager" },
        { text: qsTr("System Event"), value: "System" }
    ]

    function valueText(value, emptyText) {
        if (value === undefined || value === null) {
            return emptyText !== undefined ? emptyText : ""
        }
        return String(value)
    }

    function displaySource(source) {
        if (source === "DocumentManager") {
            return qsTr("Document Monitor")
        }
        if (source === "TransactionManager") {
            return qsTr("Transaction Monitor")
        }
        if (source === "System") {
            return qsTr("system")
        }
        return valueText(source, "-")
    }

    function displayChangeType(typeValue) {
        if (typeValue === "PROPERTY_CHANGE") {
            return qsTr("Property Changed")
        }
        if (typeValue === "OBJECT_CREATED") {
            return qsTr("Object Created")
        }
        if (typeValue === "OBJECT_DELETED") {
            return qsTr("Object Deleted")
        }
        if (typeValue === "TRANSACTION_BEGUN") {
            return qsTr("Transaction Started")
        }
        if (typeValue === "TRANSACTION_COMMITTED") {
            return qsTr("Transaction Committed")
        }
        if (typeValue === "DEBUG_CAPTURE_READY") {
            return qsTr("Debugging Ready")
        }
        return valueText(typeValue, qsTr("Unknown Type"))
    }

    function transactionTitle(tx) {
        if (!tx) {
            return ""
        }
        var desc = valueText(tx.description, "")
        if (desc.length > 0) {
            return desc
        }
        return qsTr("Untitled Transaction")
    }

    function openTransactionDetails(txId) {
        selectedTxId = txId
        detailPopup.open()
    }

    function buildDbTypeOptions() {
        var unique = {}
        var result = [qsTr("All Databases")]

        for (var i = 0; i < txSource.length; ++i) {
            var tx = txSource[i]
            if (!tx || !tx.dbTypes) {
                continue
            }
            for (var j = 0; j < tx.dbTypes.length; ++j) {
                var txType = (tx.dbTypes[j] || "").toString()
                if (txType.length > 0 && txType !== "UNKNOWN" && txType !== "N/A" && !unique[txType]) {
                    unique[txType] = true
                    result.push(txType)
                }
            }
        }

        for (var k = 0; k < changeSource.length; ++k) {
            var ch = changeSource[k]
            if (!ch) {
                continue
            }
            var dbType = (ch.dbType || "").toString()
            if (dbType.length > 0 && dbType !== "-" && dbType !== "UNKNOWN" && dbType !== "N/A" && !unique[dbType]) {
                unique[dbType] = true
                result.push(dbType)
            }
        }
        return result
    }

    function transactionMatchesDbType(tx, filter) {
        if (filter === qsTr("All Databases")) {
            return true
        }
        if (!tx || !tx.dbTypes) {
            return false
        }
        return tx.dbTypes.indexOf(filter) !== -1
    }

    function buildFilteredTransactions() {
        var result = []
        var keyword = searchText.trim().toLowerCase()

        for (var i = txSource.length - 1; i >= 0; --i) {
            var tx = txSource[i]
            if (!tx) {
                continue
            }

            if (typeFilter !== "ALL" && tx.type !== typeFilter) {
                continue
            }
            if (!transactionMatchesDbType(tx, dbTypeFilter)) {
                continue
            }

            if (keyword.length > 0) {
                var desc = (tx.description || "").toLowerCase()
                var typeName = (tx.type || "").toLowerCase()
                var dbTypesText = tx.dbTypes ? tx.dbTypes.join(", ").toLowerCase() : ""
                if (desc.indexOf(keyword) === -1 && typeName.indexOf(keyword) === -1 && dbTypesText.indexOf(keyword) === -1) {
                    continue
                }
            }
            result.push(tx)
        }
        return result
    }

    function findSelectedTransaction() {
        if (!selectedTxId || selectedTxId === "") {
            return null
        }
        for (var i = 0; i < txSource.length; ++i) {
            if (txSource[i] && txSource[i].id === selectedTxId) {
                return txSource[i]
            }
        }
        return null
    }

    function buildRealtimeChanges() {
        var result = []
        var keyword = searchText.trim().toLowerCase()

        for (var i = changeSource.length - 1; i >= 0; --i) {
            var row = changeSource[i]
            if (!row) {
                continue
            }

            if (sourceFilter !== "ALL" && row.source !== sourceFilter) {
                continue
            }
            if (dbTypeFilter !== qsTr("All Databases") && row.dbType !== dbTypeFilter) {
                continue
            }

            if (keyword.length > 0) {
                var text = ((row.changeType || "") + " " + (row.dbType || "") + " " + (row.property || "")).toLowerCase()
                if (text.indexOf(keyword) === -1) {
                    continue
                }
            }
            result.push(row)
        }
        return result
    }

    readonly property var dbTypeOptions: buildDbTypeOptions()
    readonly property var filteredTransactions: buildFilteredTransactions()
    readonly property var selectedTransaction: findSelectedTransaction()
    readonly property var selectedChanges: selectedTransaction && selectedTransaction.changes ? selectedTransaction.changes : []
    readonly property var realtimeChanges: buildRealtimeChanges()

    onDbTypeOptionsChanged: {
        if (dbTypeOptions.indexOf(dbTypeFilter) === -1) {
            dbTypeFilter = qsTr("All Databases")
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.res.solidBackgroundFillColorBase
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: 58

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Label {
                    text: qsTr("Transaction Debug")
                    font: Typography.subtitle
                }

                Label {
                    text: qsTr("Undo stack: %1  Redo stack: %2")
                        .arg(TransactionDebugBridge.undoStackSize)
                        .arg(TransactionDebugBridge.redoStackSize)
                    color: Theme.res.textFillColorSecondary
                    font: Typography.caption
                }

                Label {
                    text: TransactionDebugBridge.inTransaction ? qsTr("In Transaction") : qsTr("Idle")
                    color: TransactionDebugBridge.inTransaction
                        ? Colors.green.defaultBrushFor()
                        : Theme.res.textFillColorSecondary
                    font: Typography.caption
                }

                Item { Layout.fillWidth: true }

                Switch {
                    text: qsTr("Capture")
                    checked: TransactionDebugBridge.capturing
                    enabled: TransactionDebugBridge.runtimeEnabled
                    onToggled: TransactionDebugBridge.capturing = checked
                }

                FilledButton {
                    text: qsTr("Clear All")
                    enabled: TransactionDebugBridge.runtimeEnabled
                    onClicked: {
                        TransactionDebugBridge.clearAll()
                        root.selectedTxId = ""
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            visible: !TransactionDebugBridge.runtimeEnabled

            Label {
                anchors.fill: parent
                anchors.margins: 10
                text: qsTr("Transaction debug capture is disabled. Restart with --debug-transaction.")
                color: Theme.res.textFillColorSecondary
                wrapMode: Text.WrapAnywhere
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: 52

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                TextField {
                    Layout.preferredWidth: 260
                    placeholderText: qsTr("Search properties, types, or databases...")
                    text: root.searchText
                    onTextChanged: root.searchText = text
                }

                ComboBox {
                    Layout.preferredWidth: 160
                    model: root.typeFilterOptions
                    textRole: "text"
                    onActivated: function(index) {
                        root.typeFilter = model[index].value
                    }
                    Component.onCompleted: currentIndex = 0
                }

                ComboBox {
                    Layout.preferredWidth: 190
                    model: root.dbTypeOptions
                    onActivated: function(index) {
                        root.dbTypeFilter = model[index]
                    }
                    onModelChanged: {
                        var idx = model.indexOf(root.dbTypeFilter)
                        currentIndex = idx >= 0 ? idx : 0
                    }
                    Component.onCompleted: currentIndex = 0
                }

                ComboBox {
                    Layout.preferredWidth: 160
                    model: root.sourceFilterOptions
                    textRole: "text"
                    onActivated: function(index) {
                        root.sourceFilter = model[index].value
                    }
                    Component.onCompleted: currentIndex = 0
                }
            }
        }

        TabBar {
            Layout.fillWidth: true
            currentIndex: root.selectedTab
            onCurrentIndexChanged: root.selectedTab = currentIndex
            TabButton { text: qsTr("Live Changes") }
            TabButton { text: qsTr("Transaction List") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.selectedTab

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Live Change Stream"); font: Typography.bodyStrong }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: qsTr("%1 records").arg(root.realtimeChanges.length)
                            color: Theme.res.textFillColorSecondary
                            font: Typography.caption
                        }
                    }

                    Label {
                        visible: root.realtimeChanges.length === 0
                        text: qsTr("No matching data. Try changing model properties or filters.")
                        color: Theme.res.textFillColorSecondary
                        font: Typography.caption
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: root.realtimeChanges

                        delegate: Frame {
                            required property var modelData
                            width: ListView.view.width
                            height: 82

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4

                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: root.valueText(modelData.timestamp, "-")
                                        color: Theme.res.textFillColorSecondary
                                        font: Typography.caption
                                    }
                                    Label {
                                        text: root.displaySource(modelData.source)
                                        color: Theme.accentColor.defaultBrushFor()
                                        font: Typography.caption
                                    }
                                    Label {
                                        text: root.displayChangeType(modelData.changeType)
                                        color: Theme.res.textFillColorSecondary
                                        font: Typography.caption
                                    }
                                    Label {
                                        text: root.valueText(modelData.dbType, "-")
                                            + ":" + root.valueText(modelData.dbId, "-")
                                        color: Theme.res.textFillColorSecondary
                                        font: Typography.caption
                                    }
                                    Item { Layout.fillWidth: true }
                                    Label {
                                        text: modelData.transactionActive ? qsTr("In Transaction") : ""
                                        color: Colors.green.defaultBrushFor()
                                        font: Typography.caption
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Property: %1    New value: %2")
                                        .arg(root.valueText(modelData.property, "-"))
                                        .arg(root.valueText(modelData.newValue, ""))
                                    color: Theme.res.textFillColorPrimary
                                    font: Typography.body
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Old value: %1").arg(root.valueText(modelData.oldValue, ""))
                                    color: Theme.res.textFillColorSecondary
                                    font: Typography.caption
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                spacing: 8

                Frame {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 430

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Transaction List"); font: Typography.bodyStrong }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: qsTr("%1 transactions").arg(root.filteredTransactions.length)
                                color: Theme.res.textFillColorSecondary
                                font: Typography.caption
                            }
                        }

                        Label {
                            visible: root.filteredTransactions.length === 0
                            text: qsTr("No transactions yet. Perform an edit that creates a transaction first.")
                            color: Theme.res.textFillColorSecondary
                            font: Typography.caption
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            model: root.filteredTransactions

                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width
                                height: 96
                                radius: 6
                                color: Theme.res.layerFillColorAlt
                                border.width: 1
                                border.color: root.selectedTxId === modelData.id
                                    ? Theme.accentColor.defaultBrushFor()
                                    : Theme.res.cardStrokeColorDefault

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 4

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            text: root.transactionTitle(modelData)
                                            color: Theme.res.textFillColorPrimary
                                            font: Typography.bodyStrong
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            text: root.displayChangeType(modelData.type)
                                            color: Theme.res.textFillColorSecondary
                                            font: Typography.caption
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("Primary type: %1").arg(root.valueText(modelData.primaryDbType, "N/A"))
                                            color: Theme.res.textFillColorSecondary
                                            font: Typography.caption
                                        }
                                        Label {
                                            text: qsTr("Changes: %1").arg(root.valueText(modelData.changeCount, "0"))
                                            color: Theme.res.textFillColorSecondary
                                            font: Typography.caption
                                        }
                                        Item { Layout.fillWidth: true }
                                        Label {
                                            text: root.valueText(modelData.timestamp, "")
                                            color: Theme.res.textFillColorSecondary
                                            font: Typography.caption
                                        }
                                    }

                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.openTransactionDetails(modelData.id)
                                    acceptedButtons: Qt.LeftButton
                                }
                            }
                        }
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Label { text: qsTr("Selected Transaction Details"); font: Typography.bodyStrong }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 50
                            radius: 6
                            color: Theme.res.layerFillColorAlt
                            border.width: 1
                            border.color: Theme.res.cardStrokeColorDefault

                            Label {
                                anchors.fill: parent
                                anchors.margins: 8
                                text: root.selectedTransaction
                                      ? (root.transactionTitle(root.selectedTransaction) + " | "
                                      + root.displayChangeType(root.selectedTransaction.type))
                                      : qsTr("Click a transaction on the left to show its changes here.")
                                color: Theme.res.textFillColorSecondary
                                font: Typography.caption
                                wrapMode: Text.WrapAnywhere
                            }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            model: root.selectedChanges

                            delegate: Frame {
                                required property var modelData
                                width: ListView.view.width
                                height: 92

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 4

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: "#" + root.valueText(modelData.index, "0")
                                                + " " + root.displayChangeType(modelData.kind)
                                            color: Theme.accentColor.defaultBrushFor()
                                            font: Typography.caption
                                        }
                                        Item { Layout.fillWidth: true }
                                        Label {
                                            text: root.valueText(modelData.dbType, "-")
                                                + " / " + root.valueText(modelData.dbId, "-")
                                            color: Theme.res.textFillColorSecondary
                                            font: Typography.caption
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: root.valueText(modelData.summary, "")
                                        color: Theme.res.textFillColorPrimary
                                        font: Typography.body
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Old value: %1    New value: %2")
                                            .arg(root.valueText(modelData.oldValue, ""))
                                            .arg(root.valueText(modelData.newValue, ""))
                                        color: Theme.res.textFillColorSecondary
                                        font: Typography.caption
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    AppPopup {
        id: detailPopup
        preferredWidth: 920
        preferredHeight: 620
        title: qsTr("Transaction Changes")

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: PrintWorkspaceStyle.space16
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: root.selectedTransaction
                    ? (root.transactionTitle(root.selectedTransaction) + " | "
                    + root.displayChangeType(root.selectedTransaction.type))
                    : qsTr("No Transaction Selected")
                color: Theme.res.textFillColorSecondary
                font: Typography.caption
                wrapMode: Text.WrapAnywhere
            }

            Label {
                visible: !root.selectedTransaction
                text: qsTr("Select a transaction from the list first.")
                color: Theme.res.textFillColorSecondary
                font: Typography.caption
            }

            Label {
                visible: root.selectedTransaction && root.selectedChanges.length === 0
                text: qsTr("This transaction has no displayable changes.")
                color: Theme.res.textFillColorSecondary
                font: Typography.caption
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: root.selectedChanges

                delegate: Frame {
                    required property var modelData
                    width: ListView.view.width
                    height: 96

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: "#" + root.valueText(modelData.index, "0")
                                    + " " + root.displayChangeType(modelData.kind)
                                color: Theme.accentColor.defaultBrushFor()
                                font: Typography.caption
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: root.valueText(modelData.dbType, "-")
                                    + " / " + root.valueText(modelData.dbId, "-")
                                color: Theme.res.textFillColorSecondary
                                font: Typography.caption
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.valueText(modelData.summary, "")
                            color: Theme.res.textFillColorPrimary
                            font: Typography.body
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Old value: %1    New value: %2")
                                .arg(root.valueText(modelData.oldValue, ""))
                                .arg(root.valueText(modelData.newValue, ""))
                            color: Theme.res.textFillColorSecondary
                            font: Typography.caption
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
