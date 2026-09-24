#include "PanRotateBGHandler.hpp"
#include "QuickActionController.hpp"
#include <QGuiApplication>
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(PanRotateBGHandler, "camera.panRotate")
#include "ActionContext.hpp"
#include "ActionHandlerRegistry.hpp"
#include <CameraDB.hpp>
#include <CameraNavigationController.hpp>
#include <DocumentManager.hpp>
#include <ViewportCoordinateSystem.hpp>
#include <WindowDB.hpp>
#include <WindowDBManager.hpp>
#include <QMouseEvent>
#include <QWheelEvent>
#include "Foundation/Log.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace {
    constexpr qreal kDragThreshold = 4.0;
    constexpr float kTrackpadPixelsPerScrollUnit = 40.0f;

    bool exceedsDragThreshold(const QPointF& start, const QPointF& current) {
        const QPointF delta = current - start;
        return delta.x() * delta.x() + delta.y() * delta.y() >=
            kDragThreshold * kDragThreshold;
    }

    float scrollUnits(const QWheelEvent& event) {
        if (!event.pixelDelta().isNull()) {
            return static_cast<float>(event.pixelDelta().y()) /
                kTrackpadPixelsPerScrollUnit;
        }
        return static_cast<float>(event.angleDelta().y()) / 120.0f;
    }

    std::shared_ptr<WindowDB> window_for_view(std::uint64_t viewId) {
        auto* docManager = DocumentManager::instance();
        if (!docManager) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<WindowDB>(
            docManager->getDBInstance(DBInstanceID(viewId)));
    }

    std::shared_ptr<CameraDB> camera_for_view(std::uint64_t viewId) {
        const auto window = window_for_view(viewId);
        return window ? window->getCamera() : nullptr;
    }

    std::shared_ptr<ViewportCoordinateSystem> coordinate_system_for_view(
        std::uint64_t viewId) {
        const auto window = window_for_view(viewId);
        if (!window) {
            return nullptr;
        }
        window->updateActiveCamera();
        return window->getCoordinateSystem();
    }
}

struct PanRotateBGHandler::GestureState {
    QPointF pointerStart;
    bool dragged{false};
    bool contextClick{false};
    std::optional<CameraOrbitGesture> orbit;
    ViewportProjectionSnapshot panProjection;
    Vector3 panAnchor;
    Vector3 appliedPan;
};

PanRotateBGHandler::PanRotateBGHandler(QObject* parent)
    : IActionHandlerBase(parent) {

    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) endCurrentOperation();
        });
    }

    // Background handlers should be active by default
    setActive(true);
    LOG_DEBUG("PanRotateBGHandler created and activated as background handler");
}

PanRotateBGHandler::~PanRotateBGHandler() = default;

void PanRotateBGHandler::onEnter(std::shared_ptr<ActionContext> context) {
    Q_UNUSED(context)
    setActive(true);
    LOG_DEBUG("PanRotateBGHandler activated (Background mode)");
}

void PanRotateBGHandler::onExit() {
    endCurrentOperation();
    setActive(false);
    LOG_DEBUG("PanRotateBGHandler deactivated");
}

// === 事件处理方法实现 ===

bool PanRotateBGHandler::onMousePressEvent(QMouseEvent* event) {
    if (!isActive() || !event ||
        (event->button() != Qt::LeftButton &&
         event->button() != Qt::MiddleButton &&
         event->button() != Qt::RightButton)) {
        return false;
    }

    if (m_mousePressed) {
        if (inputViewId() == m_cameraViewId && event->button() != m_gestureButton &&
            event->buttons().testFlag(m_gestureButton)) {
            // Keep the original drag, but a button chord must not open a menu.
            if (m_gesture) m_gesture->contextClick = false;
            return false;
        }
        // A fresh press replaces a gesture whose release was not delivered.
        endCurrentOperation();
    }

    m_cameraViewId = inputViewId();
    m_inputCamera = camera_for_view(m_cameraViewId);
    if (!m_inputCamera) {
        LOG_DEBUG("PanRotateBGHandler: No camera found in DocumentManager");
        return false;
    }

    m_mousePressed = true;
    m_gesture = std::make_unique<GestureState>();
    m_gesture->pointerStart = event->position();
    m_gesture->contextClick = event->button() == Qt::RightButton &&
        event->buttons() == Qt::RightButton;

    // 记录按键类型，但不立即开始操作 - 等待拖拽
    m_pendingButton = event->button();
    m_gestureButton = event->button();

    // Model interaction is dispatched before background camera handling. Once
    // this fallback records the press, it owns the camera gesture.
    return true;
}

bool PanRotateBGHandler::onMouseMoveEvent(QMouseEvent* event) {
    if (!isActive() || !event || !m_mousePressed) {
        return false;
    }

    if (inputViewId() != m_cameraViewId || !m_inputCamera) {
        return false;
    }

    if (!event->buttons().testFlag(m_gestureButton)) {
        endCurrentOperation();
        return false;
    }

    // 如果还没有开始操作且有pending button，现在开始拖拽
    if (m_currentMode == InteractionMode::NONE && m_pendingButton != Qt::NoButton) {
        if (!m_gesture || !exceedsDragThreshold(
                m_gesture->pointerStart, event->position())) {
            return true;
        }
        m_gesture->dragged = true;
        if (m_pendingButton == Qt::LeftButton) {
            startRotation();
        } else if (m_pendingButton == Qt::MiddleButton ||
                   m_pendingButton == Qt::RightButton) {
            startPanning();
        }
        m_pendingButton = Qt::NoButton; // 清除pending状态
    }

    // 根据当前模式处理移动
    switch (m_currentMode) {
        case InteractionMode::ROTATING:
            updateRotation(event->position());
            return true; // 只在实际执行旋转时消费事件

        case InteractionMode::PANNING:
            updatePanning(event->position());
            return true; // 只在实际执行平移时消费事件

        default:
            return true;
    }
}

bool PanRotateBGHandler::onMouseReleaseEvent(QMouseEvent* event) {
    if (!isActive() || !event || !m_mousePressed ||
        inputViewId() != m_cameraViewId || event->button() != m_gestureButton) {
        return false;
    }

    const bool showMenu = m_gesture && m_gesture->contextClick &&
        !m_gesture->dragged && event->buttons() == Qt::NoButton &&
        !exceedsDragThreshold(m_gesture->pointerStart, event->position());
    endCurrentOperation();
    if (showMenu) {
        QuickActionController::getInstance()->handleRightClick(event->position());
    }
    return true;
}

bool PanRotateBGHandler::onWheelEvent(QWheelEvent* event) {
    if (!isActive() || !event) {
        return false;
    }

    // Do not mix trackpad scrolling with an armed camera drag.
    if (m_mousePressed && inputViewId() == m_cameraViewId) {
        return true;
    }

    const auto camera = camera_for_view(inputViewId());
    if (!camera) {
        return false;
    }

    return CameraNavigationController::zoomFromScroll(
        scrollUnits(*event), m_zoomSensitivity, camera);
}

void PanRotateBGHandler::startRotation() {
    auto camera = m_inputCamera;
    if (!camera || !camera->getOrbitMode() || !m_gesture) {
        m_currentMode = InteractionMode::NONE;
        return;
    }

    const auto coordinateSystem = coordinate_system_for_view(m_cameraViewId);
    if (!coordinateSystem) {
        LOG_ERROR("PanRotateBGHandler::startRotation - ViewportCoordinateSystem unavailable");
        m_currentMode = InteractionMode::NONE;
        return;
    }

    m_gesture->orbit = CameraNavigationController::beginOrbitGesture(
        m_gesture->pointerStart,
        coordinateSystem->getLogicalViewportSize(),
        camera);
    if (!m_gesture->orbit) {
        m_currentMode = InteractionMode::NONE;
        return;
    }
    m_currentMode = InteractionMode::ROTATING;
}

void PanRotateBGHandler::updateRotation(const QPointF& currentPos) {
    auto camera = m_inputCamera;
    if (!camera || !camera->getOrbitMode()) {
        return;
    }

    if (!m_gesture || !m_gesture->orbit) {
        LOG_ERROR("PanRotateBGHandler::updateRotation - Orbit gesture unavailable");
        return;
    }
    CameraNavigationController::orbitFromScreenDrag(
        *m_gesture->orbit, currentPos, m_rotationSensitivity, camera);
}

void PanRotateBGHandler::startPanning() {
    if (!m_gesture || !m_inputCamera) {
        m_currentMode = InteractionMode::NONE;
        return;
    }
    const auto coordinateSystem = coordinate_system_for_view(m_cameraViewId);
    if (!coordinateSystem) {
        LOG_ERROR("PanRotateBGHandler::startPanning - ViewportCoordinateSystem unavailable");
        m_currentMode = InteractionMode::NONE;
        return;
    }

    m_gesture->panProjection = coordinateSystem->projectionSnapshot();
    if (!m_gesture->panProjection.isValid()) {
        LOG_ERROR("PanRotateBGHandler::startPanning - Projection snapshot unavailable");
        m_currentMode = InteractionMode::NONE;
        return;
    }
    m_gesture->panAnchor = m_inputCamera->getOrbitMode()
        ? m_inputCamera->getOrbitCenter()
        : m_inputCamera->getTarget();
    m_gesture->appliedPan = Vector3(0, 0, 0);
    m_currentMode = InteractionMode::PANNING;
}

void PanRotateBGHandler::updatePanning(const QPointF& currentPos) {
    auto camera = m_inputCamera;
    if (!camera) {
        return;
    }

    if (!m_gesture || !m_gesture->panProjection.isValid()) {
        LOG_ERROR("PanRotateBGHandler::updatePanning - Pan gesture unavailable");
        return;
    }

    const Vector3 projected =
        m_gesture->panProjection.worldDeltaFromScreenDrag(
            m_gesture->pointerStart, currentPos, m_gesture->panAnchor);
    const float panGain = std::clamp(m_panSensitivity, 0.05f, 8.0f);
    const Vector3 desiredPan = projected * (-panGain);
    const Vector3 incrementalPan = desiredPan - m_gesture->appliedPan;
    if (CameraNavigationController::pan(incrementalPan, camera)) {
        m_gesture->appliedPan = desiredPan;
    }
}

void PanRotateBGHandler::endCurrentOperation() {
    if (m_currentMode != InteractionMode::NONE) {
        LOG_DEBUG("Ended camera operation: {}", static_cast<int>(m_currentMode));
    }

    m_currentMode = InteractionMode::NONE;
    m_mousePressed = false;
    m_pendingButton = Qt::NoButton; // 清除pending状态
    m_gestureButton = Qt::NoButton;
    m_inputCamera.reset();
    m_gesture.reset();
    m_cameraViewId = 0;
}

// ============================================================================
// Action 注册
// ============================================================================
