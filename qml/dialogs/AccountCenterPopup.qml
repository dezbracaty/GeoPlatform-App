import QtQuick
import QtQuick.Layouts
import SmoothUI
import SmoothUI.Controls
import SmoothUI.impl
import GPlatform
import "../components"

AppPopup {
    id: control
    objectName: "accountCenterPopup"

    SmoothUI.theme: Theme.of(control)
    SmoothUI.radius: PrintWorkspaceStyle.dialogRadius

    property string selectedSection: "overview"
    readonly property bool showingFlashforge: selectedSection === "flashforge"
    readonly property bool showingAi: selectedSection === "ai"

    function openSection(section) {
        selectedSection = section === "flashforge" || section === "ai"
                ? section : "overview"
        open()
    }

    function submitFlashforgeLogin() {
        if (!PrinterService.supportsAccount || flashforgeUserName.text.trim().length === 0
                || flashforgePassword.text.length === 0
                || PrinterService.authState === PrinterService.SigningIn) {
            return
        }
        PrinterService.login(flashforgeUserName.text, flashforgePassword.text)
    }

    function scrollToSelectedSection() {
        if (!showingFlashforge && !showingAi) {
            content.contentY = 0
            return
        }
        var target = showingFlashforge ? flashforgeCard : aiCard
        var maximumY = Math.max(0, content.contentHeight - content.height)
        content.contentY = Math.max(
                    0, Math.min(cards.y + target.y - 20, maximumY))
    }

    preferredWidth: 640
    preferredHeight: Math.max(450, cards.implicitHeight + 104)
    title: qsTr("Accounts and Services")
    windowEscapeEnabled: true

    onOpened: {
        AIAccountBridge.refreshAccount()
        Qt.callLater(function() {
            if (!control.opened)
                return
            control.scrollToSelectedSection()
            if (control.selectedSection === "flashforge" && !PrinterService.signedIn)
                flashforgeUserName.forceActiveFocus(Qt.PopupFocusReason)
            else
                content.forceActiveFocus(Qt.PopupFocusReason)
        })
    }
    onClosed: flashforgePassword.clear()

    Connections {
        target: PrinterService
        function onAuthChanged() {
            if (PrinterService.authState !== PrinterService.SigningIn) {
                flashforgePassword.clear()
                if (PrinterService.signedIn)
                    flashforgeUserName.clear()
            }
        }
    }

    Flickable {
        id: content
        anchors.fill: parent
        focus: true
        contentWidth: width
        contentHeight: cards.implicitHeight + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: cards
                width: parent.width - 40
                x: 20
                y: 20
                spacing: 14

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: qsTr("Printing Service")
                    font: Typography.bodyStrong
                    color: control.SmoothUI.theme.res.textFillColorPrimary
                }

                Rectangle {
                    id: flashforgeCard
                    Layout.fillWidth: true
                    Layout.preferredHeight: flashforgeCardContent.implicitHeight + 32
                    radius: PrintWorkspaceStyle.menuRadius
                    color: control.SmoothUI.theme.res.cardBackgroundFillColorDefault
                    border.width: 1
                    border.color: control.showingFlashforge
                                  ? control.SmoothUI.theme.accentColor.defaultBrushFor()
                                  : control.SmoothUI.theme.res.cardStrokeColorDefault

                    ColumnLayout {
                        id: flashforgeCardContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 16
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            FeatureHeaderLogo {
                                Layout.preferredWidth: 44
                                Layout.preferredHeight: 44
                                markSize: 44
                                mainIcon: FluentIcons.graph_Printer3D
                                badgeIcon: FluentIcons.graph_Link
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: qsTr("Flashforge Printing Service")
                                    font: Typography.bodyStrong
                                    color: control.SmoothUI.theme.res.textFillColorPrimary
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: PrinterService.signedIn
                                          ? (PrinterService.accountName.length > 0
                                             ? PrinterService.accountName : qsTr("Connected to Flashforge service"))
                                          : qsTr("Sign in to access cloud devices; LAN devices do not require sign-in")
                                    elide: Text.ElideRight
                                    font: Typography.caption
                                    color: control.SmoothUI.theme.res.textFillColorSecondary
                                }
                            }

                            PillButton {
                                text: PrinterService.signedIn ? qsTr("Signed In") : qsTr("Not Signed In")
                                enabled: false
                                highlighted: PrinterService.signedIn
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: control.SmoothUI.theme.res.dividerStrokeColorDefault
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: !PrinterService.signedIn

                            InfoBar {
                                Layout.fillWidth: true
                                visible: !PrinterService.agentAvailable
                                         && PrinterService.agentError.length > 0
                                severity: InfoBarType.Error
                                title: qsTr("Printing device service unavailable")
                                message: PrinterService.agentError
                                closable: false
                                messageMaximumWidth: Math.max(160, width - 190)
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    text: qsTr("Phone number or email")
                                    font: Typography.caption
                                    color: control.SmoothUI.theme.res.textFillColorSecondary
                                }
                                TextField {
                                    id: flashforgeUserName
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Enter your phone number or email")
                                    enabled: PrinterService.supportsAccount && PrinterService.agentAvailable
                                             && PrinterService.authState !== PrinterService.SigningIn
                                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                                    onAccepted: flashforgePassword.forceActiveFocus()
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    text: qsTr("Password")
                                    font: Typography.caption
                                    color: control.SmoothUI.theme.res.textFillColorSecondary
                                }
                                PasswordBox {
                                    id: flashforgePassword
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Enter your password")
                                    enabled: PrinterService.agentAvailable
                                             && PrinterService.authState !== PrinterService.SigningIn
                                    onAccepted: control.submitFlashforgeLogin()
                                }
                            }

                            InfoBar {
                                Layout.fillWidth: true
                                visible: PrinterService.authState === PrinterService.AuthError
                                severity: InfoBarType.Error
                                title: qsTr("Sign-in failed")
                                message: PrinterService.authError
                                closable: false
                                messageMaximumWidth: Math.max(160, width - 150)
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                ProgressRing {
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                    strokeWidth: 2
                                    indeterminate: true
                                    visible: PrinterService.authState === PrinterService.SigningIn
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: PrinterService.authState === PrinterService.SigningIn
                                    text: qsTr("Signing in to Flashforge")
                                    font: Typography.caption
                                    color: control.SmoothUI.theme.res.textFillColorSecondary
                                }
                                Item {
                                    Layout.fillWidth: true
                                    visible: PrinterService.authState !== PrinterService.SigningIn
                                }
                                Button {
                                    implicitWidth: 88
                                    text: qsTr("Sign In")
                                    highlighted: true
                                    enabled: PrinterService.agentAvailable
                                             && flashforgeUserName.text.trim().length > 0
                                             && flashforgePassword.text.length > 0
                                             && PrinterService.authState !== PrinterService.SigningIn
                                    onClicked: control.submitFlashforgeLogin()
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: PrinterService.signedIn
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Flashforge account connected")
                                font: Typography.caption
                                color: control.SmoothUI.theme.res.textFillColorSecondary
                            }
                            Button {
                                text: qsTr("Sign Out")
                                onClicked: PrinterService.logout()
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: qsTr("AI Service")
                    font: Typography.bodyStrong
                    color: control.SmoothUI.theme.res.textFillColorPrimary
                }

                Rectangle {
                    id: aiCard
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(142, aiCardContent.implicitHeight + 32)
                    radius: PrintWorkspaceStyle.menuRadius
                    color: control.SmoothUI.theme.res.cardBackgroundFillColorDefault
                    border.width: 1
                    border.color: control.showingAi
                                  ? control.SmoothUI.theme.accentColor.defaultBrushFor()
                                  : control.SmoothUI.theme.res.cardStrokeColorDefault

                    ColumnLayout {
                        id: aiCardContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 16
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            FeatureHeaderLogo {
                                Layout.preferredWidth: 44
                                Layout.preferredHeight: 44
                                markSize: 44
                                mainIcon: FluentIcons.graph_ChatBubbles
                                badgeIcon: FluentIcons.graph_FavoriteStarFill
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: qsTr("ChatGPT / Codex")
                                    font: Typography.bodyStrong
                                    color: control.SmoothUI.theme.res.textFillColorPrimary
                                }
                                Label {
                                    text: AIAccountBridge.signedIn
                                          ? (AIAccountBridge.email.length > 0
                                             ? AIAccountBridge.email : qsTr("ChatGPT account connected"))
                                          : qsTr("Use your Codex subscription without storing an API key in the project")
                                    font: Typography.caption
                                    color: control.SmoothUI.theme.res.textFillColorSecondary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: aiStatus.implicitWidth + 16
                                Layout.preferredHeight: 24
                                radius: 12
                                color: AIAccountBridge.signedIn
                                       ? control.SmoothUI.theme.res.systemFillColorSuccessBackground
                                       : control.SmoothUI.theme.res.subtleFillColorSecondary
                                Label {
                                    id: aiStatus
                                    anchors.centerIn: parent
                                    text: AIAccountBridge.signedIn
                                          ? qsTr("Signed In")
                                          : (AIAccountBridge.authState === AIAccountBridge.Authorizing
                                             ? qsTr("Waiting for Verification") : qsTr("Not Signed In"))
                                    font: Typography.caption
                                    color: AIAccountBridge.signedIn
                                           ? control.SmoothUI.theme.res.systemFillColorSuccess
                                           : control.SmoothUI.theme.res.textFillColorSecondary
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: control.SmoothUI.theme.res.dividerStrokeColorDefault
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 56
                            spacing: 12
                            visible: AIAccountBridge.authState === AIAccountBridge.Checking

                            ProgressRing {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                indeterminate: true
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: qsTr("Checking Codex sign-in status")
                                    font: Typography.bodyStrong
                                }
                                Label {
                                    text: qsTr("Credentials are managed by Codex App Server")
                                    font: Typography.caption
                                    color: control.SmoothUI.theme.res.textFillColorSecondary
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: AIAccountBridge.authState === AIAccountBridge.SignedOut

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("After sign-in, AI chat uses your ChatGPT/Codex subscription through the local Codex App Server.")
                                wrapMode: Text.WordWrap
                                font: Typography.caption
                                color: control.SmoothUI.theme.res.textFillColorSecondary
                            }
                            Button {
                                text: qsTr("Sign in with ChatGPT/Codex")
                                icon.name: FluentIcons.graph_OpenInNewWindow
                                highlighted: true
                                onClicked: AIAccountBridge.startDeviceCodeLogin()
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: AIAccountBridge.authState === AIAccountBridge.Authorizing

                            Label {
                                Layout.fillWidth: true
                                text: AIAccountBridge.userCode.length > 0
                                      ? qsTr("Enter the following code on the verification page")
                                      : qsTr("Requesting device code…")
                                font: Typography.caption
                                color: control.SmoothUI.theme.res.textFillColorSecondary
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 54
                                radius: 8
                                visible: AIAccountBridge.userCode.length > 0
                                color: control.SmoothUI.theme.res.subtleFillColorSecondary
                                border.width: 1
                                border.color: control.SmoothUI.theme.res.cardStrokeColorDefault
                                Label {
                                    anchors.centerIn: parent
                                    text: AIAccountBridge.userCode
                                    font.pixelSize: 23
                                    font.bold: true
                                    font.letterSpacing: 2
                                    color: control.SmoothUI.theme.res.textFillColorPrimary
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: AIAccountBridge.verificationUrl.length > 0
                                text: AIAccountBridge.verificationUrl
                                elide: Text.ElideMiddle
                                font: Typography.caption
                                color: control.SmoothUI.theme.accentColor.defaultBrushFor()
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Button {
                                    text: qsTr("Open Verification Page")
                                    highlighted: true
                                    enabled: AIAccountBridge.verificationUrl.length > 0
                                    onClicked: Qt.openUrlExternally(AIAccountBridge.verificationUrl)
                                }
                                Button {
                                    text: qsTr("Copy Verification Code")
                                    enabled: AIAccountBridge.userCode.length > 0
                                    onClicked: Tools.clipText(AIAccountBridge.userCode)
                                }
                                Item { Layout.fillWidth: true }
                                Button {
                                    text: qsTr("Cancel")
                                    onClicked: AIAccountBridge.cancelDeviceCodeLogin()
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: AIAccountBridge.authState === AIAccountBridge.SignedIn

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: AIAccountBridge.planType.length > 0
                                          ? qsTr("Subscription type: %1").arg(AIAccountBridge.planType)
                                          : qsTr("ChatGPT/Codex account connected")
                                    font: Typography.caption
                                    color: control.SmoothUI.theme.res.textFillColorSecondary
                                }
                                Button {
                                    text: qsTr("Sign Out of AI Account")
                                    onClicked: AIAccountBridge.logout()
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: AIAccountBridge.authState === AIAccountBridge.Error

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: errorText.implicitHeight + 20
                                radius: 8
                                color: control.SmoothUI.theme.res.systemFillColorCriticalBackground
                                Label {
                                    id: errorText
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    text: AIAccountBridge.errorMessage
                                    wrapMode: Text.WordWrap
                                    font: Typography.caption
                                    color: control.SmoothUI.theme.res.systemFillColorCritical
                                }
                            }
                            Button {
                                text: qsTr("Check Again")
                                onClicked: AIAccountBridge.refreshAccount()
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("ChatGPT/Codex sign-in and OpenAI Platform API billing are separate.")
                    horizontalAlignment: Text.AlignHCenter
                    font: Typography.caption
                    color: control.SmoothUI.theme.res.textFillColorTertiary
                }
            }
    }
}
