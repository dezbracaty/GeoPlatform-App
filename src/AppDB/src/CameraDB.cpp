#include "CameraDB.hpp"
#include "SceneBounds.hpp"
#include <DocumentManager.hpp>
#include <TransactionManager.hpp>
#include <ChangeTypes.hpp>
#include "Foundation/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
constexpr auto kInteractiveClippingUpdateInterval = std::chrono::milliseconds(120);
const Vector3 kOrcaDefaultViewUpVector(-0.21850801f, 0.67249851f, 0.70710678f);
constexpr float kOrcaDefaultHorizontalAngle = static_cast<float>(-M_PI * 0.4);
constexpr float kOrbitPoleMargin = static_cast<float>(M_PI / 180.0); // 1 degree
constexpr float kMinOrbitVerticalAngle = static_cast<float>(-M_PI * 0.5) + kOrbitPoleMargin;
constexpr float kMaxOrbitVerticalAngle = static_cast<float>(M_PI * 0.5) - kOrbitPoleMargin;

Vector3 orbitUpVector(float horizontalAngle, float verticalAngle)
{
    return Vector3(
        -std::sin(verticalAngle) * std::cos(horizontalAngle),
        -std::sin(verticalAngle) * std::sin(horizontalAngle),
        std::cos(verticalAngle));
}
}

CameraDB::CameraDB()
    : AutoRegisterDB() {
    // 构造函数保持简单，所有初始化移到 onCreated() 中
}

CameraDB::~CameraDB() = default;

// === 实现 AutoRegisterDB 的钩子函数 ===

void CameraDB::initializeProperties() {
    // 初始化所有相机属性的默认值
    // 不依赖静态默认值机制，而是在这里显式设置

    // Match the default OrcaSlicer screenshot view provided by the user.
    setPosition(Vector3(-94.0f, -289.0f, 304.0f));
    setTarget(Vector3(0, 0, 0));      // 默认看向原点
    setUpVector(kOrcaDefaultViewUpVector);

    // 初始化投影参数
    setProjectionType(0);          // 默认透视投影
    setFieldOfView(45.0f);         // 默认45度视野
    setOrthographicSize(10.0f);    // 正交投影大小
    setAspectRatio(1.0f);          // 默认宽高比1:1
    setNearClippingPlane(0.1f);    // 近裁剪面
    setFarClippingPlane(10000.0f); // 远裁剪面 - 增加到10000以支持远距离查看

    // 初始化轨道相机模式（Z-up坐标系）
    setOrbitMode(true);               // 默认启用轨道模式
    setOrbitCenter(Vector3(0, 0, 0)); // 轨道中心在原点
    setOrbitRadius(430.0f);
    setHorizontalAngle(kOrcaDefaultHorizontalAngle);
    setVerticalAngle(M_PI * 0.25f);

    // 初始化相机状态
    setEnabled(true);  // 默认启用
    setName("Camera"); // 默认名称
}

void CameraDB::afterPropertiesInitialized() {
    // 如果启用了轨道模式，根据轨道参数更新位置
    if (getOrbitMode()) {
        updatePositionFromOrbit();
    }

    // FIELD_VALUE callbacks are installed below, so initialization does not
    // publish itself through handlePropertyChanged(). Publish the first
    // complete state explicitly.
    publishRenderState();

    // 设置属性变化回调，用于实时通知变化（不在事务中）
    // 使用 weak_ptr 避免循环引用
    std::weak_ptr<CameraDB> weakThis =
        std::dynamic_pointer_cast<CameraDB>(shared_from_this());

    auto notifyChange = [weakThis](const std::string& propertyName, bool updateClippingPlanes = false) {
        if (auto self = weakThis.lock()) {
            self->handlePropertyChanged(propertyName, updateClippingPlanes);
        }
    };

    // 注册所有属性的变化回调
    // 注意：属性名必须与 FIELD_VALUE 宏中的名称一致（首字母大写）
    onPositionChanged = [notifyChange](const Vector3&) { notifyChange("Position"); };
    onTargetChanged = [notifyChange](const Vector3&) { notifyChange("Target"); };
    onUpVectorChanged = [notifyChange](const Vector3&) { notifyChange("UpVector"); };
    onOrbitModeChanged = [notifyChange](bool) { notifyChange("OrbitMode"); };
    onOrbitCenterChanged = [notifyChange](const Vector3&) { notifyChange("OrbitCenter"); };
    onHorizontalAngleChanged = [notifyChange](float) { notifyChange("HorizontalAngle"); };
    onVerticalAngleChanged = [notifyChange](float) { notifyChange("VerticalAngle"); };
    onOrbitRadiusChanged = [notifyChange](float) { notifyChange("OrbitRadius", true); };
    onFieldOfViewChanged = [notifyChange](float) { notifyChange("FieldOfView"); };
    onNearClippingPlaneChanged = [notifyChange](float) { notifyChange("NearClippingPlane"); };
    onFarClippingPlaneChanged = [notifyChange](float) { notifyChange("FarClippingPlane"); };
    onProjectionTypeChanged = [notifyChange](int) { notifyChange("ProjectionType"); };
    onOrthographicSizeChanged = [notifyChange](float) { notifyChange("OrthographicSize"); };
}

// === 轨道相机接口实现 ===

void CameraDB::rotate(float deltaHorizontal, float deltaVertical) {
    if (!getOrbitMode())
        return;

    // Camera rotation is a view operation, not a data modification - no transaction needed

    // 更新角度
    float newHorizontal = getHorizontalAngle() + deltaHorizontal;
    float newVertical = getVerticalAngle() + deltaVertical;

    // 限制垂直角度范围 [-π/2, π/2]，在Z-up坐标系中严格限制
    newVertical = std::clamp(newVertical, kMinOrbitVerticalAngle, kMaxOrbitVerticalAngle);

    beginCameraBatch();
    setHorizontalAngle(newHorizontal);
    setVerticalAngle(newVertical);

    updatePositionFromOrbit();
    endCameraBatch();
}

void CameraDB::zoom(float scaleFactor) {
    if (!getOrbitMode())
        return;

    // Camera zoom is a view operation, not a data modification - no transaction needed

    float newRadius = getOrbitRadius() * scaleFactor;
    // 限制半径范围
    newRadius = std::clamp(newRadius, 0.1f, 1000.0f);

    beginCameraBatch();
    setOrbitRadius(newRadius);
    updatePositionFromOrbit();
    endCameraBatch();
}

void CameraDB::pan(const Vector3& delta) {
    // 注意：这个方法在拖拽过程中频繁调用，事务管理应该在调用方处理
    if (getOrbitMode()) {
        beginCameraBatch();
        // 在轨道模式下，平移轨道中心
        Vector3 newCenter = getOrbitCenter() + delta;
        setOrbitCenter(newCenter);
        updatePositionFromOrbit();
        endCameraBatch();
    } else {
        beginCameraBatch();
        // 在自由模式下，同时平移相机位置和目标
        setPosition(getPosition() + delta);
        setTarget(getTarget() + delta);
        endCameraBatch();
    }
}

void CameraDB::updateOrbitParameters(const Vector3& center,
                                     float horizontalAngle,
                                     float verticalAngle,
                                     float radius) {
    // 注意：这个方法在拖拽过程中频繁调用，事务管理应该在调用方处理
    // 调试输出
    // Updated orbit parameters

    verticalAngle = std::clamp(verticalAngle, kMinOrbitVerticalAngle, kMaxOrbitVerticalAngle);

    beginCameraBatch();
    if (center != getOrbitCenter()) {
        setOrbitCenter(center);
    }
    if (std::abs(horizontalAngle - getHorizontalAngle()) > 1e-6f) {
        setHorizontalAngle(horizontalAngle);
    }
    if (std::abs(verticalAngle - getVerticalAngle()) > 1e-6f) {
        setVerticalAngle(verticalAngle);
    }
    if (std::abs(radius - getOrbitRadius()) > 1e-4f) {
        setOrbitRadius(radius);
    }
    updatePositionFromOrbit();
    endCameraBatch();
}

CameraDB::OrbitParameters CameraDB::getCurrentOrbitParameters() const {
    OrbitParameters params;
    params.center = getOrbitCenter();
    params.horizontalAngle = getHorizontalAngle();
    params.verticalAngle = getVerticalAngle();
    params.radius = getOrbitRadius();
    return params;
}

CameraDB::RenderState CameraDB::getRenderStateSnapshot() const {
    std::lock_guard<std::mutex> lock(m_renderStateMutex);
    return m_renderState;
}

void CameraDB::updatePositionFromOrbit() {
    if (!getOrbitMode())
        return;

    // 从球坐标计算笛卡尔坐标
    float h = getHorizontalAngle();
    float v = getVerticalAngle();
    float r = getOrbitRadius();
    Vector3 center = getOrbitCenter();

    // 球坐标到笛卡尔坐标转换（Z-up坐标系）
    // - h (水平角/方位角) 从 +X 轴开始，在XY平面内旋转
    // - v (垂直角/仰角) 从 XY平面开始，向Z轴方向为正
    Vector3 newPosition;
    newPosition.x = center.x + r * cos(v) * cos(h);
    newPosition.y = center.y + r * cos(v) * sin(h);
    newPosition.z = center.z + r * sin(v);

    // Updated position from orbit

    // 使用 setter 方法而不是 setProperty，这样会触发 onPositionChanged 回调
    setPosition(newPosition);
    setTarget(center);
    setUpVector(orbitUpVector(h, v));
}

void CameraDB::syncOrbitParametersFromPosition() {
    if (!getOrbitMode())
        return;

    // 从当前位置和目标反向计算轨道参数
    Vector3 position = getPosition();
    Vector3 target = getTarget();
    Vector3 delta = position - target;

    // 计算半径
    float radius = length(delta);
    if (radius < 0.0001f) {
        // 位置和目标太接近，无法计算有效的轨道参数
        return;
    }

    // 计算水平角（方位角）
    // atan2(y, x) 返回从+X轴到(x,y)的角度
    float horizontal = atan2(delta.y, delta.x);

    // 计算垂直角（仰角）
    // 从XY平面的投影长度
    float xyDistance = sqrt(delta.x * delta.x + delta.y * delta.y);
    // 垂直角是从XY平面到点的角度
    float vertical = atan2(delta.z, xyDistance);

    beginCameraBatch();
    if (target != getOrbitCenter()) {
        setOrbitCenter(target);
    }
    if (std::abs(horizontal - getHorizontalAngle()) > 1e-6f) {
        setHorizontalAngle(horizontal);
    }
    if (std::abs(vertical - getVerticalAngle()) > 1e-6f) {
        setVerticalAngle(vertical);
    }
    if (std::abs(radius - getOrbitRadius()) > 1e-4f) {
        setOrbitRadius(radius);
    }
    setUpVector(orbitUpVector(horizontal, vertical));
    endCameraBatch();

    LOG_DEBUG("Synced orbit parameters from position - H: {:.2f}°, V: {:.2f}°, R: {:.2f}",
              horizontal * 180.0f / M_PI, vertical * 180.0f / M_PI, radius);
}

// === 相机计算方法实现 ===

Vector3 CameraDB::getForwardVector() const {
    Vector3 forward = getTarget() - getPosition();
    return normalize(forward);
}

Vector3 CameraDB::getRightVector() const {
    Vector3 forward = getForwardVector();
    Vector3 right = cross(forward, getUpVector());
    return normalize(right);
}

std::vector<float> CameraDB::calculateViewMatrix() const {
    Vector3 pos = getPosition();
    Vector3 target = getTarget();
    Vector3 up = getUpVector();

    // 计算相机坐标系基向量
    Vector3 forward = normalize(target - pos);
    Vector3 right = normalize(cross(forward, up));
    Vector3 newUp = cross(right, forward);

    // 构建视图矩阵 (行主序)
    std::vector<float> matrix(16);

    // 第一行
    matrix[0] = right.x;
    matrix[1] = right.y;
    matrix[2] = right.z;
    matrix[3] = -dot(right, pos);

    // 第二行
    matrix[4] = newUp.x;
    matrix[5] = newUp.y;
    matrix[6] = newUp.z;
    matrix[7] = -dot(newUp, pos);

    // 第三行（注意：OpenGL使用右手坐标系，forward为负z方向）
    matrix[8] = -forward.x;
    matrix[9] = -forward.y;
    matrix[10] = -forward.z;
    matrix[11] = dot(forward, pos);

    // 第四行
    matrix[12] = 0.0f;
    matrix[13] = 0.0f;
    matrix[14] = 0.0f;
    matrix[15] = 1.0f;

    return matrix;
}

std::vector<float> CameraDB::calculateProjectionMatrix() const {
    return calculateProjectionMatrix(getAspectRatio());
}

std::vector<float> CameraDB::calculateProjectionMatrix(float aspectRatio) const {
    std::vector<float> matrix(16, 0.0f);

    const float aspect = aspectRatio > 1e-6f ? aspectRatio : 1.0f;
    float nearPlane = getNearClippingPlane();
    float farPlane = getFarClippingPlane();

    if (isPerspective()) {
        // 透视投影矩阵
        float fovRad = degreesToRadians(getFieldOfView());
        float f = 1.0f / tan(fovRad * 0.5f);

        matrix[0] = f / aspect;
        matrix[5] = f;
        matrix[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        matrix[11] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
        matrix[14] = -1.0f;
    } else {
        // 正交投影矩阵
        float size = getOrthographicSize();
        float right = size * aspect * 0.5f;
        float left = -right;
        float top = size * 0.5f;
        float bottom = -top;

        matrix[0] = 2.0f / (right - left);
        matrix[5] = 2.0f / (top - bottom);
        matrix[10] = -2.0f / (farPlane - nearPlane);
        matrix[12] = -(right + left) / (right - left);
        matrix[13] = -(top + bottom) / (top - bottom);
        matrix[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        matrix[15] = 1.0f;
    }
    return matrix;
}

// === 便捷访问方法实现 ===

float CameraDB::getCurrentRadius() const {
    if (getOrbitMode()) {
        return getOrbitRadius();
    } else {
        return getDistanceToTarget();
    }
}

float CameraDB::getDistanceToTarget() const {
    Vector3 delta = getTarget() - getPosition();
    return length(delta);
}

void CameraDB::resetToDefault() {
    // Camera reset is a view operation, not a data modification - no transaction needed

    // 重置到用户给定 OrcaSlicer 默认截图的斜向视角。
    beginCameraBatch();
    setUpVector(kOrcaDefaultViewUpVector);
    setProjectionTypeEnum(ProjectionType::PERSPECTIVE);
    setFieldOfView(45.0f);
    setOrthographicSize(10.0f);
    setAspectRatio(1.0f);
    setNearClippingPlane(0.1f);
    setFarClippingPlane(10000.0f);
    setOrbitMode(true);
    setOrbitCenter(Vector3(0, 0, 0));
    setOrbitRadius(430.0f);
    setHorizontalAngle(kOrcaDefaultHorizontalAngle);
    setVerticalAngle(M_PI * 0.25f);
    setEnabled(true);

    // 更新位置
    updatePositionFromOrbit();
    endCameraBatch();
}

// === 通知方法实现 ===

void CameraDB::notifyCameraChange(const std::string& propertyName) {
    handlePropertyChanged(propertyName, false);
}

void CameraDB::notifyOrbitChange(const std::string& propertyName) {
    handlePropertyChanged(propertyName, false);
}

// === 私有辅助方法实现 ===

void CameraDB::beginCameraBatch() {
    ++m_cameraBatchDepth;
}

void CameraDB::endCameraBatch() {
    if (m_cameraBatchDepth <= 0) {
        return;
    }

    --m_cameraBatchDepth;
    if (m_cameraBatchDepth != 0) {
        return;
    }

    const bool dirty = m_cameraBatchDirty;
    const bool needsClipping = m_cameraBatchNeedsClipping;
    m_cameraBatchDirty = false;
    m_cameraBatchNeedsClipping = false;

    if (needsClipping) {
        // 裁剪面计算做节流，避免滚轮/拖拽缩放时频繁扫场景包围盒。
        ++m_cameraBatchDepth;
        updateClippingPlanesFromCurrentScene(false);
        --m_cameraBatchDepth;
        m_cameraBatchDirty = false;
        m_cameraBatchNeedsClipping = false;
    }

    if (dirty || needsClipping) {
        publishRenderState();
        notifyCameraStateChanged();
    }
}

void CameraDB::handlePropertyChanged(const std::string& propertyName, bool updateClippingPlanes) {
    if (m_cameraBatchDepth > 0) {
        m_cameraBatchDirty = true;
        m_cameraBatchNeedsClipping = m_cameraBatchNeedsClipping || updateClippingPlanes;
        return;
    }

    // Publish before notifying render listeners. This creates one coherent
    // hand-off point even though the individual CameraDB fields are updated by
    // the GUI thread and consumed by a separate render thread.
    publishRenderState();

    if (auto* docManager = DocumentManager::instance()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, propertyName);
    }

    if (updateClippingPlanes) {
        updateClippingPlanesFromCurrentScene(false);
    }
}

void CameraDB::updateClippingPlanesFromCurrentScene(bool force) {
    const auto now = std::chrono::steady_clock::now();
    if (!force && m_lastClippingUpdate.time_since_epoch().count() != 0 &&
        now - m_lastClippingUpdate < kInteractiveClippingUpdateInterval) {
        return;
    }

    m_lastClippingUpdate = now;

    const auto sceneBBox = SceneBounds::computeSceneBounds();
    const float sceneBBoxDiagonal = sceneBBox.valid ? sceneBBox.getDiagonal() : 100.0f;
    updateClippingPlanesFromScene(sceneBBoxDiagonal);
}

void CameraDB::notifyCameraStateChanged() {
    if (auto* docManager = DocumentManager::instance()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, "CameraState");
    }
}

void CameraDB::publishRenderState() {
    RenderState next;
    next.position = getPosition();
    next.target = getTarget();
    next.upVector = getUpVector();
    next.orbitMode = getOrbitMode();
    next.orbit = getCurrentOrbitParameters();
    next.projectionType = getProjectionType();
    next.fieldOfView = getFieldOfView();
    next.orthographicSize = getOrthographicSize();
    next.nearClippingPlane = getNearClippingPlane();
    next.farClippingPlane = getFarClippingPlane();

    std::lock_guard<std::mutex> lock(m_renderStateMutex);
    next.revision = m_renderState.revision + 1;
    m_renderState = next;
}

Vector3 CameraDB::normalize(const Vector3& v) const {
    float len = length(v);
    if (len > 0.0f) {
        return Vector3(v.x / len, v.y / len, v.z / len);
    }
    return Vector3(0, 0, 1); // 默认前向向量
}

Vector3 CameraDB::cross(const Vector3& a, const Vector3& b) const {
    return Vector3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

float CameraDB::dot(const Vector3& a, const Vector3& b) const {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float CameraDB::length(const Vector3& v) const {
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// === 动态裁剪面计算实现 ===

void CameraDB::updateClippingPlanesFromScene(float sceneBBoxDiagonal) {
    float cameraDistance = getCurrentRadius();

    // 计算新的 Near 和 Far
    float newNear = calculateNearPlane(cameraDistance);
    float newFar = calculateFarPlane(sceneBBoxDiagonal, cameraDistance);

    // 只有当值变化较大时才更新（避免频繁变化）
    const float threshold = 0.1f; // 10% 变化阈值
    bool nearChanged = (std::abs(newNear - getNearClippingPlane()) / getNearClippingPlane()) > threshold;
    bool farChanged = (std::abs(newFar - getFarClippingPlane()) / getFarClippingPlane()) > threshold;

    if (nearChanged || farChanged) {
        setNearClippingPlane(newNear);
        setFarClippingPlane(newFar);

        LOG_DEBUG("CameraDB: Clipping planes updated - Near: {:.4f}, Far: {:.1f}, Distance: {:.1f}, BBox: {:.1f}",
                  newNear, newFar, cameraDistance, sceneBBoxDiagonal);
    }
}

float CameraDB::calculateNearPlane(float cameraDistance) {
    // 分段固定策略：避免深度精度频繁跳变
    // 距离越大，Near 越大（保证远处精度）
    if (cameraDistance > 100.0f) {
        return 1.0f;
    } else if (cameraDistance > 10.0f) {
        return 0.5f;
    } else if (cameraDistance > 1.0f) {
        return 0.1f;
    } else if (cameraDistance > 0.1f) {
        return 0.01f;
    } else {
        return 0.001f;
    }
}

float CameraDB::calculateFarPlane(float sceneBBoxDiagonal, float cameraDistance) {
    // 动态策略：
    // 1. 确保整个场景可见（至少 bboxDiagonal * 2）
    // 2. 相机缩放后远处物体仍可见（cameraDistance * 5）

    float farPlane = std::max(sceneBBoxDiagonal * 2.0f, cameraDistance * 5.0f);

    // 如果场景为空或很小，使用最小值
    farPlane = std::max(farPlane, 100.0f);

    // 设置最大值限制
    farPlane = std::min(farPlane, 50000.0f);

    return farPlane;
}
