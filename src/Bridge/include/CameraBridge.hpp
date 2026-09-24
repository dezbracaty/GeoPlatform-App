#pragma once

#include <QObject>
#include <QProperty>
#include <QBindable>
#include <QtQml/qqml.h>
#include <QVector3D>
#include "PropertyBridgeBase.hpp"
#include <CameraDB.hpp>

/**
 * @brief Bridge class to expose CameraDB properties to QML for animation
 *
 * This class provides a QML-friendly interface to CameraDB, enabling smooth
 * camera animations through QML's animation system.
 */
class CameraBridge : public PropertyBridgeBase {
    Q_OBJECT

    // Camera position - using Q_OBJECT_BINDABLE_PROPERTY
    Q_PROPERTY(qreal cameraX READ cameraX WRITE setCameraX
                   BINDABLE bindableCameraX NOTIFY cameraXChanged)
    Q_PROPERTY(qreal cameraY READ cameraY WRITE setCameraY
                   BINDABLE bindableCameraY NOTIFY cameraYChanged)
    Q_PROPERTY(qreal cameraZ READ cameraZ WRITE setCameraZ
                   BINDABLE bindableCameraZ NOTIFY cameraZChanged)

    // Focal point (read-only)
    Q_PROPERTY(qreal focalX READ focalX NOTIFY focalXChanged)
    Q_PROPERTY(qreal focalY READ focalY NOTIFY focalYChanged)
    Q_PROPERTY(qreal focalZ READ focalZ NOTIFY focalZChanged)

    // Up vector
    Q_PROPERTY(qreal upX READ upX WRITE setUpX
                   BINDABLE bindableUpX NOTIFY upXChanged)
    Q_PROPERTY(qreal upY READ upY WRITE setUpY
                   BINDABLE bindableUpY NOTIFY upYChanged)
    Q_PROPERTY(qreal upZ READ upZ WRITE setUpZ
                   BINDABLE bindableUpZ NOTIFY upZChanged)

    // Animation helper properties
    Q_PROPERTY(qreal cameraDistance READ cameraDistance NOTIFY cameraDistanceChanged)

    QML_ELEMENT

signals:
    void cameraXChanged();
    void cameraYChanged();
    void cameraZChanged();
    void focalXChanged();
    void focalYChanged();
    void focalZChanged();
    void upXChanged();
    void upYChanged();
    void upZChanged();
    void cameraDistanceChanged();

    // Emitted when camera is connected
    void cameraConnected();

public:
    explicit CameraBridge(QObject* parent = nullptr);
    ~CameraBridge();

    // Singleton access
    static CameraBridge* instance();

    // QML singleton factory function
    static QObject* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // Getters
    qreal cameraX() const {
        return m_cameraX;
    }
    qreal cameraY() const {
        return m_cameraY;
    }
    qreal cameraZ() const {
        return m_cameraZ;
    }
    qreal focalX() const {
        return m_focalX;
    }
    qreal focalY() const {
        return m_focalY;
    }
    qreal focalZ() const {
        return m_focalZ;
    }
    qreal upX() const {
        return m_upX;
    }
    qreal upY() const {
        return m_upY;
    }
    qreal upZ() const {
        return m_upZ;
    }
    qreal cameraDistance() const;

    // Setters - update CameraDB within transactions
    void setCameraX(qreal x);
    void setCameraY(qreal y);
    void setCameraZ(qreal z);
    void setUpX(qreal x);
    void setUpY(qreal y);
    void setUpZ(qreal z);

    // Bindables
    QBindable<qreal> bindableCameraX() {
        return &m_cameraX;
    }
    QBindable<qreal> bindableCameraY() {
        return &m_cameraY;
    }
    QBindable<qreal> bindableCameraZ() {
        return &m_cameraZ;
    }
    QBindable<qreal> bindableUpX() {
        return &m_upX;
    }
    QBindable<qreal> bindableUpY() {
        return &m_upY;
    }
    QBindable<qreal> bindableUpZ() {
        return &m_upZ;
    }

    // Stateless target pose for the navigator's face, edge and corner directions.
    Q_INVOKABLE QVariantMap calculateTargetPose(const QVector3D& direction) const;

    // Switch to specific view using CameraDB methods
    Q_INVOKABLE void switchToView(const QString& viewName);

    // Sync orbit parameters after animation
    Q_INVOKABLE void syncOrbitParameters();

    // Get current camera for coordinate transformation
    std::shared_ptr<CameraDB> getCurrentCamera() const { return findMainCamera(); }

protected:
    void onPropertyChanged(const DBInstanceID& id,
                           const std::string& propertyName,
                           const std::any& newValue) override;

    void onObjectCreated(const DBInstanceID& id) override;
    void onObjectDeleted(const DBInstanceID& id) override;

private:
    void setupBindings();
    void bindToActiveCamera();
    std::shared_ptr<CameraDB> findMainCamera() const;
    void updateFromCamera();

    // Flag to control whether property changes should immediately update CameraDB
    // Set to false during animations to allow smooth transitions
    bool m_immediateUpdate = true;

private:
    // Using Q_OBJECT_BINDABLE_PROPERTY
    Q_OBJECT_BINDABLE_PROPERTY(CameraBridge, qreal, m_cameraX, &CameraBridge::cameraXChanged)
    Q_OBJECT_BINDABLE_PROPERTY(CameraBridge, qreal, m_cameraY, &CameraBridge::cameraYChanged)
    Q_OBJECT_BINDABLE_PROPERTY(CameraBridge, qreal, m_cameraZ, &CameraBridge::cameraZChanged)
    Q_OBJECT_BINDABLE_PROPERTY(CameraBridge, qreal, m_focalX, &CameraBridge::focalXChanged)
    Q_OBJECT_BINDABLE_PROPERTY(CameraBridge, qreal, m_focalY, &CameraBridge::focalYChanged)
    Q_OBJECT_BINDABLE_PROPERTY(CameraBridge, qreal, m_focalZ, &CameraBridge::focalZChanged)
    Q_OBJECT_BINDABLE_PROPERTY(CameraBridge, qreal, m_upX, &CameraBridge::upXChanged)
    Q_OBJECT_BINDABLE_PROPERTY(CameraBridge, qreal, m_upY, &CameraBridge::upYChanged)
    Q_OBJECT_BINDABLE_PROPERTY(CameraBridge, qreal, m_upZ, &CameraBridge::upZChanged)

    std::weak_ptr<CameraDB> m_camera;
    DBInstanceID m_cameraId;
    DocumentManager::ListenerID m_activeWindowListenerId{0};

    // Singleton instance
    static CameraBridge* s_instance;

public:
    // Public access to singleton
    static CameraBridge* getInstance() { return s_instance; }
};
