import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform
import "../components"

Page {
    id: control
    property bool previewActive: false

    // 直接使用 DownloadedModels 组件，它内部已经有完整的布局
    DownloadedModels {
        anchors.fill: parent
        active: control.previewActive

        onModelImported: function(libraryItemId) {
            ActionManager.triggerAction("model.library.import", {
                libraryItemId: libraryItemId,
                scale: 1.0,
                color: "#4080C0"
            })

            // 导航到 Dock System 页面
            var parent = control.parent
            while (parent) {
                // 检查是否有router属性
                if (parent.router) {
                    parent.router.go("/dock")
                    return
                }
                // 检查是否有pageRouter属性
                if (parent.pageRouter) {
                    parent.pageRouter.go("/dock")
                    return
                }
                parent = parent.parent
            }
        }
    }
}
