#pragma once

#include <QObject>
#include <QProperty>
#include <QBindable>
#include <QtQml/qqml.h>
#include <QVector3D>
#include <QVariantList>
#include <memory>
#include "PropertyBridgeBase.hpp"
#include "BaseID.hpp"
#include "ActorDB.hpp"

class ModelPropertyBridge : public PropertyBridgeBase {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // 选中模型的基本信息
    Q_PROPERTY(bool hasSelectedModel READ hasSelectedModel
               BINDABLE bindableHasSelectedModel NOTIFY hasSelectedModelChanged)
    Q_PROPERTY(QString modelType READ modelType
               BINDABLE bindableModelType NOTIFY modelTypeChanged)
    Q_PROPERTY(QString displayName READ displayName
               BINDABLE bindableDisplayName NOTIFY displayNameChanged)

    // Transform properties - 支持读写
    Q_PROPERTY(QVector3D position READ position WRITE setPosition
               BINDABLE bindablePosition NOTIFY positionChanged)
    Q_PROPERTY(QVector3D rotation READ rotation WRITE setRotation
               BINDABLE bindableRotation NOTIFY rotationChanged)
    Q_PROPERTY(QVector3D scale READ scale WRITE setScale
               BINDABLE bindableScale NOTIFY scaleChanged)

    // Material properties
    Q_PROPERTY(QVector3D diffuseColor READ diffuseColor WRITE setDiffuseColor
               BINDABLE bindableDiffuseColor NOTIFY diffuseColorChanged)
    Q_PROPERTY(float opacity READ opacity WRITE setOpacity
               BINDABLE bindableOpacity NOTIFY opacityChanged)
    // 0 = not bound, -1 = mixed across parts, positive = explicit slot.
    Q_PROPERTY(int filamentSlot READ filamentSlot NOTIFY filamentBindingChanged)
    Q_PROPERTY(QVariantList filamentSlots READ filamentSlots
               NOTIFY filamentSlotsChanged)

    // Bounds (只读)
    Q_PROPERTY(bool boundsValid READ boundsValid
               BINDABLE bindableBoundsValid NOTIFY boundsValidChanged)
    Q_PROPERTY(QVector3D boundsCenter READ boundsCenter
               BINDABLE bindableBoundsCenter NOTIFY boundsCenterChanged)
    Q_PROPERTY(QVector3D boundsSize READ boundsSize
               BINDABLE bindableBoundsSize NOTIFY boundsSizeChanged)
    Q_PROPERTY(int partCount READ partCount NOTIFY modelStatisticsChanged)
    Q_PROPERTY(qulonglong vertexCount READ vertexCount
               NOTIFY modelStatisticsChanged)
    Q_PROPERTY(qulonglong triangleCount READ triangleCount
               NOTIFY modelStatisticsChanged)

signals:
    void hasSelectedModelChanged();
    void modelTypeChanged();
    void displayNameChanged();
    void positionChanged();
    void rotationChanged();
    void scaleChanged();
    void diffuseColorChanged();
    void opacityChanged();
    void filamentBindingChanged();
    void filamentSlotsChanged();
    void boundsValidChanged();
    void boundsCenterChanged();
    void boundsSizeChanged();
    void modelStatisticsChanged();

public:
    explicit ModelPropertyBridge(QObject* parent = nullptr);
    ~ModelPropertyBridge() override;

    static QObject* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    // Property getters
    bool hasSelectedModel() const;
    QString modelType() const;
    QString displayName() const;
    QVector3D position() const;
    QVector3D rotation() const;
    QVector3D scale() const;
    QVector3D diffuseColor() const;
    float opacity() const;
    int filamentSlot() const;
    QVariantList filamentSlots() const;
    bool boundsValid() const;
    QVector3D boundsCenter() const;
    QVector3D boundsSize() const;
    int partCount() const;
    qulonglong vertexCount() const;
    qulonglong triangleCount() const;

    // Property setters (for editable properties)
    void setPosition(const QVector3D& position);
    void setRotation(const QVector3D& rotation);
    void setScale(const QVector3D& scale);
    void setDiffuseColor(const QVector3D& color);
    void setOpacity(float opacity);
    Q_INVOKABLE bool assignFilamentSlot(int slot);
    Q_INVOKABLE bool useProjectDefaultFilament();

    // Bindables
    QBindable<bool> bindableHasSelectedModel();
    QBindable<QString> bindableModelType();
    QBindable<QString> bindableDisplayName();
    QBindable<QVector3D> bindablePosition();
    QBindable<QVector3D> bindableRotation();
    QBindable<QVector3D> bindableScale();
    QBindable<QVector3D> bindableDiffuseColor();
    QBindable<float> bindableOpacity();
    QBindable<bool> bindableBoundsValid();
    QBindable<QVector3D> bindableBoundsCenter();
    QBindable<QVector3D> bindableBoundsSize();

protected:
    // Override from PropertyBridgeBase
    void onPropertyChanged(const DBInstanceID& id,
                           const std::string& propertyName,
                           const std::any& newValue) override;
    void onObjectDeleted(const DBInstanceID& id) override;

private:
    void connectToSelectionBridge();
    void onSelectionChanged(const DBInstanceID& selectedId);
    void updateFromActor(const std::shared_ptr<ActorDB>& actor);
    void updateSpecificProperty(const std::string& propertyName);
    void clearProperties();
    void updateBounds(const std::shared_ptr<ActorDB>& actor);
    void updateModelStatistics(
        const std::shared_ptr<class ModelInstanceDB>& instance);
    void updateFilamentBinding(
        const std::shared_ptr<class ModelInstanceDB>& instance);
    std::shared_ptr<ActorDB> getSelectedActor() const;

    // Qt6 bindable properties
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, bool, m_hasSelectedModel, &ModelPropertyBridge::hasSelectedModelChanged)
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, QString, m_modelType, &ModelPropertyBridge::modelTypeChanged)
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, QString, m_displayName, &ModelPropertyBridge::displayNameChanged)
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, QVector3D, m_position, &ModelPropertyBridge::positionChanged)
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, QVector3D, m_rotation, &ModelPropertyBridge::rotationChanged)
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, QVector3D, m_scale, &ModelPropertyBridge::scaleChanged)
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, QVector3D, m_diffuseColor, &ModelPropertyBridge::diffuseColorChanged)
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, float, m_opacity, &ModelPropertyBridge::opacityChanged)
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, bool, m_boundsValid, &ModelPropertyBridge::boundsValidChanged)
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, QVector3D, m_boundsCenter, &ModelPropertyBridge::boundsCenterChanged)
    Q_OBJECT_BINDABLE_PROPERTY(ModelPropertyBridge, QVector3D, m_boundsSize, &ModelPropertyBridge::boundsSizeChanged)

    DBInstanceID m_selectedId;
    int m_filamentSlot{0};
    int m_partCount{0};
    qulonglong m_vertexCount{0};
    qulonglong m_triangleCount{0};
};
