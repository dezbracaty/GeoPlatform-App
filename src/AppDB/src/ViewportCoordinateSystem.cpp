#include "ViewportCoordinateSystem.hpp"
#include "CoordinateTransform.hpp"
#include <CameraDB.hpp>
#include "Foundation/Log.h"

namespace {

float viewportAspectRatio(const Vector2& viewportSize, float fallback) {
    if (viewportSize.y > 1e-6f) {
        return viewportSize.x / viewportSize.y;
    }
    return fallback > 1e-6f ? fallback : 1.0f;
}

} // namespace

bool ViewportProjectionSnapshot::isValid() const noexcept {
    return m_viewMatrix.size() == 16 && m_projectionMatrix.size() == 16 &&
           m_physicalViewportSize.x > 0.0f && m_physicalViewportSize.y > 0.0f &&
           m_devicePixelRatio > 0.0;
}

Vector4 ViewportProjectionSnapshot::clipFromWorld(const Vector3& p) const {
    if (!isValid()) return {};
    const auto multiply = [](const std::vector<float>& m, const Vector4& v) {
        return Vector4(m[0]*v.x+m[1]*v.y+m[2]*v.z+m[3]*v.w,
                       m[4]*v.x+m[5]*v.y+m[6]*v.z+m[7]*v.w,
                       m[8]*v.x+m[9]*v.y+m[10]*v.z+m[11]*v.w,
                       m[12]*v.x+m[13]*v.y+m[14]*v.z+m[15]*v.w);
    };
    return multiply(m_projectionMatrix, multiply(m_viewMatrix, Vector4(p.x,p.y,p.z,1)));
}

Vector2 ViewportProjectionSnapshot::logicalSize() const {
    return isValid() ? Vector2(m_physicalViewportSize.x / m_devicePixelRatio,
                              m_physicalViewportSize.y / m_devicePixelRatio) : Vector2{};
}

Vector3 ViewportProjectionSnapshot::worldFromScreen(const QPointF& logicalScreenPos,
                                                    float depth) const {
    if (!isValid()) return Vector3(0, 0, 0);
    const QPointF physicalScreenPos(
        logicalScreenPos.x() * m_devicePixelRatio,
        logicalScreenPos.y() * m_devicePixelRatio);
    return CoordinateTransform::screenToWorld(
        static_cast<float>(physicalScreenPos.x()),
        static_cast<float>(physicalScreenPos.y()),
        depth,
        static_cast<int>(m_physicalViewportSize.x),
        static_cast<int>(m_physicalViewportSize.y),
        m_viewMatrix,
        m_projectionMatrix);
}

Vector3 ViewportProjectionSnapshot::screenFromWorld(const Vector3& worldPos) const {
    if (!isValid()) return Vector3(0, 0, 0);
    const Vector3 physical = CoordinateTransform::worldToScreen(
        worldPos,
        static_cast<int>(m_physicalViewportSize.x),
        static_cast<int>(m_physicalViewportSize.y),
        m_viewMatrix,
        m_projectionMatrix);
    return Vector3(
        static_cast<float>(physical.x / m_devicePixelRatio),
        static_cast<float>(physical.y / m_devicePixelRatio),
        physical.z);
}

WorldRay ViewportProjectionSnapshot::rayFromScreen(const QPointF& logicalScreenPos) const {
    WorldRay ray{{0, 0, 0}, {0, 0, -1}};
    if (!isValid()) return ray;
    const QPointF physicalScreenPos(
        logicalScreenPos.x() * m_devicePixelRatio,
        logicalScreenPos.y() * m_devicePixelRatio);
    CoordinateTransform::screenToRay(
        static_cast<float>(physicalScreenPos.x()),
        static_cast<float>(physicalScreenPos.y()),
        static_cast<int>(m_physicalViewportSize.x),
        static_cast<int>(m_physicalViewportSize.y),
        m_viewMatrix,
        m_projectionMatrix,
        ray.origin,
        ray.direction);
    return ray;
}

Vector3 ViewportProjectionSnapshot::worldDeltaFromScreenDrag(
    const QPointF& startScreen,
    const QPointF& currentScreen,
    const Vector3& anchorWorld) const {
    if (!isValid()) return Vector3(0, 0, 0);
    const Vector3 anchorScreen = screenFromWorld(anchorWorld);
    const QPointF delta = currentScreen - startScreen;
    const Vector3 movedWorld = worldFromScreen(
        QPointF(anchorScreen.x + delta.x(), anchorScreen.y + delta.y()),
        anchorScreen.z);
    return movedWorld - anchorWorld;
}

ViewportCoordinateSystem::ViewportCoordinateSystem()
    : m_logicalViewportSize(800.0f, 600.0f)
    , m_devicePixelRatio(1.0)
{
    LOG_DEBUG("ViewportCoordinateSystem created");
}

ViewportCoordinateSystem::~ViewportCoordinateSystem() {
    LOG_DEBUG("ViewportCoordinateSystem destroyed");
}

// ===== 基础属性管理 =====

void ViewportCoordinateSystem::setLogicalViewportSize(const Vector2& size) {
    if (size.x != m_logicalViewportSize.x || size.y != m_logicalViewportSize.y) {
        LOG_DEBUG("ViewportCoordinateSystem: Logical viewport size changed from ({}, {}) to ({}, {})",
                 m_logicalViewportSize.x, m_logicalViewportSize.y, size.x, size.y);
        m_logicalViewportSize = size;
    }
}

void ViewportCoordinateSystem::setDevicePixelRatio(qreal dpr) {
    if (!qFuzzyCompare(m_devicePixelRatio, dpr)) {
        LOG_DEBUG("ViewportCoordinateSystem: DPR changed from {} to {}", m_devicePixelRatio, dpr);
        m_devicePixelRatio = dpr;
    }
}

void ViewportCoordinateSystem::setActiveCamera(std::weak_ptr<CameraDB> camera) {
    m_activeCamera = camera;
    LOG_DEBUG("ViewportCoordinateSystem: Active camera updated");
}

// ===== 尺寸查询 =====

Vector2 ViewportCoordinateSystem::getLogicalViewportSize() const {
    return m_logicalViewportSize;
}

Vector2 ViewportCoordinateSystem::physicalViewportSize() const {
    return Vector2(m_logicalViewportSize.x * m_devicePixelRatio,
                  m_logicalViewportSize.y * m_devicePixelRatio);
}

ViewportProjectionSnapshot ViewportCoordinateSystem::projectionSnapshot() const {
    ViewportProjectionSnapshot snapshot;
    if (!getCameraMatrices(snapshot.m_viewMatrix, snapshot.m_projectionMatrix)) {
        return snapshot;
    }
    snapshot.m_physicalViewportSize = physicalViewportSize();
    snapshot.m_devicePixelRatio = m_devicePixelRatio;
    return snapshot;
}

QPointF ViewportCoordinateSystem::toPhysical(const QPointF& logicalPoint) const {
    return QPointF(logicalPoint.x() * m_devicePixelRatio,
                   logicalPoint.y() * m_devicePixelRatio);
}

// ===== 语义坐标转换 =====

WorldRay ViewportCoordinateSystem::rayFromScreen(const QPointF& screenPos) const {
    WorldRay ray;
    screenToRay(screenPos, ray.origin, ray.direction);
    return ray;
}

Vector3 ViewportCoordinateSystem::worldFromScreen(const QPointF& screenPos, float depth) const {
    return screenToWorld(screenPos, depth);
}

Vector3 ViewportCoordinateSystem::screenFromWorld(const Vector3& worldPos) const {
    return worldToScreen(worldPos);
}

Vector3 ViewportCoordinateSystem::worldDeltaFromScreenDrag(const QPointF& startScreen,
                                                          const QPointF& currentScreen,
                                                          const Vector3& anchorWorld) const {
    return screenDeltaToWorldDelta(startScreen, currentScreen, anchorWorld);
}

QPointF ViewportCoordinateSystem::gestureDelta(const QPointF& startScreen,
                                               const QPointF& currentScreen) const {
    return currentScreen - startScreen;
}

// ===== 兼容坐标转换 =====

Vector3 ViewportCoordinateSystem::screenToWorld(const QPointF& screenPos, float depth) const {
    std::vector<float> viewMatrix, projectionMatrix;
    if (!getCameraMatrices(viewMatrix, projectionMatrix)) {
        LOG_ERROR("ViewportCoordinateSystem::screenToWorld - Failed to get camera matrices");
        return Vector3(0, 0, 0);
    }

    Vector2 physicalSize = physicalViewportSize();
    const QPointF physicalScreen = toPhysical(screenPos);
    return CoordinateTransform::screenToWorld(
        static_cast<float>(physicalScreen.x()),
        static_cast<float>(physicalScreen.y()),
        depth,
        static_cast<int>(physicalSize.x),
        static_cast<int>(physicalSize.y),
        viewMatrix,
        projectionMatrix
    );
}

void ViewportCoordinateSystem::screenToRay(const QPointF& screenPos,
                                          Vector3& rayOrigin,
                                          Vector3& rayDirection) const {
    std::vector<float> viewMatrix, projectionMatrix;
    if (!getCameraMatrices(viewMatrix, projectionMatrix)) {
        LOG_ERROR("ViewportCoordinateSystem::screenToRay - Failed to get camera matrices");
        rayOrigin = Vector3(0, 0, 0);
        rayDirection = Vector3(0, 0, -1);
        return;
    }

    Vector2 physicalSize = physicalViewportSize();
    const QPointF physicalScreen = toPhysical(screenPos);
    CoordinateTransform::screenToRay(
        static_cast<float>(physicalScreen.x()),
        static_cast<float>(physicalScreen.y()),
        static_cast<int>(physicalSize.x),
        static_cast<int>(physicalSize.y),
        viewMatrix,
        projectionMatrix,
        rayOrigin,
        rayDirection
    );
}

Vector3 ViewportCoordinateSystem::worldToScreen(const Vector3& worldPos) const {
    std::vector<float> viewMatrix, projectionMatrix;
    if (!getCameraMatrices(viewMatrix, projectionMatrix)) {
        LOG_ERROR("ViewportCoordinateSystem::worldToScreen - Failed to get camera matrices");
        return Vector3(0, 0, 0);
    }

    Vector2 physicalSize = physicalViewportSize();
    const Vector3 physical = CoordinateTransform::worldToScreen(
        worldPos,
        static_cast<int>(physicalSize.x),
        static_cast<int>(physicalSize.y),
        viewMatrix,
        projectionMatrix);
    return Vector3(
        static_cast<float>(physical.x / m_devicePixelRatio),
        static_cast<float>(physical.y / m_devicePixelRatio),
        physical.z);
}

Vector3 ViewportCoordinateSystem::screenDeltaToWorldDelta(const QPointF& startScreen,
                                                         const QPointF& currentScreen,
                                                         const Vector3& objectWorldPos) const {
    std::vector<float> viewMatrix, projectionMatrix;
    if (!getCameraMatrices(viewMatrix, projectionMatrix)) {
        LOG_ERROR("ViewportCoordinateSystem::screenDeltaToWorldDelta - Failed to get camera matrices");
        return Vector3(0, 0, 0);
    }

    // 计算屏幕坐标差值
    Vector2 screenDelta(
        static_cast<float>((currentScreen.x() - startScreen.x()) * m_devicePixelRatio),
        static_cast<float>((currentScreen.y() - startScreen.y()) * m_devicePixelRatio)
    );

    Vector2 physicalSize = physicalViewportSize();
    return CoordinateTransform::screenDeltaToWorldDelta(
        screenDelta,
        objectWorldPos,
        static_cast<int>(physicalSize.x),
        static_cast<int>(physicalSize.y),
        viewMatrix,
        projectionMatrix
    );
}

// ===== 私有辅助方法 =====

bool ViewportCoordinateSystem::getCameraMatrices(std::vector<float>& viewMatrix,
                                                std::vector<float>& projectionMatrix) const {
    auto camera = m_activeCamera.lock();
    if (!camera) {
        LOG_ERROR("ViewportCoordinateSystem: No active camera available");
        return false;
    }

    viewMatrix = camera->calculateViewMatrix();
    // VTK derives its projection aspect from the renderer viewport. CameraDB's
    // persisted AspectRatio may still be the 1:1 default, so using it here makes
    // handler rays diverge from VTK Pick as soon as the viewport is not square.
    // Keep this calculation window-local for correct multi-window behavior.
    const float aspectRatio = viewportAspectRatio(physicalViewportSize(),
                                                  camera->getAspectRatio());
    projectionMatrix = camera->calculateProjectionMatrix(aspectRatio);
    return true;
}
