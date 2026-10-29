import QtQuick
import SmoothUI.Controls
import GPlatform

Item {
    id: control
    objectName: "print.userGuide"
    property Item navigation
    property QtObject aiPanel
    property var sourceWorkspace: null
    property var printHost: null
    property Item overlay: null
    property string phase: "idle"
    readonly property bool running: phase !== "idle"
    property bool mainWindowSession: false
    property bool switchingWorkspace: false
    property var saved: ({})
    property var groupStates: []
    property string recordedStep: ""
    property string pendingGeometry: ""
    property bool targetUnavailable: false
    property bool hasShownStep: false
    readonly property var currentStep: tour.currentStep
    signal sessionEnded(string reason, bool shown)

    function addStep(id, title, description, target, prepare, ready) {
        return { id: id, title: title, description: description,
                 resolveTarget: target,
                 target: function() { return control.targetUnavailable ? null : target() },
                 prepare: prepare || function() {}, ready: ready || function() { return true },
                 onNextListener: function() { control.go(tour.index + 1) },
                 onPreviousListener: function() { control.go(tour.index - 1) } }
    }

    function navigationRegion() {
        // Both lists are public controls. Highlight their lowest common visual container,
        // which includes the footer without making a proxy rectangle or scanning object names.
        for (let item = navigation.sideBarView; item && item !== navigation; item = item.parent) {
            for (let footer = navigation.sideBarFooterView; footer && footer !== navigation; footer = footer.parent) {
                if (item === footer) return item
            }
        }
        return null
    }
    function makeSteps() {
        const steps = []
        steps.push(addStep("viewport", qsTr("3D View"),
            qsTr("This is the whole 3D workspace. Rotate, pan and zoom to inspect models on the build plate. Enter or Space advances the guide; Escape closes it.")
                + (mainWindowSession ? "" : "\n" + qsTr("This guide covers the viewport and toolbars in this floating window. Navigation, print tabs, print status and AI belong to the main window.")),
            function() { return control.mainWindowSession ? control.printHost.rendererForPage(control.printHost.activePageName)
                                                         : control.sourceWorkspace.guideViewport },
            null, function() { return !control.mainWindowSession || control.printHost.guidePageReady(control.printHost.activePageName) }))

        if (mainWindowSession) {
            steps.push(addStep("navigation.overview", qsTr("Main Navigation"),
                qsTr("Use this area to move between 3D Print, Model Center and Dashboard. Settings and About are in the navigation footer. Model Center contains library and model entries; Dashboard is an overview with some demonstration content. In top navigation, More contains entries that do not fit. The guide keeps your current main page."),
                function() { return control.navigationRegion() },
                function() { control.navigation.prepareGuideNavigation() },
                function() { return control.navigation.guideNavigationReady }))
            const tabs = [
                ["prepare", qsTr("Prepare"), qsTr("Prepare is where you import, select and edit models before slicing. The surrounding toolbars and print controls support this workflow.")],
                ["preview", qsTr("Preview"), qsTr("Preview shows an existing slicing result so you can inspect layers and toolpaths before exporting or printing. Without a result it shows its empty state. The guide does not start slicing.")],
                ["realistic", qsTr("Realistic View"), qsTr("This page displays the same models with the realistic renderer. Use it to inspect their appearance; it is another view of your models.")]
            ]
            for (let t = 0; t < tabs.length; ++t) {
                const tab = tabs[t]
                steps.push(addStep("tab." + tab[0], tab[1], tab[2],
                    function() { return control.printHost.guideTab(tab[0]) },
                    null, function() { return control.printHost.guidePageReady(tab[0]) }))
            }
        }
        steps.push(addStep("toolbar.left", qsTr("Left Toolbar"),
            qsTr("The five groups organize model preparation: File imports and manages files; View controls the camera and viewport; Transform adjusts placement and size; Edit contains model editing tools; Slice prepares slicing operations. Select a model when a tool requires one. Some entries are unavailable or not yet implemented; their actual enabled state applies."),
            function() { return control.sourceWorkspace.guideLeftToolbar }))
        if (mainWindowSession) {
            steps.push(addStep("toolbar.right", qsTr("Scene and help"),
                qsTr("Use the View menu to adjust scene, background, and build plate appearance. Find the guide and About in the Help menu. The print configuration panel can be expanded, collapsed, or hidden."),
                function() { return control.sourceWorkspace.guideRightToolbar }))
            steps.push(addStep("print.controls", qsTr("Print Configuration and Status"),
                qsTr("This whole area connects the printing workflow: choose printer, material and process settings, check model readiness and slicing results, and see device, transfer and print-job status. The available controls follow the current state. Export or print requires a valid result and the appropriate printer connection; the guide sends no job."),
                function() { return control.printHost.guidePrintBar }))
            steps.push(addStep("ai.panel", qsTr("AI Assistant"),
                qsTr("The top-right AI button or Alt+I opens this assistant. Ask questions or request application operations, then read responses and operation results here. Sign in through Accounts and Services before using it. The guide shows the whole panel and preserves your draft, history and any ongoing request; it sends no message."),
                function() { return control.aiPanel.guidePanelItem },
                function() { AIChatBridge.openPanel() },
                function() { return control.aiPanel.opened }))
        }
        return steps
    }

    function start(workspace, host) {
        if (running || !workspace || !workspace.guideViewport || !workspace.guideOverlay)
            return false
        const sessionOverlay = workspace.guideOverlay
        if (!sessionOverlay.Window.window || !sessionOverlay.Window.window.visible
                || sessionOverlay.width <= 0 || sessionOverlay.height <= 0)
            return false
        sourceWorkspace = workspace
        printHost = host
        overlay = sessionOverlay
        hasShownStep = false
        mainWindowSession = overlay.Window.window === navigation.Window.window
        saved = { focus: overlay.Window.window.activeFocusItem, workspace: workspace.guideState() }
        saved.workspaceSummary = workspace.guideSummary()
        groupStates = workspace.guideGroups.map(function(group) { return { owner: group, state: group.guideState() } })
        if (mainWindowSession) {
            saved.page = printHost.activePageName
            saved.navigationSummary = navigation.guideSummary()
            saved.navigation = navigation.beginGuide()
            saved.ai = { visible: AIChatBridge.panelVisible, mode: aiPanel.guideMode, summary: aiPanel.guideSummary() }
            aiPanel.guideMode = true
        }
        phase = "preparing"
        tour.steps = makeSteps()
        tour.busy = true
        tour.statusText = qsTr("Preparing the highlighted control…")
        if (!tour.open() || !tour.popupItem) {
            close("failed")
            return false
        }
        go(0)
        return true
    }

    function selectGuidePage(name) {
        if (!mainWindowSession || printHost.activePageName === name) return
        if (FiberMaskBridge.active || CutToolBridge.active)
            throw new Error(qsTr("Finish or cancel the current fiber-region or cut edit before changing workspaces. The guide preserves your edit and does not switch this page."))
        const renderer = printHost.rendererForPage(name)
        if (!printHost.dockReady || !renderer || renderer.Window.window !== overlay.Window.window)
            throw new Error(qsTr("This workspace is not ready in the guide's window."))
        switchingWorkspace = true
        try {
            // Temporary presentation only: retain every page's camera and the menu environment.
            printHost.activatePage(name, false, false)
        } finally {
            switchingWorkspace = false
        }
    }

    function leaveStep(ending) {
        for (var i = 0; i < groupStates.length; ++i) {
            var entry = groupStates[i]
            if (entry.owner)
                entry.owner.restoreGuideState(entry.state)
        }
        if (sourceWorkspace && saved.workspace)
            sourceWorkspace.restoreGuideState(saved.workspace)
        if (!ending && mainWindowSession && saved.navigation)
            navigation.beginGuide()
        if (!ending && mainWindowSession && saved.ai) {
            if (!saved.ai.visible && AIChatBridge.panelVisible)
                AIChatBridge.closePanel()
            else if (saved.ai.visible && !AIChatBridge.panelVisible)
                AIChatBridge.openPanel()
        }
        if (ending && mainWindowSession && saved.page)
            selectGuidePage(saved.page)
    }
    function go(index) {
        if (!running || phase === "closing") return
        if (index >= tour.steps.length) { close("complete"); return }
        if (index < 0) return
        phase = "preparing"
        tour.busy = true
        targetUnavailable = false
        recordedStep = ""
        pendingGeometry = ""
        tour.statusText = qsTr("Preparing the highlighted control…")
        leaveStep()
        tour.index = index
        try {
            if (mainWindowSession) {
                const id = currentStep.id
                if (id.indexOf("tab.") === 0) selectGuidePage(id.substring(4))
                else if (id.indexOf("toolbar.") === 0 || id === "print.controls")
                    selectGuidePage("prepare")
                else if (id === "viewport") selectGuidePage(saved.page)
            }
            currentStep.prepare()
            tour.focusGuide()
        } catch (error) {
            unavailable(String(error))
            return
        }
        preparationTimeout.restart()
        Qt.callLater(tryReady)
    }
    function tryReady() {
        if (phase !== "preparing") return
        try {
            const item = currentStep.resolveTarget()
            if (!item || !item.visible || item.width <= 0 || item.height <= 0
                    || item.Window.window !== overlay.Window.window || !currentStep.ready()) return
            tour.statusText = ""
            tour.busy = false
            tour.refreshTarget()
            if (!tour.targetValid) { tour.busy = true; return }
            preparationTimeout.stop()
            phase = "showing"
            tour.focusGuide()
        } catch (error) {
            unavailable(String(error))
        }
    }
    function unavailable(reason) {
        preparationTimeout.stop()
        phase = "showing"
        targetUnavailable = true
        tour.busy = false
        tour.statusText = currentStep.description + "\n\n" + qsTr("This control cannot currently be shown: %1\nCheck its prerequisites, continue to the next step or close the guide.").arg(reason)
        console.warn("[UserGuide] unavailable " + JSON.stringify({ step: currentStep.id, reason: reason }))
        tour.focusGuide()
    }
    function recordStep() {
        if (phase !== "showing" || !tour.targetValid || recordedStep === currentStep.id
                || tour.cardRect.width <= 0 || tour.cardRect.height <= 0) return
        const geometry = JSON.stringify([tour.targetRect, tour.highlightRect, tour.cardRect])
        if (pendingGeometry !== geometry) { pendingGeometry = geometry; return }
        const target = currentStep.target()
        recordedStep = currentStep.id
        hasShownStep = true
        console.info("[UserGuide] step " + JSON.stringify({ step: currentStep.id, title: currentStep.title,
            target: target.objectName, window: overlay.Window.window.objectName,
            targetRect: tour.targetRect, highlightRect: tour.highlightRect, cardRect: tour.cardRect,
            valid: tour.targetValid, index: tour.index, count: tour.steps.length,
            page: mainWindowSession && printHost ? printHost.activePageName : null }))
    }
    function close(reason = "interrupted") {
        if (!running || phase === "closing") return
        phase = "closing"
        preparationTimeout.stop()
        try {
            leaveStep(true)
        } catch (error) {
            console.warn("[UserGuide] restore unavailable: " + String(error))
        }
        // Restore each surviving owner even if the workspace was destroyed during a step.
        if (mainWindowSession && saved.navigation && navigation)
            navigation.restoreGuideState(saved.navigation)
        if (mainWindowSession && saved.ai && aiPanel) {
            aiPanel.guideMode = saved.ai.mode
            if (!saved.ai.visible && AIChatBridge.panelVisible) AIChatBridge.closePanel()
            else if (saved.ai.visible && !AIChatBridge.panelVisible) AIChatBridge.openPanel()
        }
        tour.close(reason)
        const focus = saved.focus
        const restore = {
            navigationBefore: saved.navigationSummary,
            pageBefore: saved.page,
            pageAfter: mainWindowSession && printHost ? printHost.activePageName : null,
            navigationAfter: mainWindowSession && navigation ? navigation.guideSummary() : null,
            workspaceBefore: saved.workspaceSummary,
            workspaceAfter: sourceWorkspace ? sourceWorkspace.guideSummary() : null,
            groups: groupStates.map(function(entry) {
                return { owner: entry.owner ? entry.owner.objectName : "destroyed",
                    before: entry.state, after: entry.owner ? entry.owner.guideState() : null }
            }),
            aiBefore: saved.ai ? saved.ai.summary : null,
            aiAfter: mainWindowSession && aiPanel ? aiPanel.guideSummary() : null,
            focusOwner: focus ? focus.objectName : "destroyed"
        }
        phase = "idle"
        sourceWorkspace = null
        printHost = null
        groupStates = []
        saved = ({})
        overlay = null
        if (focus && focus.visible)
            focus.forceActiveFocus()
        restore.focusRestored = focus && focus.visible ? focus.activeFocus : false
        console.info("[UserGuide] restore " + JSON.stringify(restore))
        const shown = hasShownStep
        hasShownStep = false
        sessionEnded(reason, shown)
    }

    Timer {
        id: preparationTimeout
        interval: 2000
        onTriggered: control.unavailable(qsTr("The target is hidden, unavailable or has not finished opening."))
    }
    Connections {
        target: control.printHost
        function onActivePageNameChanged() {
            if (!control.running || control.switchingWorkspace || control.phase === "closing") return
            const session = control.saved
            // An external switch can happen while Prepare is already inactive.
            Qt.callLater(function() {
                if (control.running && control.saved === session) control.close("interrupted")
            })
        }
    }
    Connections {
        target: control.overlay ? control.overlay.Window.window : null
        function onAfterAnimating() { control.tryReady(); control.recordStep() }
        function onVisibleChanged() { if (control.overlay && !control.overlay.Window.window.visible) control.close("interrupted") }
    }
    Component.onDestruction: close("interrupted")
    Tour {
        id: tour
        objectName: "print.workspace.tour"
        popupParent: control.overlay
        offset: 0
        radius: 8
        blockTargetInteraction: true
        keyboardNavigation: true
        onClosed: control.close(tour.lastCloseReason)
    }
}
