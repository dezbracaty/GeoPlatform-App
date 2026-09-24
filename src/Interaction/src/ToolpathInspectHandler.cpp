#include "ToolpathInspectHandler.hpp"
#include "InteractionRegistration.hpp"
#include "InteractionRuntime.hpp"
#include "PickService.hpp"
#include "SnapService.hpp"
#include "ViewportInputRouter.hpp"
#include <QGuiApplication>
#include <QWindow>
#include <QMouseEvent>
#include <QKeyEvent>
#include "Foundation/Log.h"
#include <SlicingPreviewBridge.hpp>
#include <DocumentManager.hpp>
#include <WindowDB.hpp>
#include <QPointer>

REGISTER_PERSISTENT_INTERACTION_ACTION(ToolpathInspectHandler, "preview.inspect")

ToolpathInspectHandler::ToolpathInspectHandler(QObject* parent) : IActionHandlerBase(parent) {
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(16);
    connect(&m_refresh, &QTimer::timeout, this, &ToolpathInspectHandler::refresh);
    connect(ViewportInputRouter::getInstance(), &ViewportInputRouter::pointerExited, this, [this](qulonglong view) {
        if (m_viewId == DBInstanceID(view)) leaveViewport();
    });
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        app->installEventFilter(this);
        connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) leave();
        });
        connect(app, &QGuiApplication::focusWindowChanged, this, [this](QWindow* window) {
            if (!window || (m_pointerWindow && window != m_pointerWindow)) leave();
        });
    }
    auto* bridge = SlicingPreviewBridge::instance();
    m_snapPreview.setShowCursor(bridge->previewCursorSnapEnabled());
    connect(bridge, &SlicingPreviewBridge::previewCursorSnapEnabledChanged, this, [this, bridge] {
        m_snapPreview.setShowCursor(bridge->previewCursorSnapEnabled());
        if (canQuery()) refresh();
    });
    connect(bridge, &SlicingPreviewBridge::currentToolpathPreviewChanged, this, [this] { clearSnap(); requestRefresh(); });
    connect(bridge, &SlicingPreviewBridge::previewPanelVisibleChanged, this, &ToolpathInspectHandler::syncEnabled);
    connect(bridge->gcodeLines(), &GCodeLineModel::navigationRequested, this, [this] {
        m_updatesAllowed = false;
        clearSnap();
    });
    // Coalesce relevant changes and fetch the latest DB state at dispatch.
    const QPointer<ToolpathInspectHandler> self(this);
    m_listener = DocumentManager::instance()->addChangeListener([self](const DocumentManager::ChangeNotification& change) {
        if (change.dbType != TypeID::CAMERA_DB && change.dbType != TypeID::WINDOW_DB &&
            change.dbType != TypeID::TOOLPATH_PREVIEW_DB)
            return;
        if (self)
            QMetaObject::invokeMethod(self, [self] { if (self) self->requestRefresh(); }, Qt::QueuedConnection);
    });
}
ToolpathInspectHandler::~ToolpathInspectHandler() {
    if (auto* app = QCoreApplication::instance()) app->removeEventFilter(this);
    leave();
    DocumentManager::instance()->removeChangeListener(m_listener);
}
void ToolpathInspectHandler::syncEnabled() {
    if (!isActive() || !SlicingPreviewBridge::instance()->previewPanelVisible()) {
        leave();
        return;
    }
    setModifiers(m_modifiers);
    if (canQuery()) requestRefresh(); else clearSnap();
}
void ToolpathInspectHandler::onEnter(std::shared_ptr<ActionContext>) {
    setActive(true);
    syncEnabled();
}
void ToolpathInspectHandler::onExit() {
    setActive(false);
    syncEnabled();
    leave();
}
void ToolpathInspectHandler::onSuspend() { onExit(); }
void ToolpathInspectHandler::onResume() {
    setActive(true);
    syncEnabled();
}
bool ToolpathInspectHandler::onKeyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Alt && !event->isAutoRepeat()) {
        auto* bridge = SlicingPreviewBridge::instance();
        const auto preview = bridge->currentToolpathPreview();
        LOG_INFO("Preview snap Option/Alt pressed: view={} active={} previewMode={} previewDb={}",
                 inputViewId(), isActive(), bridge->previewPanelVisible(),
                 preview ? preview->getDBInstanceID().getValue() : 0);
    }
    handleKey(*event);
    return false;
}
bool ToolpathInspectHandler::onKeyReleaseEvent(QKeyEvent* event) {
    handleKey(*event);
    return false;
}
bool ToolpathInspectHandler::onMouseMoveEvent(QMouseEvent* event) {
    const DBInstanceID viewId(inputViewId());
    if (viewId != m_viewId) {
        clearSnap();
        m_updatesAllowed = true;
    }
    if (!m_inside || m_position != event->position()) m_updatesAllowed = true;
    m_viewId = viewId;
    m_position = event->position();
    setModifiers(event->modifiers());
    m_inside = true;
    m_dragging = event->buttons() != Qt::NoButton;
    if (canQuery()) requestRefresh(); else clearSnap();
    return false;
}
bool ToolpathInspectHandler::onMousePressEvent(QMouseEvent* event) { return onMouseMoveEvent(event); }
bool ToolpathInspectHandler::onMouseReleaseEvent(QMouseEvent* event) { return onMouseMoveEvent(event); }

bool ToolpathInspectHandler::canQuery() const {
    return isActive() && SlicingPreviewBridge::instance()->previewPanelVisible() &&
           m_inside && m_updatesAllowed && !m_dragging && m_modifiers.testFlag(Qt::AltModifier);
}
void ToolpathInspectHandler::requestRefresh() {
    if (canQuery() && !m_refresh.isActive()) m_refresh.start();
}
void ToolpathInspectHandler::clearSnap() {
    m_refresh.stop();
    m_snapResult.reset();
    m_snapPreview.clear();
}
void ToolpathInspectHandler::leaveViewport() {
    // Moving to the toolbar ends the hover, but the held key still exposes its
    // checkbox. Releasing the key or leaving the window ends that state.
    m_inside = false;
    m_dragging = false;
    clearSnap();
}
void ToolpathInspectHandler::leave() {
    leaveViewport();
    setModifiers(Qt::NoModifier);
    m_pointerWindow.clear();
}
void ToolpathInspectHandler::setModifiers(Qt::KeyboardModifiers modifiers) {
    m_modifiers = modifiers;
    auto* bridge = SlicingPreviewBridge::instance();
    bridge->setPreviewInspectModifierPressed(isActive() && bridge->previewPanelVisible() &&
                                            modifiers.testFlag(Qt::AltModifier));
}
void ToolpathInspectHandler::handleKey(const QKeyEvent& event) {
    if (!isActive() || !SlicingPreviewBridge::instance()->previewPanelVisible() || event.isAutoRepeat()) return;
    auto modifiers = event.modifiers();
    if (event.key() == Qt::Key_Alt) {
        if (event.type() == QEvent::KeyPress) {
            modifiers |= Qt::AltModifier; // Option on macOS.
            m_updatesAllowed = true;
        } else {
            modifiers &= ~Qt::KeyboardModifiers(Qt::AltModifier);
        }
    }
    setModifiers(modifiers);
    if (!m_inside || DBInstanceID(inputViewId()) != m_viewId) return;
    if (canQuery()) refresh(); else clearSnap();
}
bool ToolpathInspectHandler::eventFilter(QObject* watched, QEvent* event) {
    if (!isActive() || !SlicingPreviewBridge::instance()->previewPanelVisible()) return false;
    // Key state belongs to the preview window, including its toolbar controls.
    // Queries still require routed viewport input and m_inside.
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (!key->isAutoRepeat() && key->key() == Qt::Key_Alt) {
            auto modifiers = key->modifiers();
            if (event->type() == QEvent::KeyPress) {
                modifiers |= Qt::AltModifier;
                m_updatesAllowed = true;
            } else {
                modifiers &= ~Qt::KeyboardModifiers(Qt::AltModifier);
            }
            setModifiers(modifiers);
            if (!canQuery()) clearSnap();
        }
    } else if (event->type() == QEvent::MouseMove) {
        if (auto* window = qobject_cast<QWindow*>(watched)) m_pointerWindow = window;
    } else if (m_inside && event->type() == QEvent::MouseButtonPress) {
        // Camera/tools can consume presses before this background handler.
        m_dragging = true;
        clearSnap();
    } else if (event->type() == QEvent::WindowDeactivate) {
        leave();
    } else if (m_inside && event->type() == QEvent::FocusOut) {
        leaveViewport();
    }
    return false;
}
void ToolpathInspectHandler::refresh() {
    m_refresh.stop();
    if (!canQuery()) { clearSnap(); return; }
    const auto view = m_viewId;
    auto context = snapContext(view);
    if (!canQuery() || m_viewId != view) return;
    if (!context || !context->query.projection.isValid()) {
        clearSnap();
    } else {
        context->query.position = m_position;
        m_snapResult = interactionSnapService().query(context->query, context->targets, m_snapResult);
        if (!m_pointerWindow) m_pointerWindow = QGuiApplication::focusWindow();
        m_snapPreview.show(view, context->query, context->targets, m_snapResult, m_pointerWindow);
    }
    inspectResult();
}

std::optional<ToolpathInspectHandler::SnapContext> ToolpathInspectHandler::snapContext(DBInstanceID viewId) {
    auto* bridge = SlicingPreviewBridge::instance();
    auto preview = bridge->currentToolpathPreview();
    if (preview && DocumentManager::instance()->getDBInstance(preview->getDBInstanceID()) != preview) preview.reset();
    bridge->gcodeLines()->setPreview(preview);
    if (!preview || !preview->isVisible() || !bridge->previewPanelVisible()) return std::nullopt;
    const auto role = interactionPickService().roleForView(viewId);
    if (role != GPlatform::Rendering::RendererRole::Preview && role != GPlatform::Rendering::RendererRole::Compatibility)
        return std::nullopt;
    auto window = std::dynamic_pointer_cast<WindowDB>(DocumentManager::instance()->getDBInstance(viewId));
    if (!window || !window->getCoordinateSystem()) return std::nullopt;
    SnapContext context;
    context.query.projection = window->getCoordinateSystem()->projectionSnapshot();
    context.query.preferPoints = true;
    GPlatform::Interaction::SnapTarget target;
    target.db = preview;
    target.localToWorld = preview->getTransformMatrix();
    target.eligible = [preview, visibility = preview->inspectionVisibility()](std::size_t index, const SnapFeature&) {
        return preview->inspectionElementVisible(index, visibility);
    };
    context.targets.push_back(std::move(target));
    return context;
}
void ToolpathInspectHandler::inspectResult() {
    auto* bridge = SlicingPreviewBridge::instance();
    auto* model = bridge->gcodeLines();
    const auto preview = bridge->currentToolpathPreview();
    const auto& hit = m_snapResult;
    if (!hit || !preview || hit->sourceId != preview->getDBInstanceID() || hit->geometry != preview->snapGeometry()) {
        model->clearInspection();
        return;
    }
    const auto& element = preview->inspectionElements().at(hit->featureIndex);
    using Kind = GPlatform::ToolpathPreviewDB::InspectionElement::Kind;
    QString label;
    if (element.kind == Kind::Segment || element.kind == Kind::Endpoint) {
        const auto& segment = preview->getSegments()[element.index];
        label = tr("%1 · Layer %2 · G-code line %3")
                    .arg(element.kind == Kind::Endpoint ? (element.subIndex ? tr("Command end") : tr("Command start"))
                                                       : tr("Command path"))
                    .arg(segment.layerId)
                    .arg(element.sourceLine);
    } else if (element.kind == Kind::Event) {
        label = tr("Seam point · Associated G-code line %1").arg(element.sourceLine);
    } else {
        const auto& diagnostic = preview->getFiberFillDiagnostics()[element.index];
        const auto type = diagnostic.kind == GPlatform::FiberDiagnosticKind::ContourCandidate
            ? tr("原始轮廓候选") : diagnostic.kind == GPlatform::FiberDiagnosticKind::RoundedContourCandidate
                ? tr("圆角后轮廓候选") : diagnostic.kind == GPlatform::FiberDiagnosticKind::OriginalContourRegion
            ? tr("原始轮廓区域") : diagnostic.kind == GPlatform::FiberDiagnosticKind::MissingContourRegion
                ? tr("候选轮廓未生成区域") : tr("被拒绝的纤维路径");
        label = tr("%1 · 无 G-code 命令 · 区域 %2/%3 · %4")
                    .arg(type).arg(diagnostic.policyGroupId).arg(diagnostic.componentId)
                    .arg(QString::fromStdString(diagnostic.reason));
    }
    model->inspect(element.sourceLine, label);
}
