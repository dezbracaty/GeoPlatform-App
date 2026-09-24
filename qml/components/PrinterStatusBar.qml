import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform

// Compact entry for selecting a physical printer and opening its details.
// Device selection is the public interaction; transport sessions stay inside
// the printer agent and are deliberately not exposed as connect buttons.
Button {
    id: root
    objectName: "printerStatusButton"

    SmoothUI.theme: Theme.of(root)

    property bool agentAvailable: true
    property bool hasSelectedDevice: false
    property bool selectedDeviceOnline: false
    property bool loading: false
    property bool serviceError: false
    property bool accessRequired: false
    property string errorText: ""
    property string printerName: ""
    property string printerIP: ""
    property bool controlPanelOpen: false
    property bool compact: false
    property bool embedded: false

    property real nozzleTemp: 0
    property real nozzleTargetTemp: 0
    property real bedTemp: 0
    property real bedTargetTemp: 0
    property bool isPrinting: false
    property real printProgress: 0

    readonly property bool deviceReady:
        hasSelectedDevice && selectedDeviceOnline && !loading
        && !serviceError && !accessRequired
    readonly property string displayName:
        printerName.length > 0 ? printerName : qsTr("Printing device")
    readonly property string statusText: {
        if (!agentAvailable || (serviceError && !accessRequired))
            return qsTr("Device service unavailable")
        if (!hasSelectedDevice)
            return qsTr("Select a printer")
        if (!selectedDeviceOnline)
            return qsTr("%1 · Offline").arg(displayName)
        if (accessRequired)
            return qsTr("%1 · Access required").arg(displayName)
        if (loading)
            return qsTr("Loading %1").arg(displayName)
        return displayName
    }

    signal controlPanelRequested()

    implicitHeight: compact ? PrintWorkspaceStyle.controlHeight : 40
    implicitWidth: contentRow.implicitWidth + leftPadding + rightPadding
    readonly property real minimumContentWidth: contentRow.Layout.minimumWidth + leftPadding + rightPadding
    padding: 0
    leftPadding: compact ? 8 : 14
    rightPadding: compact ? 8 : 10
    flat: true

    background: Rectangle {
        anchors.fill: parent
        color: root.embedded ? "transparent"
                             : root.SmoothUI.theme.res.solidBackgroundFillColorBase
        radius: root.embedded ? PrintWorkspaceStyle.controlRadius : 10
        border.color: root.SmoothUI.theme.res.cardStrokeColorDefault
        border.width: root.embedded ? 0 : 1

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: root.SmoothUI.theme.res.subtleFillColorSecondary
            opacity: root.hovered ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }
        }
    }

    contentItem: RowLayout {
        id: contentRow
        spacing: 8

        Label {
            text: qsTr("Printing device")
            font: Typography.caption
            color: root.SmoothUI.theme.res.textFillColorTertiary
            visible: !root.compact
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 22
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
            visible: !root.compact
        }

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: (!root.agentAvailable || root.serviceError)
                   ? root.SmoothUI.theme.res.systemFillColorCritical
                   : (root.accessRequired || root.loading
                      ? root.SmoothUI.theme.res.systemFillColorCaution
                      : (root.deviceReady
                         ? root.SmoothUI.theme.res.systemFillColorSuccess
                         : root.SmoothUI.theme.res.textFillColorTertiary))

            SequentialAnimation on opacity {
                running: root.loading && root.visible
                loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 500 }
                NumberAnimation { to: 1; duration: 500 }
            }
        }

        Icon {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            source: FluentIcons.graph_Printer3D
            color: root.deviceReady
                   ? root.SmoothUI.theme.res.textFillColorPrimary
                   : root.SmoothUI.theme.res.textFillColorSecondary
        }

        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            elide: Text.ElideRight
            text: root.statusText
            font: Typography.body
            color: (!root.agentAvailable || root.serviceError)
                   ? root.SmoothUI.theme.res.systemFillColorCritical
                   : root.SmoothUI.theme.res.textFillColorPrimary
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 22
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            color: root.SmoothUI.theme.res.dividerStrokeColorDefault
            visible: root.deviceReady && !root.compact
        }

        Label {
            visible: root.deviceReady && !root.compact
            text: qsTr("Nozzle %1° · Bed %2°")
                .arg(Math.round(root.nozzleTemp)).arg(Math.round(root.bedTemp))
            font: Typography.body
            color: root.SmoothUI.theme.res.textFillColorSecondary
        }

        Label {
            visible: root.deviceReady && root.isPrinting && !root.compact
            text: qsTr("%1%").arg(Math.round(root.printProgress))
            font: Typography.bodyStrong
            color: root.SmoothUI.theme.res.textFillColorPrimary
        }

        Icon {
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            source: root.controlPanelOpen
                    ? FluentIcons.graph_ChevronUp
                    : FluentIcons.graph_ChevronDown
            color: root.SmoothUI.theme.res.textFillColorSecondary
        }
    }

    ToolTip {
        visible: root.hovered
        delay: Theme.tooltipDelay
        text: {
            if (!root.agentAvailable || root.serviceError)
                return root.errorText.length > 0
                        ? root.errorText : qsTr("Device service unavailable")
            if (!root.hasSelectedDevice)
                return qsTr("Choose an online or offline printer")
            if (!root.selectedDeviceOnline)
                return qsTr("%1 is offline").arg(root.displayName)
            if (root.accessRequired)
                return qsTr("%1 requires an access code").arg(root.displayName)
            if (root.loading)
                return qsTr("Loading printer details")
            return qsTr("Printer: %1\nAddress: %2")
                .arg(root.displayName).arg(root.printerIP || qsTr("Unknown"))
        }
    }

    onClicked: root.controlPanelRequested()
}
