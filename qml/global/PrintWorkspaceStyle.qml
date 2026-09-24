pragma Singleton
import QtQuick

QtObject {
    readonly property int space4: 4
    readonly property int space8: 8
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space24: 24

    readonly property int controlHeight: 36
    readonly property int toolbarButtonSize: 40
    readonly property int toolbarWidth: 52
    readonly property int closeButtonSize: 32
    readonly property int popoverRowHeight: 36
    readonly property int tabShellHeight: 44
    readonly property int tabHeight: 36
    readonly property int commandBarHeight: 52
    readonly property int globalCommandBarHeight: 64
    readonly property int titleBarHeight: 48
    readonly property int machineSelectorWidth: 468
    readonly property int machineSelectorHeight: 48
    readonly property int machinePopoverHeight: 430
    readonly property int slicingSettingsPopoverWidth: 960
    readonly property int slicingSettingsPopoverHeight: 680
    readonly property int printerDevicePopoverWidth: 680
    readonly property int printerDevicePopoverHeight: 520
    readonly property int machinePopoverOffset: 8
    readonly property int printContextSpacing: 6
    readonly property int settingsTransitionDuration: 220

    readonly property int controlRadius: 6
    readonly property int menuRadius: 8
    readonly property int toolbarRadius: 10
    readonly property int dialogRadius: 12

    function surface(dark) { return dark ? "#FF202020" : "#FFFFFFFF" }
    function secondarySurface(dark) { return dark ? "#FF2B2B2B" : "#FFF6F7F9" }
    function hoverFill(dark) { return dark ? "#FF34383F" : "#FFF1F4F8" }
    function border(dark) { return dark ? "#FF414750" : "#FFD9DEE7" }
    function divider(dark) { return dark ? "#FF343941" : "#FFE7EAF0" }
    function primaryText(dark) { return dark ? "#FFF5F7FA" : "#FF20242A" }
    function secondaryText(dark) { return dark ? "#FFB4BCC8" : "#FF606975" }
    function tertiaryText(dark) { return dark ? "#FF858E9C" : "#FF8A94A3" }
    function shadow(dark, opacity) {
        return Qt.rgba(0, 0, 0, dark ? Math.min(0.72, opacity * 1.7) : opacity)
    }
}
