import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl

Item {
    id: topBanner
    SmoothUI.theme: Theme.of(topBanner)

    // 固定高度
    height: 120

    // 背景层 - 与主页面背景融合
    Rectangle {
        anchors.fill: parent
        color: "transparent"
    }

    // 主内容列 - 垂直居中布局（三行：AI标签、主标题、副标题）
    Column {
        id: titleColumn
        anchors.centerIn: parent
        spacing: 12

        // AI驱动标签
        Rectangle {
            id: aiTag
            color: topBanner.SmoothUI.theme.accentColor.defaultBrushFor(topBanner.SmoothUI.dark)
            radius: 14
            height: 28
            width: aiTagText.implicitWidth + 24
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                id: aiTagText
                text: qsTr("✨ AI-powered 3D Generation")
                anchors.centerIn: parent
                color: topBanner.SmoothUI.theme.res.textOnAccentFillColorPrimary
                font.pixelSize: Theme.captionFontSize
                font.bold: false
            }
        }

        // 主标题
        Text {
            id: mainTitle
            text: qsTr("3D Model Workshop")
            font.pixelSize: Theme.pageTitleFontSize
            font.bold: true
            color: topBanner.SmoothUI.theme.res.textFillColorPrimary
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
        }

        // 副标题
        Text {
            id: subTitle
            text: qsTr("Enter a description or upload reference images, and AI will generate multiple 3D model options")
            font.pixelSize: Theme.bodyFontSize
            color: topBanner.SmoothUI.theme.res.textFillColorSecondary
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
