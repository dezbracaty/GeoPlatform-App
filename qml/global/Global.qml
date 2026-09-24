pragma Singleton

import QtQuick
import QtQuick.Controls
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

QtObject {
    id: control
    property var starter
    property var mainScreen
    property int displayMode: NavigationViewType.Minimal
    property int windowEffect: WindowEffectType.Normal
    property bool initialized: false

    // Auto-save display mode changes (only after initialization)
    onDisplayModeChanged: {
        if (initialized) {
            SettingsHelper.saveDisplayMode(displayMode)
        }
    }

    // Auto-save window effect changes (only after initialization)
    onWindowEffectChanged: {
        if (initialized) {
            SettingsHelper.saveWindowEffect(windowEffect)
        }
    }
}
