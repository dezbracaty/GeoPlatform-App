import QtQuick
import QtQuick.Layouts
import QtMultimedia
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

Item {
    id: root
    objectName: "printerVideoPlayer"

    property url playbackSource: ""
    property int playbackRetryAttempt: 0

    readonly property bool sessionReady:
        PrinterService.cameraSessionState === PrinterService.CameraReady
    readonly property bool playerLoading:
        player.mediaStatus === MediaPlayer.LoadingMedia
        || player.mediaStatus === MediaPlayer.BufferingMedia
        || playbackRetryTimer.running
    readonly property bool playerFailed: player.error !== MediaPlayer.NoError
    readonly property bool showingVideo:
        root.sessionReady && player.playbackState === MediaPlayer.PlayingState
        && !root.playerFailed

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
        visible: root.showingVideo
    }

    AudioOutput {
        id: silentAudio
        muted: true
    }

    MediaPlayer {
        id: player
        source: root.playbackSource
        videoOutput: videoOutput
        audioOutput: silentAudio
        autoPlay: true
        loops: MediaPlayer.Infinite
        onErrorOccurred: root.schedulePlaybackRetry()
        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PlayingState)
                root.playbackRetryAttempt = 0
        }
    }

    Timer {
        id: playbackRetryTimer
        repeat: false
        onTriggered: root.reloadPlaybackSource()
    }

    Connections {
        target: PrinterService
        function onCameraChanged() { root.syncPlaybackSource() }
        function onStatusChanged() { root.syncPlaybackSource() }
    }

    Component.onCompleted: syncPlaybackSource()

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 320)
        spacing: 8
        visible: !root.showingVideo

        ProgressRing {
            id: loadingRing
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            indeterminate: true
            visible: PrinterService.connectionState === PrinterService.Connecting
                     || PrinterService.cameraSessionState === PrinterService.CameraOpening
                     || PrinterService.cameraSessionState === PrinterService.CameraLoading
                     || root.playerLoading
        }
        Icon {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            visible: !loadingRing.visible
            source: FluentIcons.graph_Camera
            color: "#FFD7DCE3"
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: {
                if (!PrinterService.selectedDeviceOnline)
                    return qsTr("Video unavailable while the printer is offline")
                if (PrinterService.connectionState === PrinterService.Connecting)
                    return qsTr("Loading printer status")
                if (!PrinterService.connected)
                    return qsTr("Printer details are unavailable")
                if (!PrinterService.cameraAvailable)
                    return qsTr("Video is unavailable for this printer")
                if (PrinterService.cameraSessionState === PrinterService.CameraOpening)
                    return qsTr("Opening camera stream")
                if (PrinterService.cameraSessionState === PrinterService.CameraLoading
                        || root.playerLoading)
                    return qsTr("Loading live video")
                if (PrinterService.cameraSessionState === PrinterService.CameraError)
                    return PrinterService.cameraSessionError.length > 0
                            ? PrinterService.cameraSessionError
                            : qsTr("Unable to open the camera stream")
                if (root.playerFailed)
                    return player.errorString.length > 0
                            ? player.errorString
                            : qsTr("This video stream cannot be played")
                if (PrinterService.cameraSessionState === PrinterService.CameraClosing)
                    return qsTr("Closing camera stream")
                return qsTr("Live video is stopped")
            }
            font: Typography.body
            color: "#FFD7DCE3"
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            visible: PrinterService.connected && PrinterService.cameraAvailable
                     && (PrinterService.cameraSessionState === PrinterService.CameraIdle
                         || PrinterService.cameraSessionState === PrinterService.CameraError
                         || root.playerFailed)
            text: PrinterService.cameraSessionState === PrinterService.CameraIdle
                  ? qsTr("Play") : qsTr("Retry")
            icon.name: FluentIcons.graph_Play
            onClicked: {
                if (root.sessionReady) {
                    root.playbackRetryAttempt = 0
                    root.reloadPlaybackSource()
                } else {
                    PrinterService.startCameraStream()
                }
            }
        }
    }

    Button {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        visible: root.showingVideo
        text: qsTr("Stop")
        icon.name: FluentIcons.graph_Stop
        onClicked: PrinterService.stopCameraStream()
    }

    function syncPlaybackSource() {
        const nextSource = root.sessionReady ? PrinterService.cameraStreamUrl : ""
        if (root.playbackSource.toString() === nextSource.toString())
            return
        playbackRetryTimer.stop()
        playbackRetryAttempt = 0
        playbackSource = nextSource
    }

    function schedulePlaybackRetry() {
        if (!root.sessionReady || playbackRetryAttempt >= 4)
            return
        playbackRetryTimer.interval = 1000 * Math.pow(2, playbackRetryAttempt)
        ++playbackRetryAttempt
        playbackRetryTimer.restart()
    }

    function reloadPlaybackSource() {
        root.playbackSource = ""
        Qt.callLater(function() {
            if (root.sessionReady)
                root.playbackSource = PrinterService.cameraStreamUrl
        })
    }
}
