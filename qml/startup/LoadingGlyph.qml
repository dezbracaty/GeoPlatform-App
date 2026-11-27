import QtQuick

Item {
    id: glyph
    required property color accentColor
    required property color mutedColor
    required property bool animating
    width: 32
    height: 32

    component Fold: Item {
        id: fold
        required property color strokeColor
        Rectangle {
            x: 4; y: 7
            width: 13; height: 2; radius: 1
            rotation: 22.62
            transformOrigin: Item.Left
            color: fold.strokeColor
            antialiasing: true
        }
        Rectangle {
            x: 16; y: 12
            width: 13; height: 2; radius: 1
            rotation: -22.62
            transformOrigin: Item.Left
            color: fold.strokeColor
            antialiasing: true
        }
    }

    Repeater {
        model: 3
        Fold {
            required property int index
            y: index * 6
            strokeColor: glyph.mutedColor
            opacity: 0.28
        }
    }
    Fold { id: topFold; strokeColor: glyph.accentColor; opacity: 0 }
    Fold { id: middleFold; y: 6; strokeColor: glyph.accentColor; opacity: 0 }
    Fold { id: bottomFold; y: 12; strokeColor: glyph.accentColor; opacity: 1 }

    SequentialAnimation {
        id: pulse
        running: glyph.visible && glyph.animating
        loops: Animation.Infinite
        onRunningChanged: {
            if (!running) {
                topFold.opacity = 0
                middleFold.opacity = 0
                bottomFold.opacity = 1
            }
        }
        PauseAnimation { duration: 240 }
        ParallelAnimation {
            OpacityAnimator { target: bottomFold; from: 1; to: 0; duration: 120 }
            OpacityAnimator { target: middleFold; from: 0; to: 1; duration: 120 }
        }
        PauseAnimation { duration: 240 }
        ParallelAnimation {
            OpacityAnimator { target: middleFold; from: 1; to: 0; duration: 120 }
            OpacityAnimator { target: topFold; from: 0; to: 1; duration: 120 }
        }
        PauseAnimation { duration: 240 }
        ParallelAnimation {
            OpacityAnimator { target: topFold; from: 1; to: 0; duration: 120 }
            OpacityAnimator { target: bottomFold; from: 0; to: 1; duration: 120 }
        }
    }
}
