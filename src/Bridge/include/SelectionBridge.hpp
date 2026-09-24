#pragma once

#include "PropertyBridgeBase.hpp"
#include <QVariant>
#include <QVector3D>
#include <QProperty>
#include <QBindable>
#include <qqmlregistration.h>
#include <QQmlEngine>
#include <QJSEngine>
#include <memory>
#include <vector>
#include <SystemTypes.hpp>

// Forward declarations
class ActorDB;

/**
 * @brief SelectionBridge - Bridges selection events from C++ to QML
 *
 * This class provides a QML-accessible interface for selection management,
 * allowing QML components to react to selection changes in the 3D scene.
 *
 * Extended to support material property editing for selected actors.
 */
class SelectionBridge : public PropertyBridgeBase {
    Q_OBJECT

    // === Selection Properties ===
    Q_PROPERTY(QVariant selectedModel READ selectedModel NOTIFY selectedModelChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY hasSelectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionCountChanged)
    Q_PROPERTY(int selectedId READ getSelectedId NOTIFY selectedModelChanged)
    Q_PROPERTY(bool hasOrientationWidget READ hasOrientationWidget NOTIFY hasOrientationWidgetChanged)
    Q_PROPERTY(bool hasScaleWidget READ hasScaleWidget NOTIFY hasScaleWidgetChanged)
    Q_PROPERTY(bool hasTranslateWidget READ hasTranslateWidget NOTIFY hasTranslateWidgetChanged)
    Q_PROPERTY(bool hasMirrorWidget READ hasMirrorWidget NOTIFY hasMirrorWidgetChanged)

    // === Material Color Properties ===
    Q_PROPERTY(QVector3D diffuseColor READ diffuseColor WRITE setDiffuseColor
               BINDABLE bindableDiffuseColor NOTIFY diffuseColorChanged)
    Q_PROPERTY(QVector3D ambientColor READ ambientColor WRITE setAmbientColor
               BINDABLE bindableAmbientColor NOTIFY ambientColorChanged)
    Q_PROPERTY(QVector3D specularColor READ specularColor WRITE setSpecularColor
               BINDABLE bindableSpecularColor NOTIFY specularColorChanged)

    // === PBR Properties ===
    Q_PROPERTY(float metallic READ metallic WRITE setMetallic
               BINDABLE bindableMetallic NOTIFY metallicChanged)
    Q_PROPERTY(float roughness READ roughness WRITE setRoughness
               BINDABLE bindableRoughness NOTIFY roughnessChanged)
    Q_PROPERTY(float materialOpacity READ materialOpacity WRITE setMaterialOpacity
               BINDABLE bindableMaterialOpacity NOTIFY materialOpacityChanged)

    // === Phong Lighting Properties ===
    Q_PROPERTY(float ambient READ ambient WRITE setAmbient
               BINDABLE bindableAmbient NOTIFY ambientChanged)
    Q_PROPERTY(float diffuse READ diffuse WRITE setDiffuse
               BINDABLE bindableDiffuse NOTIFY diffuseChanged)
    Q_PROPERTY(float specular READ specular WRITE setSpecular
               BINDABLE bindableSpecular NOTIFY specularChanged)
    Q_PROPERTY(float specularPower READ specularPower WRITE setSpecularPower
               BINDABLE bindableSpecularPower NOTIFY specularPowerChanged)

    // === Rendering Properties ===
    Q_PROPERTY(int renderingMode READ renderingMode WRITE setRenderingMode
               BINDABLE bindableRenderingMode NOTIFY renderingModeChanged)
    Q_PROPERTY(bool edgeVisibility READ edgeVisibility WRITE setEdgeVisibility
               BINDABLE bindableEdgeVisibility NOTIFY edgeVisibilityChanged)
    Q_PROPERTY(QVector3D edgeColor READ edgeColor WRITE setEdgeColor
               BINDABLE bindableEdgeColor NOTIFY edgeColorChanged)
    Q_PROPERTY(float lineWidth READ lineWidth WRITE setLineWidth
               BINDABLE bindableLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(int representation READ representation WRITE setRepresentation
               BINDABLE bindableRepresentation NOTIFY representationChanged)

    // === Material Preset ===
    Q_PROPERTY(int materialPreset READ materialPreset WRITE setMaterialPreset
               BINDABLE bindableMaterialPreset NOTIFY materialPresetChanged)

    QML_ELEMENT
    QML_SINGLETON

public:
    // Singleton pattern
    static SelectionBridge* instance();

    // QML singleton provider
    static SelectionBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // Property accessors
    QVariant selectedModel() const;
    bool hasSelection() const;
    int selectionCount() const;
    bool hasOrientationWidget() const;
    bool hasScaleWidget() const;
    bool hasTranslateWidget() const;
    bool hasMirrorWidget() const;

    // C++ interface for updating selection
    void setSelectedObjects(const std::vector<DBInstanceID>& selectedIds);
    void clearSelection();

    // Update widget status - called by widget handler
    void updateOrientationWidgetStatus();
    void updateScaleWidgetStatus();
    void updateTranslateWidgetStatus();
    void updateMirrorWidgetStatus();

    // Set mirror widget status
    void setMirrorWidgetStatus(bool active);

    // Get the first selected ID (for single selection)
    DBInstanceID selectedId() const;

    // QML-accessible version that returns int
    int getSelectedId() const;

    // Get all selected IDs (for multi-selection)
    const std::vector<DBInstanceID>& getSelectedIds() const;

    // === Material Property Getters ===
    QVector3D diffuseColor() const;
    QVector3D ambientColor() const;
    QVector3D specularColor() const;
    float metallic() const;
    float roughness() const;
    float materialOpacity() const;
    float ambient() const;
    float diffuse() const;
    float specular() const;
    float specularPower() const;
    int renderingMode() const;
    bool edgeVisibility() const;
    QVector3D edgeColor() const;
    float lineWidth() const;
    int representation() const;
    int materialPreset() const;

    // === Material Property Setters ===
    void setDiffuseColor(const QVector3D& color);
    void setAmbientColor(const QVector3D& color);
    void setSpecularColor(const QVector3D& color);
    void setMetallic(float metallic);
    void setRoughness(float roughness);
    void setMaterialOpacity(float opacity);
    void setAmbient(float ambient);
    void setDiffuse(float diffuse);
    void setSpecular(float specular);
    void setSpecularPower(float power);
    void setRenderingMode(int mode);
    void setEdgeVisibility(bool visible);
    void setEdgeColor(const QVector3D& color);
    void setLineWidth(float width);
    void setRepresentation(int representation);
    void setMaterialPreset(int preset);

    // === Bindables ===
    QBindable<QVector3D> bindableDiffuseColor();
    QBindable<QVector3D> bindableAmbientColor();
    QBindable<QVector3D> bindableSpecularColor();
    QBindable<float> bindableMetallic();
    QBindable<float> bindableRoughness();
    QBindable<float> bindableMaterialOpacity();
    QBindable<float> bindableAmbient();
    QBindable<float> bindableDiffuse();
    QBindable<float> bindableSpecular();
    QBindable<float> bindableSpecularPower();
    QBindable<int> bindableRenderingMode();
    QBindable<bool> bindableEdgeVisibility();
    QBindable<QVector3D> bindableEdgeColor();
    QBindable<float> bindableLineWidth();
    QBindable<int> bindableRepresentation();
    QBindable<int> bindableMaterialPreset();

signals:
    // Selection signals
    void selectedModelChanged();
    void hasSelectionChanged();
    void selectionCountChanged();
    void selectionChanged(const QVariantList& selectedIds);
    void hasOrientationWidgetChanged();
    void hasScaleWidgetChanged();
    void hasTranslateWidgetChanged();
    void hasMirrorWidgetChanged();

    // Material property signals
    void diffuseColorChanged();
    void ambientColorChanged();
    void specularColorChanged();
    void metallicChanged();
    void roughnessChanged();
    void materialOpacityChanged();
    void ambientChanged();
    void diffuseChanged();
    void specularChanged();
    void specularPowerChanged();
    void renderingModeChanged();
    void edgeVisibilityChanged();
    void edgeColorChanged();
    void lineWidthChanged();
    void representationChanged();
    void materialPresetChanged();

private:
    SelectionBridge(QObject* parent = nullptr);
    ~SelectionBridge() = default;

    // Disable copy and move
    SelectionBridge(const SelectionBridge&) = delete;
    SelectionBridge& operator=(const SelectionBridge&) = delete;
    SelectionBridge(SelectionBridge&&) = delete;
    SelectionBridge& operator=(SelectionBridge&&) = delete;

    // Member variables
    std::vector<DBInstanceID> m_selectedIds;
    mutable QVariant m_cachedSelectedModel;
    mutable bool m_cacheValid = false;
    bool m_hasMirrorWidget = false;  // 跟踪镜像 widget 是否被激活

    // Material property bindables
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, QVector3D, m_diffuseColor, &SelectionBridge::diffuseColorChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, QVector3D, m_ambientColor, &SelectionBridge::ambientColorChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, QVector3D, m_specularColor, &SelectionBridge::specularColorChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, float, m_metallic, &SelectionBridge::metallicChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, float, m_roughness, &SelectionBridge::roughnessChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, float, m_materialOpacity, &SelectionBridge::materialOpacityChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, float, m_ambient, &SelectionBridge::ambientChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, float, m_diffuse, &SelectionBridge::diffuseChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, float, m_specular, &SelectionBridge::specularChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, float, m_specularPower, &SelectionBridge::specularPowerChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, int, m_renderingMode, &SelectionBridge::renderingModeChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, bool, m_edgeVisibility, &SelectionBridge::edgeVisibilityChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, QVector3D, m_edgeColor, &SelectionBridge::edgeColorChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, float, m_lineWidth, &SelectionBridge::lineWidthChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, int, m_representation, &SelectionBridge::representationChanged)
    Q_OBJECT_BINDABLE_PROPERTY(SelectionBridge, int, m_materialPreset, &SelectionBridge::materialPresetChanged)

    // Helper methods
    void updateCache() const;

    /**
     * @brief Get the first selected actor (for material editing)
     * @return Shared pointer to the first selected ActorDB, or nullptr if none
     */
    std::shared_ptr<ActorDB> getSelectedActor() const;

    /**
     * @brief Update all material properties from the selected actor
     */
    void updateMaterialProperties();

    /**
     * @brief Clear all material properties (when no actor is selected)
     */
    void clearMaterialProperties();

    /**
     * @brief Update a specific material property
     * @param propertyName The name of the property that changed
     */
    void updateSpecificMaterialProperty(const std::string& propertyName);

protected:
    // Override from PropertyBridgeBase
    void onPropertyChanged(const DBInstanceID& id,
                           const std::string& propertyName,
                           const std::any& newValue) override;
    void onObjectCreated(const DBInstanceID& id) override;
    void onObjectDeleted(const DBInstanceID& id) override;
};