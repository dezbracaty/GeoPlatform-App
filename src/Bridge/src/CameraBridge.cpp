#include "CameraBridge.hpp"
#include "BridgeRegistration.hpp"
#include <CameraNavigationController.hpp>
#include "Foundation/Log.h"
#include <DocumentManager.hpp>
#include <QQmlEngine>
#include <cmath>
#include <algorithm>

// Initialize static instance
CameraBridge* CameraBridge::s_instance = nullptr;

CameraBridge::CameraBridge(QObject* parent)
    : PropertyBridgeBase(parent) {

    // Set singleton instance
    if (!s_instance) {
        s_instance = this;
    }

    setupBindings();

    m_activeWindowListenerId = DocumentManager::instance()->addActiveWindowListener(
        [this](const DBInstanceID&, const DBInstanceID&) {
            bindToActiveCamera();
        });

    // Connect property change signals to update CameraDB
    // This allows QML animations to work properly
    connect(this, &CameraBridge::cameraXChanged, [this]() {
        auto camera = m_camera.lock();
        if (camera && m_immediateUpdate) {
            // No transaction for view operations
            Vector3 pos = camera->getPosition();
            if (std::abs(pos.x - m_cameraX.value()) > 0.0001) {
                pos.x = m_cameraX.value();
                camera->setPosition(pos);
            }
        }
    });

    connect(this, &CameraBridge::cameraYChanged, [this]() {
        auto camera = m_camera.lock();
        if (camera && m_immediateUpdate) {
            // No transaction for view operations
            Vector3 pos = camera->getPosition();
            if (std::abs(pos.y - m_cameraY.value()) > 0.0001) {
                pos.y = m_cameraY.value();
                camera->setPosition(pos);
            }
        }
    });

    connect(this, &CameraBridge::cameraZChanged, [this]() {
        auto camera = m_camera.lock();
        if (camera && m_immediateUpdate) {
            // No transaction for view operations
            Vector3 pos = camera->getPosition();
            if (std::abs(pos.z - m_cameraZ.value()) > 0.0001) {
                pos.z = m_cameraZ.value();
                camera->setPosition(pos);
            }
        }
    });

    // Connect up vector signals
    connect(this, &CameraBridge::upXChanged, [this]() {
        auto camera = m_camera.lock();
        if (camera && m_immediateUpdate) {
            // No transaction for view operations
            Vector3 up = camera->getUpVector();
            if (std::abs(up.x - m_upX.value()) > 0.0001) {
                up.x = m_upX.value();
                camera->setUpVector(up);
            }
        }
    });

    connect(this, &CameraBridge::upYChanged, [this]() {
        auto camera = m_camera.lock();
        if (camera && m_immediateUpdate) {
            // No transaction for view operations
            Vector3 up = camera->getUpVector();
            if (std::abs(up.y - m_upY.value()) > 0.0001) {
                up.y = m_upY.value();
                camera->setUpVector(up);
            }
        }
    });

    connect(this, &CameraBridge::upZChanged, [this]() {
        auto camera = m_camera.lock();
        if (camera && m_immediateUpdate) {
            // No transaction for view operations
            Vector3 up = camera->getUpVector();
            if (std::abs(up.z - m_upZ.value()) > 0.0001) {
                up.z = m_upZ.value();
                camera->setUpVector(up);
            }
        }
    });
}

CameraBridge::~CameraBridge() {

    if (m_activeWindowListenerId != 0) {
        DocumentManager::instance()->removeActiveWindowListener(
            m_activeWindowListenerId);
        m_activeWindowListenerId = 0;
    }

    // Clear singleton instance
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

CameraBridge* CameraBridge::instance() {
    return s_instance;
}

QObject* CameraBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(jsEngine)

    // Create or return the singleton instance
    static CameraBridge* instance = new CameraBridge(qmlEngine);

    // Set ownership to C++ to prevent QML from deleting it
    QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);

    return instance;
}

void CameraBridge::setupBindings() {
    bindToActiveCamera();
}

void CameraBridge::bindToActiveCamera() {
    const auto camera = findMainCamera();
    const DBInstanceID nextCameraId =
        camera ? camera->getDBInstanceID() : INVALID_DB_ID;
    if (nextCameraId == m_cameraId && !m_camera.expired()) {
        return;
    }

    if (m_cameraId != INVALID_DB_ID) {
        stopListeningTo(m_cameraId);
    }

    m_camera = camera;
    m_cameraId = nextCameraId;
    if (!camera) {
        return;
    }

    startListeningTo(m_cameraId);
    updateFromCamera();
    emit cameraConnected();
}

std::shared_ptr<CameraDB> CameraBridge::findMainCamera() const {
    return CameraNavigationController::currentCamera();
}

void CameraBridge::onPropertyChanged(const DBInstanceID& id,
                                     const std::string& propertyName,
                                     const std::any& newValue) {
    if (id == m_cameraId) {
        // Camera properties changed, update bound properties
        updateFromCamera();
    }
}

void CameraBridge::onObjectCreated(const DBInstanceID& id) {
    // Check if a new camera was created and we don't have one
    if (!m_camera.lock()) {
        // Try to get the created object
        auto obj = DocumentManager::instance()->getDBInstance(id);
        if (obj) {
            // Check if it's a camera
            auto camera = std::dynamic_pointer_cast<CameraDB>(obj);
            if (camera) {
                // During initial view creation CameraDB is registered before
                // WindowDB receives its immutable camera binding. Preserve the
                // legacy first-camera bootstrap; subsequent active-window
                // changes use bindToActiveCamera().
                m_camera = camera;
                m_cameraId = id;
                startListeningTo(m_cameraId);
                updateFromCamera();
                emit cameraConnected();
            }
        }
    }
}

void CameraBridge::onObjectDeleted(const DBInstanceID& id) {
    if (id == m_cameraId) {
        // Camera was deleted
        stopListeningTo(id);
        m_camera.reset();
        m_cameraId = DBInstanceID();
        bindToActiveCamera();
    }
}

void CameraBridge::updateFromCamera() {
    auto camera = m_camera.lock();
    if (!camera)
        return;

    // Update position
    Vector3 pos = camera->getPosition();
    m_cameraX = pos.x;
    m_cameraY = pos.y;
    m_cameraZ = pos.z;

    // Update focal point (Target)
    Vector3 focal = camera->getTarget();
    m_focalX = focal.x;
    m_focalY = focal.y;
    m_focalZ = focal.z;

    // Update up vector
    Vector3 up = camera->getUpVector();
    m_upX = up.x;
    m_upY = up.y;
    m_upZ = up.z;

    // Emit camera distance changed
    emit cameraDistanceChanged();
}

qreal CameraBridge::cameraDistance() const {
    // Calculate distance from camera to focal point
    qreal dx = m_cameraX - m_focalX;
    qreal dy = m_cameraY - m_focalY;
    qreal dz = m_cameraZ - m_focalZ;

    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void CameraBridge::setCameraX(qreal x) {
    auto camera = m_camera.lock();
    if (!camera)
        return;

    // No transaction for view operations
    Vector3 pos = camera->getPosition();
    pos.x = x;
    camera->setPosition(pos);
}

void CameraBridge::setCameraY(qreal y) {
    auto camera = m_camera.lock();
    if (!camera)
        return;

    // No transaction for view operations
    Vector3 pos = camera->getPosition();
    pos.y = y;
    camera->setPosition(pos);
}

void CameraBridge::setCameraZ(qreal z) {
    auto camera = m_camera.lock();
    if (!camera)
        return;

    // No transaction for view operations
    Vector3 pos = camera->getPosition();
    pos.z = z;
    camera->setPosition(pos);
}

void CameraBridge::setUpX(qreal x) {
    auto camera = m_camera.lock();
    if (!camera)
        return;

    // No transaction for view operations
    Vector3 up = camera->getUpVector();
    up.x = x;
    camera->setUpVector(up);
}

void CameraBridge::setUpY(qreal y) {
    auto camera = m_camera.lock();
    if (!camera)
        return;

    // No transaction for view operations
    Vector3 up = camera->getUpVector();
    up.y = y;
    camera->setUpVector(up);
}

void CameraBridge::setUpZ(qreal z) {
    auto camera = m_camera.lock();
    if (!camera)
        return;

    // No transaction for view operations
    Vector3 up = camera->getUpVector();
    up.z = z;
    camera->setUpVector(up);
}

QVariantMap CameraBridge::calculateTargetPose(const QVector3D& direction) const {
    const auto camera = findMainCamera();
    if (!camera || direction.isNull() || !std::isfinite(direction.lengthSquared())) {
        return {};
    }
    const double yaw = std::atan2(direction.y(), direction.x());
    // Match the pole margin used by existing camera presets.
    const double pitch = std::clamp(
        static_cast<double>(std::atan2(direction.z(), std::hypot(direction.x(), direction.y()))),
        -M_PI / 2 + M_PI / 180, M_PI / 2 - M_PI / 180);
    const QVector3D outward(std::cos(pitch) * std::cos(yaw),
                            std::cos(pitch) * std::sin(yaw), std::sin(pitch));
    const QVector3D up(-std::sin(pitch) * std::cos(yaw),
                      -std::sin(pitch) * std::sin(yaw), std::cos(pitch));
    const Vector3 target = camera->getTarget();
    const QVector3D position = QVector3D(target.x, target.y, target.z)
        + outward * camera->getDistanceToTarget();
    return {{QStringLiteral("position"), QVariant::fromValue(position)},
            {QStringLiteral("up"), QVariant::fromValue(up)}};
}

void CameraBridge::switchToView(const QString& viewName) {
    const auto preset = CameraNavigationController::presetFromOrientation(viewName);
    if (!preset) {
        LOG_WARN("CameraBridge: unknown view name '{}'", viewName.toStdString());
        return;
    }

    auto camera = m_camera.lock();
    if (CameraNavigationController::setPreset(*preset, camera)) {
        updateFromCamera();
    }
}

void CameraBridge::syncOrbitParameters() {
    auto camera = m_camera.lock();
    if (!camera) {
        camera = findMainCamera();
        if (!camera) {
            LOG_WARN("No camera available for orbit parameter sync");
            return;
        }
    }

    if (CameraNavigationController::syncOrbitFromPosition(camera)) {
        LOG_INFO("Synced orbit parameters after animation");
    }
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    CameraBridge, "CameraBridge", &CameraBridge::create)
