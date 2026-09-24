import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl

ScrollablePage {
    id: page
    title: qsTr("Dashboard")
    
    property int activePrints: 3
    property real successRate: 94.8
    property real materialUsage: 2.3
    
    // Header
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 60
        
        RowLayout {
            anchors.fill: parent
            
            ColumnLayout {
                spacing: 4
                
                Label {
                    text: qsTr("Active Prints")
                    font: Typography.subtitle
                }
                
                Label {
                    text: Qt.formatDate(new Date(), "dddd, MMMM dd, yyyy")
                    font: Typography.caption
                    color: Theme.res.textFillColorSecondary
                }
            }
            
            Item { Layout.fillWidth: true }
            
            FilledButton {
                text: qsTr("Start New Print")
                icon.name: FluentIcons.graph_Add
                onClicked: {
                    // Navigate to model library
                }
            }
        }
    }
    
    // Stats Cards
    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: 20
        rowSpacing: 20
        
        // Active Prints Card
        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                
                RowLayout {
                    Layout.fillWidth: true
                    
                    Icon {
                        source: FluentIcons.graph_Print
                        width: 20
                        height: 20
                        color: Theme.accentColor.defaultBrushFor()
                    }
                    
                    Label {
                        text: qsTr("Active Prints")
                        font: Typography.body
                        color: Theme.res.textFillColorSecondary
                    }
                }
                
                Label {
                    text: activePrints.toString()
                    font.pixelSize: 48
                    font.weight: Font.Bold
                    color: Theme.accentColor.defaultBrushFor()
                }
                
                Label {
                    text: qsTr("Jobs Running")
                    font: Typography.caption
                    color: Theme.res.textFillColorTertiary
                }
                
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 10
                    value: activePrints
                }
            }
        }
        
        // Success Rate Card
        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                
                RowLayout {
                    Layout.fillWidth: true
                    
                    Icon {
                        source: FluentIcons.graph_CheckMark
                        width: 20
                        height: 20
                        color: Colors.green.defaultBrushFor()
                    }
                    
                    Label {
                        text: qsTr("Success Rate")
                        font: Typography.body
                        color: Theme.res.textFillColorSecondary
                    }
                }
                
                Label {
                    text: successRate + "%"
                    font.pixelSize: 48
                    font.weight: Font.Bold
                    color: Colors.green.defaultBrushFor()
                }
                
                Label {
                    text: qsTr("Print Success")
                    font: Typography.caption
                    color: Theme.res.textFillColorTertiary
                }
                
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: successRate
                }
            }
        }
        
        // Material Usage Card
        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                
                RowLayout {
                    Layout.fillWidth: true
                    
                    Icon {
                        source: FluentIcons.graph_CubeShape
                        width: 20
                        height: 20
                        color: Colors.purple.defaultBrushFor()
                    }
                    
                    Label {
                        text: qsTr("Material Usage")
                        font: Typography.body
                        color: Theme.res.textFillColorSecondary
                    }
                }
                
                RowLayout {
                    spacing: 4
                    
                    Label {
                        text: materialUsage.toString()
                        font.pixelSize: 48
                        font.weight: Font.Bold
                        color: Colors.purple.defaultBrushFor()
                    }
                    
                    Label {
                        text: "kg"
                        font: Typography.subtitle
                        color: Theme.res.textFillColorSecondary
                        Layout.alignment: Qt.AlignBottom
                        Layout.bottomMargin: 8
                    }
                }
                
                Label {
                    text: qsTr("This Week")
                    font: Typography.caption
                    color: Theme.res.textFillColorTertiary
                }
            }
        }
    }
    
    // Recent Models Section
    Label {
        Layout.topMargin: 30
        text: qsTr("Recent Models")
        font: Typography.subtitle
    }
    
    Frame {
        Layout.fillWidth: true
        Layout.preferredHeight: 200
        
        ListView {
            anchors.fill: parent
            orientation: ListView.Horizontal
            spacing: 16
            clip: true
            
            model: ListModel {
                ListElement { name: "Mechanical Gear"; size: "2.3 MB"; time: "2h 15m"; color: "#6366F1" }
                ListElement { name: "Phone Stand Pro"; size: "1.8 MB"; time: "1h 45m"; color: "#EC4899" }
                ListElement { name: "Artistic Vase"; size: "4.1 MB"; time: "3h 20m"; color: "#10B981" }
                ListElement { name: "Tool Organizer"; size: "980 KB"; time: "1h 10m"; color: "#F59E0B" }
                ListElement { name: "Desk Lamp Base"; size: "3.2 MB"; time: "2h 30m"; color: "#8B5CF6" }
            }
            
            delegate: StandardButton {
                width: 180
                height: 160
                SmoothUI.radius: 8
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 80
                        color: model.color
                        radius: 4
                        
                        Icon {
                            anchors.centerIn: parent
                            source: FluentIcons.graph_CubeShape
                            width: 32
                            height: 32
                            color: "white"
                        }
                    }
                    
                    Label {
                        Layout.fillWidth: true
                        text: model.name
                        font: Typography.bodyStrong
                        elide: Text.ElideRight
                    }
                    
                    Label {
                        text: model.size + " • " + model.time
                        font: Typography.caption
                        color: Theme.res.textFillColorSecondary
                    }
                }
            }
        }
    }
    
    // Available Printers
    Label {
        Layout.topMargin: 30
        text: qsTr("Available Printers")
        font: Typography.subtitle
    }
    
    GridLayout {
        Layout.fillWidth: true
        columns: 4
        columnSpacing: 16
        rowSpacing: 16
        
        Repeater {
            model: ListModel {
                ListElement { 
                    name: "X1 Carbon"
                    status: "Ready"
                    statusColor: "#10B981"
                    temp: "25°C"
                    progress: 0
                }
                ListElement { 
                    name: "Prusa MK4"
                    status: "Printing"
                    statusColor: "#F59E0B"
                    temp: "210°C"
                    progress: 65
                }
                ListElement { 
                    name: "Ender 3 V3"
                    status: "Offline"
                    statusColor: "#6B7280"
                    temp: "20°C"
                    progress: 0
                }
                ListElement { 
                    name: "Bambu P1S"
                    status: "Maintenance"
                    statusColor: "#8B5CF6"
                    temp: "22°C"
                    progress: 0
                }
            }
            
            delegate: Frame {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    
                    RowLayout {
                        Layout.fillWidth: true
                        
                        Icon {
                            source: FluentIcons.graph_Print
                            width: 24
                            height: 24
                            color: Theme.res.textFillColorSecondary
                        }
                        
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            
                            Label {
                                text: model.name
                                font: Typography.bodyStrong
                            }
                            
                            RowLayout {
                                spacing: 4
                                
                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: model.statusColor
                                }
                                
                                Label {
                                    text: model.status
                                    font: Typography.caption
                                    color: Theme.res.textFillColorSecondary
                                }
                            }
                        }
                    }
                    
                    Label {
                        text: qsTr("Temp: ") + model.temp
                        font: Typography.caption
                        color: Theme.res.textFillColorTertiary
                    }
                    
                    ProgressBar {
                        Layout.fillWidth: true
                        visible: model.progress > 0
                        from: 0
                        to: 100
                        value: model.progress
                    }
                    
                    Label {
                        visible: model.progress > 0
                        text: qsTr("Progress: ") + model.progress + "%"
                        font: Typography.caption
                        color: Theme.res.textFillColorSecondary
                    }
                }
            }
        }
    }
    
    // Live Visualization
    Label {
        Layout.topMargin: 30
        text: qsTr("Live Print Visualization")
        font: Typography.subtitle
    }
    
    Frame {
        Layout.fillWidth: true
        Layout.preferredHeight: 300
        
        Rectangle {
            anchors.fill: parent
            color: Theme.dark ? "#1A1A1A" : "#F5F5F5"
            radius: 8
            
            Canvas {
                id: canvas
                anchors.fill: parent
                
                property real phase: 0
                
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    
                    // Draw wave
                    ctx.strokeStyle = Theme.accentColor.defaultBrushFor()
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    
                    for (var i = 0; i <= width; i++) {
                        var y = height/2 + Math.sin((i + phase) * 0.02) * 50
                        if (i === 0) {
                            ctx.moveTo(i, y)
                        } else {
                            ctx.lineTo(i, y)
                        }
                    }
                    ctx.stroke()
                }
                
                Timer {
                    interval: 50
                    running: true
                    repeat: true
                    onTriggered: {
                        canvas.phase += 5
                        canvas.requestPaint()
                    }
                }
            }
            
            Label {
                anchors.centerIn: parent
                text: qsTr("Real-time Layer Analysis")
                font: Typography.title
                color: Theme.accentColor.defaultBrushFor()
                opacity: 0.3
            }
        }
    }
}