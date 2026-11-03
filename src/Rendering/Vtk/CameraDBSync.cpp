#include "CameraDBSync.hpp"
#include "VtkDBSyncFactory.hpp"
#include <vtkGenericOpenGLRenderWindow.h>
#include <cmath>

void CameraDBSync::initialize(
    const DBInstanceID& id,
    vtkSmartPointer<vtkRenderer> renderer,
    std::shared_ptr<RenderThread> renderThread) {
    
    m_id = id;
    m_vtkRenderer = renderer;
    m_renderThread = renderThread;
    
    auto cameraDB = documentDB<CameraDB>(id);
    if (!cameraDB) {
        LOG_ERROR("CameraDBSync::initialize - Invalid CameraDB for ID: {}", id.toString());
        return;
    }
    
    // 获取渲染器的相机，不创建新的
    if (m_renderThread) {
        // 使用 shared_from_this() 确保对象在lambda执行期间保持存活
        auto self = shared_from_this();
        dispatchRenderCommand([self](vtkGenericOpenGLRenderWindow* window, vtkObject* obj) {
            if (self->m_vtkRenderer) {
                // 使用渲染器现有的相机
                self->m_vtkCamera = self->m_vtkRenderer->GetActiveCamera();

                if (self->m_vtkCamera) {
                    LOG_INFO("CameraDBSync::initialize - Using existing VTK camera for ID: {}",
                             self->m_id.toString());

                    // 立即同步所有相机属性
                    self->syncAllCameraProperties();
                    self->logCameraState("After initialization");
                } else {
                    LOG_ERROR("CameraDBSync::initialize - No active camera in renderer for ID: {}",
                              self->m_id.toString());
                }
            }
        });
    }
    
    LOG_INFO("CameraDBSync::initialize - Initialized for ID: {}", id.toString());
}

void CameraDBSync::cleanup(const DBInstanceID& id) {
    LOG_INFO("CameraDBSync::cleanup - Cleaning up for ID: {}", id.toString());
    
    // Camera对象由渲染器管理，我们不需要删除
    m_vtkCamera = nullptr;
    m_vtkRenderer = nullptr;
    m_renderThread = nullptr;
}

void CameraDBSync::update(
    const DBInstanceID& id,
    ChangeType changeType,
    const std::string& propertyName) {

    auto cameraDB = documentDB<CameraDB>(m_id);
    if (!cameraDB || !m_vtkCamera) {
        return;
    }

    if (propertyName.empty() || propertyName == "CameraState") {
        syncAllCameraProperties();
        return;
    }

    // 根据属性名称执行对应的同步
    if (propertyName == "Position") {
        syncCameraPosition();
    }

    if (propertyName == "Target") {
        syncCameraTarget();
    }

    if (propertyName == "UpVector") {
        syncCameraUpVector();
    }

    if (propertyName == "FieldOfView" ||
        propertyName == "ProjectionType" ||
        propertyName == "OrthographicSize") {
        syncProjectionParameters();
    }

    if (propertyName == "NearClippingPlane" ||
        propertyName == "FarClippingPlane") {
        syncClippingPlanes();
    }

    if (propertyName == "OrbitRadius" ||
        propertyName == "HorizontalAngle" ||
        propertyName == "VerticalAngle" ||
        propertyName == "OrbitCenter") {
        syncOrbitParameters();
    }

    logCameraState("After property '" + propertyName + "' update");
}

void CameraDBSync::update(
    const DBInstanceID& id,
    ChangeType changeType,
    const trans::Prop& prop) {
    // Prop 参数版本：使用 PROP_##name() 语法进行属性比较

    auto cameraDB = documentDB<CameraDB>(m_id);
    if (!cameraDB || !m_vtkCamera) {
        return;
    }

    if (prop.name()[0] == '\0') {
        syncAllCameraProperties();
        return;
    }

    // 🎯 使用 PROP_##name() 语法进行属性比较
    if (prop == CameraDB::PROP_Position()) {
        syncCameraPosition();
    }

    if (prop == CameraDB::PROP_Target()) {
        syncCameraTarget();
    }

    if (prop == CameraDB::PROP_UpVector()) {
        syncCameraUpVector();
    }

    if (prop == CameraDB::PROP_FieldOfView() ||
        prop == CameraDB::PROP_ProjectionType() ||
        prop == CameraDB::PROP_OrthographicSize()) {
        syncProjectionParameters();
    }

    if (prop == CameraDB::PROP_NearClippingPlane() ||
        prop == CameraDB::PROP_FarClippingPlane()) {
        syncClippingPlanes();
    }

    if (prop == CameraDB::PROP_OrbitRadius() ||
        prop == CameraDB::PROP_HorizontalAngle() ||
        prop == CameraDB::PROP_VerticalAngle() ||
        prop == CameraDB::PROP_OrbitCenter()) {
        syncOrbitParameters();
    }

    // 属性变化后记录日志
    if (prop.name()[0] != '\0') {
        logCameraState("After property '" + std::string(prop.name()) + "' update");
    }
}

void CameraDBSync::setVisible(bool visible) {
    // Camera本身没有可见性概念，这里是空实现
    LOG_DEBUG("CameraDBSync::setVisible - Camera visibility not applicable");
}

void CameraDBSync::syncCameraPosition() {
    auto cameraDB = documentDB<CameraDB>(m_id);
    if (!cameraDB || !m_vtkCamera) return;

    const auto state = cameraDB->getRenderStateSnapshot();
    updateVTKCameraThreadSafe([position = state.position](vtkCamera* camera) {
        camera->SetPosition(position.x, position.y, position.z);
    });
}

void CameraDBSync::syncCameraTarget() {
    auto cameraDB = documentDB<CameraDB>(m_id);
    if (!cameraDB || !m_vtkCamera) return;

    const auto state = cameraDB->getRenderStateSnapshot();
    updateVTKCameraThreadSafe([target = state.target](vtkCamera* camera) {
        camera->SetFocalPoint(target.x, target.y, target.z);
    });
}

void CameraDBSync::syncCameraUpVector() {
    auto cameraDB = documentDB<CameraDB>(m_id);
    if (!cameraDB || !m_vtkCamera) return;

    const auto state = cameraDB->getRenderStateSnapshot();
    updateVTKCameraThreadSafe([upVector = state.upVector](vtkCamera* camera) {
        camera->SetViewUp(upVector.x, upVector.y, upVector.z);
    });
}

void CameraDBSync::syncProjectionParameters() {
    auto cameraDB = documentDB<CameraDB>(m_id);
    if (!cameraDB || !m_vtkCamera) return;

    const auto state = cameraDB->getRenderStateSnapshot();
    updateVTKCameraThreadSafe([state](vtkCamera* camera) {
        if (state.projectionType == static_cast<int>(CameraDB::ProjectionType::PERSPECTIVE)) {
            camera->SetParallelProjection(0);
            camera->SetViewAngle(state.fieldOfView);
        } else {
            camera->SetParallelProjection(1);
            camera->SetParallelScale(state.orthographicSize);
        }
    });
}

void CameraDBSync::syncClippingPlanes() {
    auto cameraDB = documentDB<CameraDB>(m_id);
    if (!cameraDB || !m_vtkCamera) return;

    const auto state = cameraDB->getRenderStateSnapshot();
    updateVTKCameraThreadSafe([nearClip = state.nearClippingPlane,
                               farClip = state.farClippingPlane](vtkCamera* camera) {
        camera->SetClippingRange(nearClip, farClip);
    });
}

void CameraDBSync::syncOrbitParameters() {
    auto cameraDB = documentDB<CameraDB>(m_id);
    if (!cameraDB || !m_vtkCamera) return;

    const auto state = cameraDB->getRenderStateSnapshot();
    if (!state.orbitMode) return;

    // 轨道模式下，根据轨道参数重新计算相机位置
    updateVTKCameraThreadSafe([orbitParams = state.orbit](vtkCamera* camera) {

        // 从轨道参数计算相机位置
        float cosH = cos(orbitParams.horizontalAngle);
        float sinH = sin(orbitParams.horizontalAngle);
        float cosV = cos(orbitParams.verticalAngle);
        float sinV = sin(orbitParams.verticalAngle);

        Vector3 newPosition;
        newPosition.x = orbitParams.center.x + orbitParams.radius * cosV * cosH;
        newPosition.y = orbitParams.center.y + orbitParams.radius * cosV * sinH;
        newPosition.z = orbitParams.center.z + orbitParams.radius * sinV;

        camera->SetPosition(newPosition.x, newPosition.y, newPosition.z);
        camera->SetFocalPoint(orbitParams.center.x, orbitParams.center.y, orbitParams.center.z);
    });

    // 动态更新裁剪面
    updateClippingPlanesFromScene();
}

void CameraDBSync::updateClippingPlanesFromScene() {
    // 裁剪面更新已在 CameraDB::onOrbitRadiusChanged 回调中处理（DB 层职责）
    // DBSync 只负责读取 DB 并同步到 VTK，不应修改 DB
    // 这里保留空实现以保持接口兼容性
}

void CameraDBSync::syncAllCameraProperties() {
    auto cameraDB = documentDB<CameraDB>(m_id);
    if (!cameraDB || !m_vtkCamera) return;

    // 聚合更新只派发一条渲染线程命令，降低相机高频交互时的命令队列压力。
    const auto state = cameraDB->getRenderStateSnapshot();
    const Vector3 position = state.position;
    const Vector3 target = state.target;
    const Vector3 upVector = state.upVector;
    const bool perspective =
        state.projectionType == static_cast<int>(CameraDB::ProjectionType::PERSPECTIVE);
    const float fieldOfView = state.fieldOfView;
    const float orthographicSize = state.orthographicSize;
    const float nearClip = state.nearClippingPlane;
    const float farClip = state.farClippingPlane;

    updateVTKCameraThreadSafe([position, target, upVector, perspective, fieldOfView, orthographicSize, nearClip, farClip](vtkCamera* camera) {
        camera->SetPosition(position.x, position.y, position.z);
        camera->SetFocalPoint(target.x, target.y, target.z);
        camera->SetViewUp(upVector.x, upVector.y, upVector.z);

        if (perspective) {
            camera->SetParallelProjection(0);
            camera->SetViewAngle(fieldOfView);
        } else {
            camera->SetParallelProjection(1);
            camera->SetParallelScale(orthographicSize);
        }

        camera->SetClippingRange(nearClip, farClip);
    });
}

void CameraDBSync::logCameraState(const std::string& context) const {
    // Camera state logging disabled to reduce log noise
}

void CameraDBSync::updateVTKCameraThreadSafe(std::function<void(vtkCamera*)> operation) {
    if (!m_vtkCamera || !m_renderThread) return;

    if (isOnRenderThread()) {
        operation(m_vtkCamera);
        return;
    }

    // 使用 shared_from_this() 确保对象在lambda执行期间保持存活
    auto self = shared_from_this();
    dispatchRenderCommand([self, operation](vtkGenericOpenGLRenderWindow* window, vtkObject* obj) {
        operation(self->m_vtkCamera);
    });
}

namespace {
bool matchesRendererCamera(
    const GPlatform::Rendering::RendererInstanceContext& context,
    const std::shared_ptr<const AutoRegisterDB>& dbInstance) {
    return dbInstance && context.cameraId != INVALID_DB_ID &&
           dbInstance->getDBInstanceID() == context.cameraId;
}
}

// 静态注册
REGISTER_VTK_DBSYNC_FILTERED(
    TypeID::CAMERA_DB, CameraDBSync,
    GPlatform::Rendering::RendererRoleSets::PrepareAndPreview,
    matchesRendererCamera)
