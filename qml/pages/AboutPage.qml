import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

ScrollablePage {
    title: qsTr("About")
    
    ColumnLayout {
        spacing: 20
        
        Label {
            text: "GPlatform"
            font: Typography.title
        }
        
        Label {
            text: "Version " + AppInfo.version
            font: Typography.subtitle
        }
        
        Label {
            text: "A versatile platform currently focused on 3D printing management"
            font: Typography.body
        }
    }
}
