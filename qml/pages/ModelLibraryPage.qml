import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Page {
    id: control
    objectName: "modelLibraryPage"

    signal requestLocalModels()

    property string searchText: ""
    property string selectedSection: "featured"
    property int readyPreviewCount: 0
    property bool suggestionsRequested: false
    readonly property bool screenshotReady: !ModelService.loading
                                               && displayedModels.length > 0
                                               && readyPreviewCount >= Math.min(
                                                   3, displayedModels.length)
                                               && suggestionTags.length > 0
                                               && hotSearches.length > 0
                                               && hotPrints.length > 0
    property color accentColor: Theme.accentColor.defaultBrushFor()
    property color pageColor: control.SmoothUI.dark ? "#17191D" : "#F4F6F8"
    property color surfaceColor: control.SmoothUI.dark ? "#22252B" : "#FFFFFF"
    property color surfaceMutedColor: control.SmoothUI.dark ? "#292D34" : "#F6F7F9"
    property color primaryTextColor: control.SmoothUI.dark ? "#F3F5F7" : "#202328"
    property color secondaryTextColor: control.SmoothUI.dark ? "#AEB4BE" : "#69717D"
    property color borderColor: control.SmoothUI.dark ? "#383D46" : "#E5E8EC"

    property var sectionTabs: [
        { key: "featured", title: qsTr("Recommended") },
        { key: "popular", title: qsTr("Trending") },
        { key: "accessories", title: qsTr("Useful Accessories") },
        { key: "art", title: qsTr("Decorative Art") },
        { key: "education", title: qsTr("Education") }
    ]

    property var suggestionTags: ModelService.searchSuggestions || []
    property var hotSearches: ModelService.hotSearches || []
    property var hotPrints: ModelService.hotPrints || []
    property var displayedModels: ModelService.models || []

    function normalized(value) {
        return value === undefined || value === null
                ? "" : String(value).toLowerCase()
    }

    function refreshModels() {
        readyPreviewCount = 0
        ModelService.fetchModels(selectedSection, searchText.trim())
    }

    function formatNumber(value) {
        var number = Number(value || 0)
        if (number >= 1000000) return (number / 1000000).toFixed(1) + "M"
        if (number >= 1000) return (number / 1000).toFixed(1) + "K"
        return String(number)
    }

    function openModel(model) {
        modelDetailPopup.modelData = model
        modelDetailPopup.open()
    }

    function downloadModel(model) {
        if (!model.downloadUrl) {
            if (model.url) {
                Qt.openUrlExternally(model.url)
            }
            return
        }
        DownloadManager.downloadModel(model)
        NotificationManager.showInfo(
                    qsTr("Download started"),
                    qsTr("Downloading: %1").arg(model.name || qsTr("Untitled Model")))
    }

    function positionSearchSuggestions() {
        if (!searchSuggestions.parent) {
            return
        }
        var position = searchShell.mapToItem(
                    searchSuggestions.parent, 0, searchShell.height + 8)
        searchSuggestions.x = position.x
        searchSuggestions.y = position.y
    }

    function showSearchSuggestions() {
        control.suggestionsRequested = true
        searchField.forceActiveFocus()
        control.positionSearchSuggestions()
        Qt.callLater(control.positionSearchSuggestions)
    }

    onWidthChanged: {
        if (searchSuggestions.visible) {
            control.positionSearchSuggestions()
        }
    }

    onSearchTextChanged: searchDebounce.restart()
    onSelectedSectionChanged: {
        searchDebounce.stop()
        refreshModels()
    }

    Component.onCompleted: {
        ModelService.fetchDiscovery()
        if ((!ModelService.models || ModelService.models.length === 0)
                && !ModelService.loading) {
            refreshModels()
        }
    }

    Timer {
        id: searchDebounce
        interval: 350
        repeat: false
        onTriggered: control.refreshModels()
    }

    background: Rectangle { color: control.pageColor }

    component HeaderActionButton: Rectangle {
        id: actionButton
        property string text: ""
        property var iconSource: ""
        property bool primary: false
        signal clicked()

        implicitWidth: buttonRow.implicitWidth + 28
        implicitHeight: 38
        radius: 8
        color: primary
               ? control.accentColor
               : (buttonMouse.containsMouse ? control.surfaceMutedColor : control.surfaceColor)
        border.width: primary ? 0 : 1
        border.color: control.borderColor

        Row {
            id: buttonRow
            anchors.centerIn: parent
            spacing: 7

            Icon {
                width: 15
                height: 15
                anchors.verticalCenter: parent.verticalCenter
                source: actionButton.iconSource
                color: actionButton.primary ? "white" : control.primaryTextColor
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: actionButton.text
                color: actionButton.primary ? "white" : control.primaryTextColor
                font.pixelSize: 13
                font.weight: Font.Medium
            }
        }

        MouseArea {
            id: buttonMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: actionButton.clicked()
        }
    }

    component ModelCard: Item {
        id: modelCard
        required property var entry
        required property int cardIndex

        height: 318
        scale: cardMouse.containsMouse ? 1.012 : 1.0

        Behavior on scale {
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }

        Rectangle {
            anchors.fill: cardSurface
            anchors.topMargin: 5
            radius: cardSurface.radius
            color: control.SmoothUI.dark ? "#26000000" : "#120D1B2A"
        }

        Rectangle {
            id: cardSurface
            anchors.fill: parent
            anchors.bottomMargin: 5
            radius: 14
            color: control.surfaceColor
            border.width: 1
            border.color: cardMouse.containsMouse ? control.accentColor : control.borderColor
            clip: true

            Rectangle {
                id: previewArea
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 206
                color: modelCard.entry.previewColor || control.surfaceMutedColor

                Image {
                    id: previewImage
                    property bool readinessCounted: false
                    anchors.fill: parent
                    source: modelCard.entry.thumbnail || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    visible: status !== Image.Error && source !== ""

                    onStatusChanged: {
                        if (status === Image.Ready
                                && modelCard.cardIndex < 3
                                && !readinessCounted) {
                            readinessCounted = true
                            control.readyPreviewCount += 1
                        }
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        width: 30
                        height: 30
                        running: previewImage.status === Image.Loading
                        visible: running
                    }
                }

                Icon {
                    anchors.centerIn: parent
                    width: 42
                    height: 42
                    source: FluentIcons.graph_CubeShape
                    color: "white"
                    opacity: 0.72
                    visible: previewImage.source === "" || previewImage.status === Image.Error
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 10
                    width: formatLabel.implicitWidth + 14
                    height: 24
                    radius: 7
                    color: "#B321252B"

                    Text {
                        id: formatLabel
                        anchors.centerIn: parent
                        text: modelCard.entry.format || qsTr("3D")
                        color: "white"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    color: cardMouse.containsMouse ? "#16000000" : "transparent"

                    Behavior on color { ColorAnimation { duration: 120 } }
                }
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: previewArea.bottom
                anchors.margins: 13
                spacing: 9

                Text {
                    width: parent.width
                    text: modelCard.entry.name || qsTr("Untitled Model")
                    color: control.primaryTextColor
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Rectangle {
                        width: 22
                        height: 22
                        radius: 11
                        color: control.surfaceMutedColor
                        border.width: 1
                        border.color: control.borderColor

                        Image {
                            id: authorAvatar
                            anchors.fill: parent
                            anchors.margins: 1
                            source: modelCard.entry.authorAvatar || ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            visible: source !== "" && status !== Image.Error
                        }

                        Text {
                            anchors.centerIn: parent
                            text: normalized(modelCard.entry.author).length > 0
                                  ? String(modelCard.entry.author).charAt(0).toUpperCase() : "G"
                            color: control.secondaryTextColor
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            visible: !authorAvatar.visible
                        }
                    }

                    Text {
                        width: Math.max(70, parent.width - statsRow.width - 38)
                        anchors.verticalCenter: parent.children[0].verticalCenter
                        text: modelCard.entry.author || qsTr("GPlatform Community")
                        color: control.secondaryTextColor
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    Row {
                        id: statsRow
                        anchors.verticalCenter: parent.children[0].verticalCenter
                        spacing: 10

                        Row {
                            spacing: 4
                            Icon {
                                width: 12; height: 12
                                source: FluentIcons.graph_Like
                                color: control.secondaryTextColor
                            }
                            Text {
                                text: control.formatNumber(modelCard.entry.likes)
                                color: control.secondaryTextColor
                                font.pixelSize: 11
                            }
                        }

                        Row {
                            spacing: 4
                            Icon {
                                width: 12; height: 12
                                source: FluentIcons.graph_Download
                                color: control.secondaryTextColor
                            }
                            Text {
                                text: control.formatNumber(modelCard.entry.downloads)
                                color: control.secondaryTextColor
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            MouseArea {
                id: cardMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: control.openModel(modelCard.entry)
            }

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 10
                width: 34
                height: 34
                radius: 9
                color: downloadMouse.containsMouse ? control.accentColor : "#D9FFFFFF"
                visible: cardMouse.containsMouse || downloadMouse.containsMouse
                z: 3

                Icon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: modelCard.entry.downloadUrl
                            ? FluentIcons.graph_Download
                            : FluentIcons.graph_OpenInNewWindow
                    color: downloadMouse.containsMouse ? "white" : "#30343A"
                }

                MouseArea {
                    id: downloadMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: control.downloadModel(modelCard.entry)
                }
            }
        }
    }

    Flickable {
        id: pageFlickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.height + 56
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        onContentYChanged: {
            if (searchSuggestions.visible) {
                control.positionSearchSuggestions()
            }
        }

        Column {
            id: contentColumn
            width: Math.min(pageFlickable.width - 64, 1540)
            anchors.horizontalCenter: parent.horizontalCenter
            topPadding: 28
            spacing: 26

            Rectangle {
                id: hero
                width: parent.width
                height: 218
                radius: 18
                clip: true

                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop {
                        position: 0
                        color: control.SmoothUI.dark ? "#243449" : "#E9F4FF"
                    }
                    GradientStop {
                        position: 1
                        color: control.SmoothUI.dark ? "#352B49" : "#F3EEFF"
                    }
                }

                Rectangle {
                    width: 240
                    height: 240
                    radius: 120
                    anchors.right: parent.right
                    anchors.rightMargin: 70
                    anchors.verticalCenter: parent.verticalCenter
                    color: control.SmoothUI.dark ? "#223E8CF0" : "#553E8CF0"
                }

                Rectangle {
                    width: 130
                    height: 130
                    radius: 34
                    rotation: 18
                    anchors.right: parent.right
                    anchors.rightMargin: 122
                    anchors.verticalCenter: parent.verticalCenter
                    color: control.accentColor
                    opacity: 0.92

                    Icon {
                        anchors.centerIn: parent
                        width: 68
                        height: 68
                        source: FluentIcons.graph_CubeShape
                        color: "white"
                    }
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 30
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(parent.width - 360, 840)
                    height: 142
                    spacing: 10

                    Text {
                        text: qsTr("Discover models worth printing")
                        color: control.primaryTextColor
                        font.pixelSize: 25
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: qsTr("Find inspiration in curated community models and bring downloads directly into your printing workflow.")
                        color: control.secondaryTextColor
                        font.pixelSize: 13
                    }

                    Row {
                        width: parent.width
                        spacing: 10
                        topPadding: 8

                        Rectangle {
                            id: searchShell
                            width: Math.min(parent.width - quickActions.width - 12, 620)
                            height: 42
                            radius: 9
                            color: control.surfaceColor
                            border.width: searchField.activeFocus ? 2 : 1
                            border.color: searchField.activeFocus
                                          ? control.accentColor : control.borderColor

                            Icon {
                                id: searchIcon
                                anchors.left: parent.left
                                anchors.leftMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                width: 17
                                height: 17
                                source: FluentIcons.graph_Search
                                color: control.secondaryTextColor
                            }

                            TextField {
                                id: searchField
                                objectName: "modelLibrarySearchField"
                                anchors.left: searchIcon.right
                                anchors.right: clearSearch.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 9
                                anchors.rightMargin: 6
                                text: control.searchText
                                placeholderText: qsTr("Search models, creators, or categories")
                                color: control.primaryTextColor
                                placeholderTextColor: control.secondaryTextColor
                                font.pixelSize: 13
                                selectByMouse: true
                                background: null
                                onTextEdited: control.searchText = text
                                onAccepted: focus = false
                                onActiveFocusChanged: {
                                    if (!activeFocus) {
                                        control.suggestionsRequested = false
                                    }
                                }

                                TapHandler {
                                    onTapped: control.showSearchSuggestions()
                                }
                            }

                            IconButton {
                                id: clearSearch
                                anchors.right: parent.right
                                anchors.rightMargin: 5
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30
                                height: 30
                                visible: searchField.text.length > 0
                                icon.name: FluentIcons.graph_ChromeClose
                                icon.width: 11
                                icon.height: 11
                                onClicked: {
                                    searchField.clear()
                                    control.searchText = ""
                                }
                            }
                        }

                        Row {
                            id: quickActions
                            spacing: 8

                            HeaderActionButton {
                                text: qsTr("Download History")
                                iconSource: FluentIcons.graph_History
                                onClicked: control.requestLocalModels()
                            }

                            HeaderActionButton {
                                text: qsTr("Import Local File")
                                iconSource: FluentIcons.graph_OpenLocal
                                primary: true
                                onClicked: Global.starter.chooseModel({})
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 14

                Row {
                    width: parent.width
                    spacing: 8

                    Rectangle {
                        width: 27
                        height: 27
                        radius: 8
                        color: control.accentColor

                        Icon {
                            anchors.centerIn: parent
                            width: 15
                            height: 15
                            source: FluentIcons.graph_Package
                            color: "white"
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.children[0].verticalCenter
                        text: qsTr("Recommended Models")
                        color: control.primaryTextColor
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }

                    Item { width: 8; height: 1 }

                    Text {
                        anchors.verticalCenter: parent.children[0].verticalCenter
                        text: qsTr("Pick something for your next print")
                        color: control.secondaryTextColor
                        font.pixelSize: 12
                    }
                }

                Row {
                    id: sectionTabRow
                    spacing: 4

                    Repeater {
                        model: control.sectionTabs

                        Rectangle {
                            id: sectionTab
                            required property var modelData
                            width: tabText.implicitWidth + 24
                            height: 34
                            radius: 8
                            color: control.selectedSection === modelData.key
                                   ? control.surfaceColor
                                   : (tabMouse.containsMouse ? control.surfaceMutedColor : "transparent")
                            border.width: control.selectedSection === modelData.key ? 1 : 0
                            border.color: control.borderColor

                            Text {
                                id: tabText
                                anchors.centerIn: parent
                                text: sectionTab.modelData.title
                                color: control.selectedSection === sectionTab.modelData.key
                                       ? control.primaryTextColor : control.secondaryTextColor
                                font.pixelSize: 13
                                font.weight: control.selectedSection === sectionTab.modelData.key
                                             ? Font.DemiBold : Font.Normal
                            }

                            MouseArea {
                                id: tabMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: control.selectedSection = sectionTab.modelData.key
                            }
                        }
                    }
                }

                Row {
                    id: searchResultRow
                    width: parent.width
                    visible: control.searchText.length > 0

                    Text {
                        id: searchResultLabel
                        text: qsTr("Found %1 related models").arg(ModelService.totalCount)
                        color: control.secondaryTextColor
                        font.pixelSize: 12
                    }

                    Item {
                        width: Math.max(0, searchResultRow.width
                                        - searchResultLabel.width - clearResult.width)
                        height: 1
                    }

                    Text {
                        id: clearResult
                        text: qsTr("Clear Search")
                        color: control.accentColor
                        font.pixelSize: 12

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                searchField.clear()
                                control.searchText = ""
                            }
                        }
                    }
                }

                Grid {
                    id: modelGrid
                    width: parent.width
                    columns: width >= 1280 ? 4 : (width >= 900 ? 3 : 2)
                    columnSpacing: 18
                    rowSpacing: 18

                    property real cardWidth: (width - (columns - 1) * columnSpacing) / columns

                    Repeater {
                        model: control.displayedModels

                        ModelCard {
                            required property var modelData
                            required property int index
                            width: modelGrid.cardWidth
                            entry: modelData
                            cardIndex: index
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 148
                    radius: 14
                    color: control.surfaceColor
                    border.width: 1
                    border.color: control.borderColor
                    visible: !ModelService.loading && control.displayedModels.length === 0

                    Column {
                        anchors.centerIn: parent
                        spacing: 9

                        Icon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 34
                            height: 34
                            source: FluentIcons.graph_Search
                            color: control.secondaryTextColor
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("No matching models found")
                            color: control.primaryTextColor
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("Try other keywords or recommended categories")
                            color: control.secondaryTextColor
                            font.pixelSize: 12
                        }
                    }
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 10
                    visible: !ModelService.loading && ModelService.hasMore

                    HeaderActionButton {
                        text: qsTr("View More Models")
                        iconSource: FluentIcons.graph_More
                        onClicked: ModelService.loadMoreModels()
                    }
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 10
                    visible: ModelService.loading

                    BusyIndicator { width: 24; height: 24; running: visible }
                    Text {
                        anchors.verticalCenter: parent.children[0].verticalCenter
                        text: qsTr("Loading online models…")
                        color: control.secondaryTextColor
                        font.pixelSize: 12
                    }
                }

                Item { width: 1; height: 16 }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        visible: control.suggestionsRequested && searchField.activeFocus
        z: 50
        onClicked: searchField.focus = false
    }

    Rectangle {
        id: searchSuggestions
        x: 0
        y: 0
        width: searchShell.width
        height: 252
        radius: 12
        color: control.surfaceColor
        border.width: 1
        border.color: control.borderColor
        visible: control.suggestionsRequested && searchField.activeFocus
        z: 60

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 5
            radius: parent.radius
            color: control.SmoothUI.dark ? "#24000000" : "#160D1B2A"
            z: -1
        }

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 13

            Text {
                text: qsTr("Suggested Searches")
                color: control.primaryTextColor
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            Flow {
                width: parent.width
                spacing: 7

                Repeater {
                    model: control.suggestionTags

                    Rectangle {
                        id: suggestionChip
                        required property string modelData
                        width: suggestionText.implicitWidth + 20
                        height: 28
                        radius: 14
                        color: suggestionMouse.containsMouse
                               ? control.surfaceMutedColor : "transparent"
                        border.width: 1
                        border.color: control.borderColor

                        Text {
                            id: suggestionText
                            anchors.centerIn: parent
                            text: suggestionChip.modelData
                            color: control.secondaryTextColor
                            font.pixelSize: 12
                        }

                        MouseArea {
                            id: suggestionMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                searchField.text = suggestionChip.modelData
                                control.searchText = suggestionChip.modelData
                                searchField.focus = false
                            }
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: control.borderColor }

            Row {
                width: parent.width
                spacing: 24

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 7

                    Text {
                        text: qsTr("Popular Searches")
                        color: control.primaryTextColor
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: control.hotSearches.slice(0, 5)

                        Item {
                            id: hotSearchItem
                            required property string modelData
                            required property int index
                            width: parent.width
                            height: 18

                            Row {
                                anchors.fill: parent
                                spacing: 7

                                Text {
                                    text: hotSearchItem.index + 1
                                    color: hotSearchItem.index < 3
                                           ? "#E04B3F" : control.secondaryTextColor
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    width: hotSearchItem.width - 24
                                    text: hotSearchItem.modelData
                                    color: control.secondaryTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    searchField.text = hotSearchItem.modelData
                                    control.searchText = hotSearchItem.modelData
                                    searchField.focus = false
                                }
                            }
                        }
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 7

                    Text {
                        text: qsTr("Popular Prints")
                        color: control.primaryTextColor
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: control.hotPrints.slice(0, 5)

                        Item {
                            id: hotPrintItem
                            required property string modelData
                            required property int index
                            width: parent.width
                            height: 18

                            Row {
                                anchors.fill: parent
                                spacing: 7

                                Text {
                                    text: hotPrintItem.index + 1
                                    color: hotPrintItem.index < 3
                                           ? "#E04B3F" : control.secondaryTextColor
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    width: hotPrintItem.width - 24
                                    text: hotPrintItem.modelData
                                    color: control.secondaryTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    searchField.text = hotPrintItem.modelData
                                    control.searchText = hotPrintItem.modelData
                                    searchField.focus = false
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ModelDetailPopup { id: modelDetailPopup }

    Connections {
        target: DownloadManager

        function onDownloadCompleted(modelId, filePath) {
            var fileName = filePath.split('/').pop()
            NotificationManager.showSuccess(
                        qsTr("Download completed"),
                        qsTr("%1 has been saved to the model downloads folder").arg(fileName),
                        6000)
        }

        function onDownloadFailed(modelId, error) {
            NotificationManager.showError(
                        qsTr("Download failed"),
                        qsTr("Model download failed: %1").arg(error),
                        5000)
        }
    }

    InfoBar {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        visible: ModelService.errorMessage !== ""
        severity: 3
        title: qsTr("Failed to load model")
        message: ModelService.errorMessage
        z: 80
    }
}
