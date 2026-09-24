import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SmoothUI.Controls
import "."

ContentDialog {
    id: dialog
    title: qsTr("Verification Failed - Step %1").arg(stepNumber)

    property int stepNumber: 0
    property string stepDescription: ""
    property real similarity: 0.0
    property real threshold: 0.995
    property string expectedImage: ""
    property string actualImage: ""
    property string diffImage: ""
    property string reportPath: ""

    width: 1200
    height: 800

    Column {
        width: parent.width - 40
        spacing: 15
        anchors.horizontalCenter: parent.horizontalCenter

        // 错误信息
        Label {
            text: qsTr("UI verification failed!")
            font.pixelSize: 18
            font.bold: true
            color: "#FF0000"
        }

        // 详细信息
        Label {
            text: qsTr("Step description: %1").arg(stepDescription)
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            width: parent.width
        }

        Label {
            text: qsTr("Similarity: %1% (required: ≥%2%)")
                .arg((similarity * 100).toFixed(2))
                .arg((threshold * 100).toFixed(1))
            font.pixelSize: 14
            color: "#FF0000"
        }

        // 图片对比区域 - 使用TabBar和StackLayout
        TabBar {
            id: tabBar
            width: parent.width

            TabButton { text: qsTr("Comparison") }
            TabButton { text: qsTr("Expected Image") }
            TabButton { text: qsTr("Actual Image") }
            TabButton { text: qsTr("Difference Image") }
        }

        // 内容区域
        StackLayout {
            width: parent.width
            height: 500
            currentIndex: tabBar.currentIndex

            // 对比视图页面
            Item {
                Row {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    // 期望图像查看器
                    ZoomableImageViewer {
                        width: (parent.width - parent.spacing * 2) / 3
                        height: parent.height
                        source: expectedImage ? "file:///" + expectedImage : ""
                        label: qsTr("Expected Result (recording)")
                    }

                    // 实际图像查看器
                    ZoomableImageViewer {
                        width: (parent.width - parent.spacing * 2) / 3
                        height: parent.height
                        source: actualImage ? "file:///" + actualImage : ""
                        label: qsTr("Actual Result (playback)")
                    }

                    // 差异图像查看器
                    ZoomableImageViewer {
                        width: (parent.width - parent.spacing * 2) / 3
                        height: parent.height
                        source: diffImage ? "file:///" + diffImage : ""
                        label: qsTr("Differences (marked in red)")
                    }
                }
            }

            // 期望图像页面
            Item {
                ZoomableImageViewer {
                    anchors.fill: parent
                    anchors.margins: 10
                    source: expectedImage ? "file:///" + expectedImage : ""
                    maxZoom: 20
                }
            }

            // 实际图像页面
            Item {
                ZoomableImageViewer {
                    anchors.fill: parent
                    anchors.margins: 10
                    source: actualImage ? "file:///" + actualImage : ""
                    maxZoom: 20
                }
            }

            // 差异图像页面
            Item {
                ZoomableImageViewer {
                    anchors.fill: parent
                    anchors.margins: 10
                    source: diffImage ? "file:///" + diffImage : ""
                    maxZoom: 20
                }
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("View Report")
            onClicked: {
                // 打开验证报告
                if (reportPath) {
                    var url = "file://" + reportPath
                    Qt.openUrlExternally(url)
                }
            }
        }
        Button {
            text: qsTr("OK")
            highlighted: true
            onClicked: {
                // 回放已经自动停止，关闭对话框即可
                dialog.close()
            }
        }
    }
}
