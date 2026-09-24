import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform 1.0
import Render 1.0

FramelessWindow {
    id: window
    objectName: "gplatform.main"
    property alias infoBarManager: infobar_manager
    width: 1382
    // macOS adds a 60 px native title bar, so the complete initial window is 1382x954.
    height: 894
    minimumWidth: 1200
    minimumHeight: 700
    visible: true
    title: qsTr("GPlatform")
    // On Windows, reserve the app bar above the navigation/AI row.
    // Other platforms retain their existing native-title-bar integration.
    fitsAppBarWindows: Qt.platform.os !== "windows"
    launchMode: WindowType.SingleInstance
    windowEffect: Global.windowEffect
    autoDestroy: false
    
    appBar: AppBar {
        implicitHeight: Qt.platform.os === "osx" ? 60 : (Qt.platform.os === "windows" ? 30 : 48)
        windowIcon: Item {}
        actionsOnLeft: Qt.platform.os === "windows"
        action: RowLayout {
            IconButton {
                id: btn_restart_qml
                implicitWidth: 46
                padding: 0
                radius: 0
                icon.width: 14
                icon.height: 14
                icon.name: FluentIcons.graph_Sync
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Restart QML Engine")
                ToolTip.delay: Theme.tooltipDelay
                onClicked: {
                    // Show confirmation before restarting
                    infobar_manager.showInfo(qsTr("Restarting QML Engine..."))
                    // Add a small delay before restart
                    Qt.callLater(function() {
                        ApplicationHelper.restartQmlEngine()
                    })
                }
            }
            IconButton {
                id: btn_dark
                implicitWidth: 46
                padding: 0
                radius: 0
                icon.width: 14
                icon.height: 14
                icon.name: Theme.dark ? FluentIcons.graph_Brightness : FluentIcons.graph_QuietHours
                ToolTip.visible: hovered
                ToolTip.text: Theme.dark ? qsTr("Light") : qsTr("Dark")
                ToolTip.delay: Theme.tooltipDelay
                onClicked: handleDarkChanged(this)
            }
            IconButton {
                id: btn_stick_on_top
                implicitWidth: 46
                padding: 0
                radius: 0
                icon.width: 14
                icon.height: 14
                icon.name: FluentIcons.graph_Pinned
                icon.color: window.topmost ? Theme.accentColor.defaultBrushFor() : this.SmoothUI.textColor
                ToolTip.visible: hovered
                ToolTip.text: window.topmost ? qsTr("Sticky on Top cancelled") : qsTr("Sticky on Top")
                ToolTip.delay: Theme.tooltipDelay
                onClicked: {
                    window.topmost = !window.topmost
                }
            }
        }
    }
    
    initialItem: "qrc:/qt/qml/GPlatform/qml/window/MainScreen.qml"
    
    onNewInit: (argument) => {
        if (argument.type === 0) {
            dialog_program_already.argsText = argument.args
            dialog_program_already.open()
        }
    }
    
    onCloseListener: function(event) {
        dialog_close.open()
        event.accepted = false
    }
    
    Component.onCompleted: {
        // Trigger layout recalculation
        width = width
        height = height
    }
    
    InfoBarManager {
        id: infobar_manager
        target: window.contentItem
        messageMaximumWidth: 380
    }

    NotificationOverlay {
        id: notification_overlay
        parent: window.contentItem
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 16
        // Keep the notification above the global print command bar.
        anchors.bottomMargin: 96
        z: 1000

        onFiberFillDebugActivated: function(previewToken) {
            if (SlicingPreviewBridge.showFiberFillDiagnostics(previewToken)) {
                ActionManager.triggerAction("environment.switch", { "environment": "preview" })
            } else {
                infobar_manager.showInfo(qsTr("铺纤填充调试"), 3000, qsTr("这次切片结果已失效，请重新切片。"))
            }
        }

        onSliceConfigurationErrorActivated: function(presetName, message,
                                                     optionKey) {
            dialog_slice_configuration_error.presetName = presetName
            dialog_slice_configuration_error.errorMessage = message
            dialog_slice_configuration_error.optionKey = optionKey
            dialog_slice_configuration_error.open()
        }
    }

    // Connect to NotificationManager signals
    Connections {
        target: NotificationManager

        function onFiberFillDebugAvailable(previewToken, rejectedCount) {
            notification_overlay.pushFiberFillDebug(previewToken, rejectedCount)
        }

        function onErrorOccurred(title, message) {
            infobar_manager.showError(title, 5000, message)
        }

        function onWarningOccurred(title, message) {
            infobar_manager.showWarning(title, 4000, message)
        }

        function onInfoOccurred(title, message) {
            infobar_manager.showInfo(title, 3000, message)
        }

        function onSuccessOccurred(title, message) {
            infobar_manager.showSuccess(title, 3000, message)
        }

        function onDialogRequested(title, message, critical) {
            dialog_notification.notificationTitle = title
            dialog_notification.notificationMessage = message
            dialog_notification.isCritical = critical
            dialog_notification.open()
        }

        function onSliceConfigurationErrorRequested(presetName, message, optionKey) {
            notification_overlay.pushSliceConfigurationError(
                        presetName, message, optionKey)
        }

        function onVerificationFailureOccurred(stepNumber, stepDesc, similarity, threshold,
                                              expectedImg, actualImg, diffImg, reportPath) {
            dialog_verification_failure.stepNumber = stepNumber
            dialog_verification_failure.stepDescription = stepDesc
            dialog_verification_failure.similarity = similarity
            dialog_verification_failure.threshold = threshold
            dialog_verification_failure.expectedImage = expectedImg
            dialog_verification_failure.actualImage = actualImg
            dialog_verification_failure.diffImage = diffImg
            dialog_verification_failure.reportPath = reportPath
            dialog_verification_failure.open()
        }
    }
    
    CircularReveal {
        id: reveal
        target: window.contentItem
        anchors.fill: parent
        z: 65535
        onImageChanged: {
            changeDark()
        }
    }
    
    function distance(x1, y1, x2, y2) {
        return Math.sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2))
    }
    
    function handleDarkChanged(button) {
        if (reveal.visible === true) {
            return
        }
        var target = window.contentItem
        var pos = button.mapToItem(target, 0, 0)
        var centerX = pos.x + button.width / 2
        var centerY = pos.y + button.height / 2
        var radius = Math.max(
            distance(centerX, centerY, 0, 0),
            distance(centerX, centerY, target.width, 0),
            distance(centerX, centerY, 0, target.height),
            distance(centerX, centerY, target.width, target.height)
        )
        reveal.start(reveal.width, reveal.height, Qt.point(centerX, centerY), radius, Theme.dark)
    }
    
    function changeDark() {
        if (Theme.dark) {
            Theme.darkMode = SmoothUI.Light
        } else {
            Theme.darkMode = SmoothUI.Dark
        }
    }
    
    ContentDialog {
        id: dialog_notification
        property string notificationTitle: ""
        property string notificationMessage: ""
        property bool isCritical: false

        title: notificationTitle
        Column {
            spacing: 20
            anchors.fill: parent
            Label {
                width: 400
                wrapMode: Text.WrapAnywhere
                text: dialog_notification.notificationMessage
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("OK")
                highlighted: dialog_notification.isCritical
                onClicked: {
                    dialog_notification.close()
                }
            }
        }
    }

    ContentDialog {
        id: dialog_close
        title: qsTr("Quit")
        Column {
            spacing: 20
            anchors.fill: parent
            Label {
                width: 300
                wrapMode: Text.WrapAnywhere
                text: qsTr("Are you sure you want to exit the program?")
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                onClicked: {
                    dialog_close.close()
                }
            }
            Button {
                text: qsTr("Minimize")
                onClicked: {
                    window.hide()
                    dialog_close.close()
                }
            }
            Button {
                text: qsTr("Ok")
                highlighted: true
                onClicked: {
                    ApplicationHelper.exit(0)
                }
            }
        }
    }

    SliceConfigurationErrorDialog {
        id: dialog_slice_configuration_error
    }
    
    ContentDialog {
        id: dialog_program_already
        property string argsText: ""
        title: qsTr("Friendly reminder")
        standardButtons: Dialog.Yes
        Column {
            spacing: 20
            anchors.fill: parent
            Label {
                width: 300
                wrapMode: Text.WrapAnywhere
                text: qsTr("The program is already running. The parameter is ->") + dialog_program_already.argsText
                bottomPadding: 30
            }
        }
    }

    // Load the verification failure dialog component dynamically
    Loader {
        id: dialog_verification_failure_loader
        source: "qrc:/qt/qml/GPlatform/qml/dialogs/VerificationFailureDialog.qml"
        active: false
        property int stepNumber: 0
        property string stepDescription: ""
        property real similarity: 0.0
        property real threshold: 0.995
        property string expectedImage: ""
        property string actualImage: ""
        property string diffImage: ""
        property string reportPath: ""

        onLoaded: {
            if (item) {
                item.stepNumber = stepNumber
                item.stepDescription = stepDescription
                item.similarity = similarity
                item.threshold = threshold
                item.expectedImage = expectedImage
                item.actualImage = actualImage
                item.diffImage = diffImage
                item.reportPath = reportPath
            }
        }

        function open() {
            active = true
            if (item) {
                item.open()
            }
        }
    }

    // Create alias for the loader
    property alias dialog_verification_failure: dialog_verification_failure_loader
}
