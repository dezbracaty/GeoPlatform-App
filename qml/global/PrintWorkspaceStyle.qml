pragma Singleton
import QtQuick
import SmoothUI.Controls

QtObject {
    readonly property int space4: Theme.space4
    readonly property int space8: Theme.space8
    readonly property int space12: Theme.space12
    readonly property int space16: Theme.space16
    readonly property int space24: Theme.space24

    readonly property int controlHeight: Theme.controlHeight
    readonly property int toolbarButtonSize: Theme.toolbarButtonSize
    readonly property int toolbarWidth: toolbarButtonSize + space8 * 2
    readonly property int closeButtonSize: Theme.compactControlHeight
    readonly property int popoverRowHeight: Theme.controlHeight
    readonly property int tabShellHeight: 44
    readonly property int tabHeight: Theme.controlHeight
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

    readonly property int controlRadius: Theme.controlRadius
    readonly property int menuRadius: Theme.popupRadius
    readonly property int toolbarRadius: Theme.panelRadius
    readonly property int dialogRadius: Theme.dialogRadius

    function surface(dark) { return Theme.ofDark(dark).cardBackgroundColor }
    function secondarySurface(dark) { return Theme.ofDark(dark).solidBackgroundFillColorSecondary }
    function hoverFill(dark) { return Theme.ofDark(dark).controlFillColorSecondary }
    function border(dark) { return Theme.ofDark(dark).controlStrokeColorDefault }
    function divider(dark) { return Theme.ofDark(dark).dividerStrokeColorDefault }
    function primaryText(dark) { return Theme.ofDark(dark).textFillColorPrimary }
    function secondaryText(dark) { return Theme.ofDark(dark).textFillColorSecondary }
    function tertiaryText(dark) { return Theme.ofDark(dark).textFillColorTertiary }
}
