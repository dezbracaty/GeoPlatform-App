import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl

ScrollablePage {
    title: qsTr("About")
    
    ColumnLayout {
        spacing: 20
        
        Label {
            text: "GPlatform"
            font: Typography.title
        }
        
        Label {
            text: "Version 1.0.0"
            font: Typography.subtitle
        }
        
        Label {
            text: "A versatile platform currently focused on 3D printing management"
            font: Typography.body
        }
    }
}