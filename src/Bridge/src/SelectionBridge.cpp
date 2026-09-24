#include "SelectionBridge.hpp"
#include "BridgeRegistration.hpp"
#include <DocumentManager.hpp>
#include <ActorDB.hpp>
#include "Foundation/Log.h"
#include <QQmlEngine>
#include <ModelOrientationWidgetDB.hpp>
#include <ModelScaleWidgetDB.hpp>
#include <ModelTranslateWidgetDB.hpp>
#include <transdb.h>
#include <SystemTypes.hpp>  // For typeIdToString()

SelectionBridge::SelectionBridge(QObject* parent)
    : PropertyBridgeBase(parent) {
    // LOG_INFO("SelectionBridge created");

    // Initialize material properties with default values
    m_diffuseColor = QVector3D(0.8f, 0.8f, 0.8f);
    m_ambientColor = QVector3D(0.2f, 0.2f, 0.2f);
    m_specularColor = QVector3D(1.0f, 1.0f, 1.0f);
    m_metallic = 0.0f;
    m_roughness = 0.5f;
    m_materialOpacity = 1.0f;
    m_ambient = 0.3f;
    m_diffuse = 0.8f;
    m_specular = 0.5f;
    m_specularPower = 10.0f;
    m_renderingMode = 1;  // PBR
    m_edgeVisibility = false;
    m_edgeColor = QVector3D(0.0f, 0.0f, 0.0f);
    m_lineWidth = 1.0f;
    m_representation = 2;  // Surface
    m_materialPreset = 0;
}

SelectionBridge* SelectionBridge::instance() {
    static SelectionBridge* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new SelectionBridge();
    }
    return s_instance;
}

SelectionBridge* SelectionBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(jsEngine)

    auto* bridge = instance();

    // Set ownership to C++ to prevent QML from deleting the singleton
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);

    return bridge;
}

QVariant SelectionBridge::selectedModel() const {
    if (!m_cacheValid) {
        updateCache();
    }
    return m_cachedSelectedModel;
}

bool SelectionBridge::hasSelection() const {
    return !m_selectedIds.empty();
}

int SelectionBridge::selectionCount() const {
    return static_cast<int>(m_selectedIds.size());
}

void SelectionBridge::setSelectedObjects(const std::vector<DBInstanceID>& selectedIds) {
    if (m_selectedIds == selectedIds) {
        return; // No change
    }

    LOG_DEBUG("SelectionBridge: Selection changed, {} objects selected", selectedIds.size());

    // 找出被取消选中的模型
    std::vector<DBInstanceID> deselectedModels;
    for (const auto& prevId : m_selectedIds) {
        bool stillSelected = false;
        for (const auto& currId : selectedIds) {
            if (prevId == currId) {
                stillSelected = true;
                break;
            }
        }
        if (!stillSelected) {
            deselectedModels.push_back(prevId);
        }
    }

    // 删除被取消选中模型的所有Widget (Orientation, Scale, Translate)
    if (!deselectedModels.empty()) {
        LOG_INFO("🗑️ SelectionBridge: Cleaning up widgets for {} deselected models", deselectedModels.size());
        TransactionGuard guard("Remove Model Widgets");

        auto docManager = DocumentManager::instance();

        // 清理 ModelOrientationWidget
        auto orientationWidgetIds = docManager->getAllDBInstanceIds(TypeID::MODEL_ORIENTATION_WIDGET_DB);
        for (const auto& widgetId : orientationWidgetIds) {
            auto instance = docManager->getDBInstance(widgetId);
            if (auto widget = std::dynamic_pointer_cast<ModelOrientationWidgetDB>(instance)) {
                auto linkedActorId = widget->getLinkedActorID();
                for (const auto& deselectedId : deselectedModels) {
                    if (linkedActorId == deselectedId) {
                        docManager->unregisterDBInstance(widgetId);
                        break;
                    }
                }
            }
        }

        // 清理 ModelScaleWidget
        auto scaleWidgetIds = docManager->getAllDBInstanceIds(TypeID::MODEL_SCALE_WIDGET_DB);
        LOG_DEBUG("SelectionBridge: Found {} scale widgets to check for cleanup", scaleWidgetIds.size());
        for (const auto& widgetId : scaleWidgetIds) {
            auto instance = docManager->getDBInstance(widgetId);
            if (auto widget = std::dynamic_pointer_cast<ModelScaleWidgetDB>(instance)) {
                auto linkedActorId = widget->getLinkedActorID();
                for (const auto& deselectedId : deselectedModels) {
                    if (linkedActorId == deselectedId) {
                        LOG_INFO("🗑️ SelectionBridge: Removing ModelScaleWidget {} linked to deselected actor {}", widgetId.getValue(), deselectedId.getValue());
                        docManager->unregisterDBInstance(widgetId);
                        break;
                    }
                }
            }
        }

        // 清理 ModelTranslateWidget
        auto translateWidgetIds = docManager->getAllDBInstanceIds(TypeID::MODEL_TRANSLATE_WIDGET_DB);
        for (const auto& widgetId : translateWidgetIds) {
            auto instance = docManager->getDBInstance(widgetId);
            if (auto widget = std::dynamic_pointer_cast<ModelTranslateWidgetDB>(instance)) {
                auto linkedActorId = widget->getLinkedActorID();
                for (const auto& deselectedId : deselectedModels) {
                    if (linkedActorId == deselectedId) {
                        docManager->unregisterDBInstance(widgetId);
                        break;
                    }
                }
            }
        }
    }

    m_selectedIds = selectedIds;
    m_cacheValid = false;

    // 更新材质属性
    updateMaterialProperties();

    // 监听选中对象的属性变化
    clearListeners();
    if (!m_selectedIds.empty()) {
        startListeningTo(m_selectedIds.front());
    }

    // Convert to QVariantList for QML signal
    QVariantList qmlIds;
    for (const auto& id : selectedIds) {
        qmlIds.append(QVariant::fromValue(id.getValue()));
    }

    // Emit all change signals
    emit selectedModelChanged();
    emit hasSelectionChanged();
    emit selectionCountChanged();
    emit selectionChanged(qmlIds);

    // 更新widget状态
    LOG_INFO("📡 SelectionBridge: Emitting widget status change signals - hasScaleWidget: {}", hasScaleWidget());
    emit hasOrientationWidgetChanged();
    emit hasScaleWidgetChanged();
    emit hasTranslateWidgetChanged();
}

void SelectionBridge::clearSelection() {
    if (m_selectedIds.empty()) {
        return; // Already empty
    }

    LOG_INFO("🧹 SelectionBridge: Selection cleared");

    // 删除所有之前选中模型的所有Widget (Orientation, Scale, Translate)
    TransactionGuard guard("Remove Model Widgets");

    auto docManager = DocumentManager::instance();

    // 清理 ModelOrientationWidget
    auto orientationWidgetIds = docManager->getAllDBInstanceIds(TypeID::MODEL_ORIENTATION_WIDGET_DB);
    for (const auto& widgetId : orientationWidgetIds) {
        auto instance = docManager->getDBInstance(widgetId);
        if (auto widget = std::dynamic_pointer_cast<ModelOrientationWidgetDB>(instance)) {
            auto linkedActorId = widget->getLinkedActorID();
            for (const auto& prevId : m_selectedIds) {
                if (linkedActorId == prevId) {
                    docManager->unregisterDBInstance(widgetId);
                    break;
                }
            }
        }
    }

    // 清理 ModelScaleWidget
    auto scaleWidgetIds = docManager->getAllDBInstanceIds(TypeID::MODEL_SCALE_WIDGET_DB);
    for (const auto& widgetId : scaleWidgetIds) {
        auto instance = docManager->getDBInstance(widgetId);
        if (auto widget = std::dynamic_pointer_cast<ModelScaleWidgetDB>(instance)) {
            auto linkedActorId = widget->getLinkedActorID();
            for (const auto& prevId : m_selectedIds) {
                if (linkedActorId == prevId) {
                    docManager->unregisterDBInstance(widgetId);
                    break;
                }
            }
        }
    }

    // 清理 ModelTranslateWidget
    auto translateWidgetIds = docManager->getAllDBInstanceIds(TypeID::MODEL_TRANSLATE_WIDGET_DB);
    for (const auto& widgetId : translateWidgetIds) {
        auto instance = docManager->getDBInstance(widgetId);
        if (auto widget = std::dynamic_pointer_cast<ModelTranslateWidgetDB>(instance)) {
            auto linkedActorId = widget->getLinkedActorID();
            for (const auto& prevId : m_selectedIds) {
                if (linkedActorId == prevId) {
                    docManager->unregisterDBInstance(widgetId);
                    break;
                }
            }
        }
    }


    m_selectedIds.clear();
    m_cacheValid = false;

    // 清除镜像 widget 状态
    m_hasMirrorWidget = false;

    // Emit all change signals
    emit selectedModelChanged();
    emit hasSelectionChanged();
    emit selectionCountChanged();
    emit selectionChanged(QVariantList());

    // 更新widget状态
    emit hasOrientationWidgetChanged();
    emit hasScaleWidgetChanged();
    emit hasTranslateWidgetChanged();
    emit hasMirrorWidgetChanged();
}

DBInstanceID SelectionBridge::selectedId() const {
    if (m_selectedIds.empty()) {
        return DBInstanceID(0); // Invalid ID
    }
    return m_selectedIds.front(); // Return the first selected ID
}

int SelectionBridge::getSelectedId() const {
    if (m_selectedIds.empty()) {
        return 0; // Invalid ID
    }
    return m_selectedIds.front().getValue();
}

const std::vector<DBInstanceID>& SelectionBridge::getSelectedIds() const {
    return m_selectedIds;
}

void SelectionBridge::updateCache() const {
    if (m_selectedIds.empty()) {
        m_cachedSelectedModel = QVariant();
        m_cacheValid = true;
        return;
    }

    // Get the first selected object
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_WARN("SelectionBridge: DocumentManager not available");
        m_cachedSelectedModel = QVariant();
        m_cacheValid = true;
        return;
    }

    // Get the DB instance for the first selected ID
    auto dbInstance = docManager->getDBInstance(m_selectedIds.front());
    if (!dbInstance) {
        LOG_WARN("SelectionBridge: Failed to get DB instance for ID {}", m_selectedIds.front().getValue());
        m_cachedSelectedModel = QVariant();
        m_cacheValid = true;
        return;
    }

    // For QML access, we need to cast to ActorDB if it's an actor
    auto actor = std::dynamic_pointer_cast<ActorDB>(dbInstance);
    if (actor) {
        // Create a QVariantMap with all the properties QML needs
        QVariantMap modelData;

        // Basic info
        modelData["objectId"] = QString::fromStdString(actor->getDisplayName());
        // 使用架构自带的 TypeID 系统获取类型名，而不是 RTTR
        // 这样避免了 RTTR 的静态初始化顺序问题（Windows DLL 特有）
        modelData["className"] = typeIdToString(actor->getTypeID());

        // Transform properties
        auto position = actor->getPosition();
        QVariantMap posMap;
        posMap["x"] = position.x;
        posMap["y"] = position.y;
        posMap["z"] = position.z;
        modelData["position"] = posMap;

        auto rotation = actor->getRotation();
        QVariantMap rotMap;
        rotMap["x"] = rotation.x;
        rotMap["y"] = rotation.y;
        rotMap["z"] = rotation.z;
        modelData["rotation"] = rotMap;

        auto scale = actor->getScale();
        QVariantMap scaleMap;
        scaleMap["x"] = scale.x;
        scaleMap["y"] = scale.y;
        scaleMap["z"] = scale.z;
        modelData["scale"] = scaleMap;

        // Appearance
        modelData["opacity"] = actor->getOpacity();

        // Material
        auto material = actor->getMaterial();
        if (material) {
            QVariantMap materialMap;
            auto diffuseColor = material->getDiffuseColor();
            QVariantMap colorMap;
            colorMap["x"] = diffuseColor.x;
            colorMap["y"] = diffuseColor.y;
            colorMap["z"] = diffuseColor.z;
            materialMap["diffuseColor"] = colorMap;
            modelData["material"] = materialMap;
        }

        // Bounds
        const auto bounds = actor->localBounds();
        QVariantMap boundsMap;
        boundsMap["valid"] = bounds.valid;

        LOG_DEBUG("SelectionBridge: Bounds for actor {}: valid={}, min=({},{},{}), max=({},{},{})",
                  actor->getDisplayName(), bounds.valid,
                  bounds.min.x, bounds.min.y, bounds.min.z,
                  bounds.max.x, bounds.max.y, bounds.max.z);

        if (bounds.valid) {
            auto size = bounds.getSize();
            QVariantMap sizeMap;
            sizeMap["x"] = size.x;
            sizeMap["y"] = size.y;
            sizeMap["z"] = size.z;
            boundsMap["size"] = sizeMap;

            auto center = bounds.getCenter();
            QVariantMap centerMap;
            centerMap["x"] = center.x;
            centerMap["y"] = center.y;
            centerMap["z"] = center.z;
            boundsMap["center"] = centerMap;

            LOG_DEBUG("SelectionBridge: Bounds size=({},{},{}), center=({},{},{})",
                      size.x, size.y, size.z,
                      center.x, center.y, center.z);
        } else {
            // Provide empty objects even if bounds are not valid
            QVariantMap sizeMap;
            sizeMap["x"] = 0.0f;
            sizeMap["y"] = 0.0f;
            sizeMap["z"] = 0.0f;
            boundsMap["size"] = sizeMap;

            QVariantMap centerMap;
            centerMap["x"] = 0.0f;
            centerMap["y"] = 0.0f;
            centerMap["z"] = 0.0f;
            boundsMap["center"] = centerMap;
        }
        modelData["bounds"] = boundsMap;

        m_cachedSelectedModel = modelData;
        LOG_DEBUG("SelectionBridge: Selected actor of type {} converted to QVariantMap with {} properties",
                  dbInstance->getDisplayName(), modelData.size());
    } else {
        // Not an actor, just store basic info
        QVariantMap modelData;
        modelData["objectId"] = QString::fromStdString(dbInstance->getDisplayName());
        // 使用架构自带的 TypeID 系统获取类型名
        modelData["className"] = typeIdToString(dbInstance->getTypeID());

        m_cachedSelectedModel = modelData;
        // LOG_INFO("SelectionBridge: Selected non-actor object of type {} converted to QVariantMap",
        //          dbInstance->getDisplayName());
    }

    m_cacheValid = true;
}

bool SelectionBridge::hasOrientationWidget() const {
    // 如果没有选中的对象，则肯定没有widget
    if (m_selectedIds.empty()) {
        LOG_DEBUG("SelectionBridge::hasOrientationWidget - No selected objects");
        return false;
    }

    // 获取DocumentManager实例
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_WARN("SelectionBridge::hasOrientationWidget - DocumentManager not available");
        return false;
    }

    // 获取所有的ModelOrientationWidget
    auto widgetIds = docManager->getAllDBInstanceIds(TypeID::MODEL_ORIENTATION_WIDGET_DB);
    LOG_DEBUG("SelectionBridge::hasOrientationWidget - Found {} orientation widgets", widgetIds.size());

    // 检查是否有widget链接到当前选中的模型
    for (const auto& widgetId : widgetIds) {
        auto instance = docManager->getDBInstance(widgetId);
        if (auto widget = std::dynamic_pointer_cast<ModelOrientationWidgetDB>(instance)) {
            auto linkedActorId = widget->getLinkedActorID();
            LOG_DEBUG("SelectionBridge::hasOrientationWidget - Widget {} linked to actor {}",
                     widgetId.getValue(), linkedActorId.getValue());
            // 检查是否有widget链接到任何选中的对象
            for (const auto& selectedId : m_selectedIds) {
                if (linkedActorId == selectedId) {
                    LOG_DEBUG("SelectionBridge::hasOrientationWidget - Found widget for selected actor {}", selectedId.getValue());
                    return true;
                }
            }
        }
    }

    LOG_DEBUG("SelectionBridge::hasOrientationWidget - No widget found for selected actors");
    return false;
}

void SelectionBridge::updateOrientationWidgetStatus() {
    // 发送信号通知QML widget状态已经改变
    LOG_DEBUG("SelectionBridge::updateOrientationWidgetStatus - hasOrientationWidget: {}", hasOrientationWidget());
    emit hasOrientationWidgetChanged();
}

bool SelectionBridge::hasScaleWidget() const {
    // 如果没有选中的对象，则肯定没有widget
    if (m_selectedIds.empty()) {
        LOG_DEBUG("SelectionBridge::hasScaleWidget - No selected objects");
        return false;
    }

    // 获取DocumentManager实例
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_WARN("SelectionBridge::hasScaleWidget - DocumentManager not available");
        return false;
    }

    // 获取所有的ModelScaleWidget
    auto widgetIds = docManager->getAllDBInstanceIds(TypeID::MODEL_SCALE_WIDGET_DB);
    LOG_DEBUG("SelectionBridge::hasScaleWidget - Found {} scale widgets", widgetIds.size());

    // 检查是否有widget链接到当前选中的模型
    for (const auto& widgetId : widgetIds) {
        auto instance = docManager->getDBInstance(widgetId);
        if (auto widget = std::dynamic_pointer_cast<ModelScaleWidgetDB>(instance)) {
            auto linkedActorId = widget->getLinkedActorID();
            LOG_DEBUG("SelectionBridge::hasScaleWidget - Widget {} linked to actor {}",
                     widgetId.getValue(), linkedActorId.getValue());

            // 检查是否有widget链接到任何选中的对象
            for (const auto& selectedId : m_selectedIds) {
                if (linkedActorId == selectedId) {
                    LOG_DEBUG("SelectionBridge::hasScaleWidget - Found widget for selected actor {}", selectedId.getValue());
                    return true;
                }
            }
        }
    }

    LOG_DEBUG("SelectionBridge::hasScaleWidget - No widget found for selected actors");
    return false;
}

void SelectionBridge::updateScaleWidgetStatus() {
    // 发送信号通知QML widget状态已经改变
    LOG_DEBUG("SelectionBridge::updateScaleWidgetStatus - hasScaleWidget: {}", hasScaleWidget());
    emit hasScaleWidgetChanged();
}

bool SelectionBridge::hasTranslateWidget() const {
    // 如果没有选中的对象，则肯定没有widget
    if (m_selectedIds.empty()) {
        LOG_DEBUG("SelectionBridge::hasTranslateWidget - No selected objects");
        return false;
    }

    // 获取DocumentManager实例
    auto docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_WARN("SelectionBridge::hasTranslateWidget - DocumentManager not available");
        return false;
    }

    // 获取所有的ModelTranslateWidget
    auto widgetIds = docManager->getAllDBInstanceIds(TypeID::MODEL_TRANSLATE_WIDGET_DB);
    LOG_DEBUG("SelectionBridge::hasTranslateWidget - Found {} translate widgets", widgetIds.size());

    // 检查是否有widget链接到当前选中的模型
    for (const auto& widgetId : widgetIds) {
        auto instance = docManager->getDBInstance(widgetId);
        if (auto widget = std::dynamic_pointer_cast<ModelTranslateWidgetDB>(instance)) {
            auto linkedActorId = widget->getLinkedActorID();
            LOG_DEBUG("SelectionBridge::hasTranslateWidget - Widget {} linked to actor {}",
                     widgetId.getValue(), linkedActorId.getValue());

            // 检查是否有widget链接到任何选中的对象
            for (const auto& selectedId : m_selectedIds) {
                if (linkedActorId == selectedId) {
                    LOG_DEBUG("SelectionBridge::hasTranslateWidget - Found widget for selected actor {}", selectedId.getValue());
                    return true;
                }
            }
        }
    }

    LOG_DEBUG("SelectionBridge::hasTranslateWidget - No widget found for selected actors");
    return false;
}

void SelectionBridge::updateTranslateWidgetStatus() {
    // 发送信号通知QML widget状态已经改变
    LOG_DEBUG("SelectionBridge::updateTranslateWidgetStatus - hasTranslateWidget: {}", hasTranslateWidget());
    emit hasTranslateWidgetChanged();
}

bool SelectionBridge::hasMirrorWidget() const {
    // 镜像功能不需要创建交互式 widget（如旋转、缩放那样的控件）
    // 但为了显示镜像选项面板，我们使用成员变量来跟踪状态
    return m_hasMirrorWidget;
}

void SelectionBridge::updateMirrorWidgetStatus() {
    // 发送信号通知QML widget状态已经改变
    LOG_DEBUG("SelectionBridge::updateMirrorWidgetStatus - hasMirrorWidget: {}", hasMirrorWidget());
    emit hasMirrorWidgetChanged();
}

void SelectionBridge::setMirrorWidgetStatus(bool active) {
    if (m_hasMirrorWidget != active) {
        m_hasMirrorWidget = active;
        LOG_DEBUG("SelectionBridge::setMirrorWidgetStatus - Setting hasMirrorWidget to: {}", active);
        emit hasMirrorWidgetChanged();
    }
}

// ============================================================================
// Material Property Getters
// ============================================================================

QVector3D SelectionBridge::diffuseColor() const {
    return m_diffuseColor;
}

QVector3D SelectionBridge::ambientColor() const {
    return m_ambientColor;
}

QVector3D SelectionBridge::specularColor() const {
    return m_specularColor;
}

float SelectionBridge::metallic() const {
    return m_metallic;
}

float SelectionBridge::roughness() const {
    return m_roughness;
}

float SelectionBridge::materialOpacity() const {
    return m_materialOpacity;
}

float SelectionBridge::ambient() const {
    return m_ambient;
}

float SelectionBridge::diffuse() const {
    return m_diffuse;
}

float SelectionBridge::specular() const {
    return m_specular;
}

float SelectionBridge::specularPower() const {
    return m_specularPower;
}

int SelectionBridge::renderingMode() const {
    return m_renderingMode;
}

bool SelectionBridge::edgeVisibility() const {
    return m_edgeVisibility;
}

QVector3D SelectionBridge::edgeColor() const {
    return m_edgeColor;
}

float SelectionBridge::lineWidth() const {
    return m_lineWidth;
}

int SelectionBridge::representation() const {
    return m_representation;
}

int SelectionBridge::materialPreset() const {
    return m_materialPreset;
}

// ============================================================================
// Material Property Setters (with Transaction Support)
// ============================================================================

void SelectionBridge::setDiffuseColor(const QVector3D& color) {
    if (m_diffuseColor == color) return;

    auto actor = getSelectedActor();
    if (!actor) {
        LOG_WARN("SelectionBridge::setDiffuseColor - No selected actor");
        return;
    }

    executeInTransaction("Set Diffuse Color", [this, actor, color]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setDiffuseColor(Vector3(color.x(), color.y(), color.z()));
            m_diffuseColor = color;
        }
    });
}

void SelectionBridge::setAmbientColor(const QVector3D& color) {
    if (m_ambientColor == color) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Ambient Color", [this, actor, color]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setAmbientColor(Vector3(color.x(), color.y(), color.z()));
            m_ambientColor = color;
        }
    });
}

void SelectionBridge::setSpecularColor(const QVector3D& color) {
    if (m_specularColor == color) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Specular Color", [this, actor, color]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setSpecularColor(Vector3(color.x(), color.y(), color.z()));
            m_specularColor = color;
        }
    });
}

void SelectionBridge::setMetallic(float metallic) {
    if (m_metallic == metallic) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Metallic", [this, actor, metallic]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setMetallic(metallic);
            m_metallic = metallic;
        }
    });
}

void SelectionBridge::setRoughness(float roughness) {
    if (m_roughness == roughness) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Roughness", [this, actor, roughness]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setRoughness(roughness);
            m_roughness = roughness;
        }
    });
}

void SelectionBridge::setMaterialOpacity(float opacity) {
    if (m_materialOpacity == opacity) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Material Opacity", [this, actor, opacity]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setOpacity(opacity);
            m_materialOpacity = opacity;
        }
    });
}

void SelectionBridge::setAmbient(float ambient) {
    LOG_DEBUG("SelectionBridge::setAmbient called with value: {:.3f}, current: {:.3f}", ambient, m_ambient.value());

    if (m_ambient == ambient) return;

    auto actor = getSelectedActor();
    if (!actor) {
        LOG_WARN("SelectionBridge::setAmbient - No selected actor");
        return;
    }

    executeInTransaction("Set Ambient", [this, actor, ambient]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setAmbient(ambient);
            m_ambient = ambient;
            LOG_INFO("SelectionBridge::setAmbient - Set ambient to {:.3f} for actor {}", ambient, actor->getDisplayName());
        }
    });
}

void SelectionBridge::setDiffuse(float diffuse) {
    if (m_diffuse == diffuse) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Diffuse", [this, actor, diffuse]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setDiffuse(diffuse);
            m_diffuse = diffuse;
        }
    });
}

void SelectionBridge::setSpecular(float specular) {
    if (m_specular == specular) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Specular", [this, actor, specular]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setSpecular(specular);
            m_specular = specular;
        }
    });
}

void SelectionBridge::setSpecularPower(float power) {
    if (m_specularPower == power) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Specular Power", [this, actor, power]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setSpecularPower(power);
            m_specularPower = power;
        }
    });
}

void SelectionBridge::setRenderingMode(int mode) {
    if (m_renderingMode == mode) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Rendering Mode", [this, actor, mode]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setRenderingMode(mode);
            m_renderingMode = mode;
        }
    });
}

void SelectionBridge::setEdgeVisibility(bool visible) {
    if (m_edgeVisibility == visible) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Edge Visibility", [this, actor, visible]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setEdgeVisibility(visible);
            m_edgeVisibility = visible;
        }
    });
}

void SelectionBridge::setEdgeColor(const QVector3D& color) {
    if (m_edgeColor == color) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Edge Color", [this, actor, color]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setEdgeColor(Vector3(color.x(), color.y(), color.z()));
            m_edgeColor = color;
        }
    });
}

void SelectionBridge::setLineWidth(float width) {
    if (m_lineWidth == width) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Line Width", [this, actor, width]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setLineWidth(width);
            m_lineWidth = width;
        }
    });
}

void SelectionBridge::setRepresentation(int representation) {
    if (m_representation == representation) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Representation", [this, actor, representation]() {
        auto material = actor->getMaterial();
        if (material) {
            material->setRepresentation(representation);
            m_representation = representation;
        }
    });
}

void SelectionBridge::setMaterialPreset(int preset) {
    if (m_materialPreset == preset) return;

    auto actor = getSelectedActor();
    if (!actor) return;

    executeInTransaction("Set Material Preset", [this, actor, preset]() {
        auto material = actor->getMaterial();
        if (material) {
            material->applyMaterialPreset(static_cast<MaterialDB::MaterialPreset>(preset));
            m_materialPreset = preset;
            // 预设会改变多个属性，需要刷新所有材质属性
            updateMaterialProperties();
        }
    });
}

// ============================================================================
// Bindable Accessors
// ============================================================================

QBindable<QVector3D> SelectionBridge::bindableDiffuseColor() {
    return &m_diffuseColor;
}

QBindable<QVector3D> SelectionBridge::bindableAmbientColor() {
    return &m_ambientColor;
}

QBindable<QVector3D> SelectionBridge::bindableSpecularColor() {
    return &m_specularColor;
}

QBindable<float> SelectionBridge::bindableMetallic() {
    return &m_metallic;
}

QBindable<float> SelectionBridge::bindableRoughness() {
    return &m_roughness;
}

QBindable<float> SelectionBridge::bindableMaterialOpacity() {
    return &m_materialOpacity;
}

QBindable<float> SelectionBridge::bindableAmbient() {
    return &m_ambient;
}

QBindable<float> SelectionBridge::bindableDiffuse() {
    return &m_diffuse;
}

QBindable<float> SelectionBridge::bindableSpecular() {
    return &m_specular;
}

QBindable<float> SelectionBridge::bindableSpecularPower() {
    return &m_specularPower;
}

QBindable<int> SelectionBridge::bindableRenderingMode() {
    return &m_renderingMode;
}

QBindable<bool> SelectionBridge::bindableEdgeVisibility() {
    return &m_edgeVisibility;
}

QBindable<QVector3D> SelectionBridge::bindableEdgeColor() {
    return &m_edgeColor;
}

QBindable<float> SelectionBridge::bindableLineWidth() {
    return &m_lineWidth;
}

QBindable<int> SelectionBridge::bindableRepresentation() {
    return &m_representation;
}

QBindable<int> SelectionBridge::bindableMaterialPreset() {
    return &m_materialPreset;
}

// ============================================================================
// Helper Methods
// ============================================================================

std::shared_ptr<ActorDB> SelectionBridge::getSelectedActor() const {
    if (m_selectedIds.empty()) {
        return nullptr;
    }

    auto docManager = DocumentManager::instance();
    if (!docManager) {
        return nullptr;
    }

    auto dbInstance = docManager->getDBInstance(m_selectedIds.front());
    return std::dynamic_pointer_cast<ActorDB>(dbInstance);
}

void SelectionBridge::updateMaterialProperties() {
    auto actor = getSelectedActor();
    if (!actor) {
        clearMaterialProperties();
        return;
    }

    auto material = actor->getMaterial();
    if (!material) {
        clearMaterialProperties();
        return;
    }

    // 从选中的 actor 的 material 中读取所有属性到本地缓存
    auto diffuse = material->getDiffuseColor();
    m_diffuseColor = QVector3D(diffuse.x, diffuse.y, diffuse.z);

    auto ambient = material->getAmbientColor();
    m_ambientColor = QVector3D(ambient.x, ambient.y, ambient.z);

    auto specular = material->getSpecularColor();
    m_specularColor = QVector3D(specular.x, specular.y, specular.z);

    m_metallic = material->getMetallic();
    m_roughness = material->getRoughness();
    m_materialOpacity = material->getOpacity();
    m_ambient = material->getAmbient();
    m_diffuse = material->getDiffuse();
    m_specular = material->getSpecular();
    m_specularPower = material->getSpecularPower();
    m_renderingMode = material->getRenderingMode();
    m_edgeVisibility = material->getEdgeVisibility();

    auto edgeCol = material->getEdgeColor();
    m_edgeColor = QVector3D(edgeCol.x, edgeCol.y, edgeCol.z);

    m_lineWidth = material->getLineWidth();
    m_representation = material->getRepresentation();

    LOG_DEBUG("SelectionBridge: Updated material properties from actor {}", actor->getDisplayName());
}

void SelectionBridge::clearMaterialProperties() {
    // 重置为默认值
    m_diffuseColor = QVector3D(0.8f, 0.8f, 0.8f);
    m_ambientColor = QVector3D(0.2f, 0.2f, 0.2f);
    m_specularColor = QVector3D(1.0f, 1.0f, 1.0f);
    m_metallic = 0.0f;
    m_roughness = 0.5f;
    m_materialOpacity = 1.0f;
    m_ambient = 0.3f;
    m_diffuse = 0.8f;
    m_specular = 0.5f;
    m_specularPower = 10.0f;
    m_renderingMode = 1;  // PBR
    m_edgeVisibility = false;
    m_edgeColor = QVector3D(0.0f, 0.0f, 0.0f);
    m_lineWidth = 1.0f;
    m_representation = 2;  // Surface
    m_materialPreset = 0;

    LOG_DEBUG("SelectionBridge: Cleared material properties");
}

void SelectionBridge::updateSpecificMaterialProperty(const std::string& propertyName) {
    auto actor = getSelectedActor();
    if (!actor) return;

    auto material = actor->getMaterial();
    if (!material) return;

    // 根据属性名更新对应的缓存值
    if (propertyName == "DiffuseColor") {
        auto diffuse = material->getDiffuseColor();
        m_diffuseColor = QVector3D(diffuse.x, diffuse.y, diffuse.z);
    } else if (propertyName == "AmbientColor") {
        auto ambient = material->getAmbientColor();
        m_ambientColor = QVector3D(ambient.x, ambient.y, ambient.z);
    } else if (propertyName == "SpecularColor") {
        auto specular = material->getSpecularColor();
        m_specularColor = QVector3D(specular.x, specular.y, specular.z);
    } else if (propertyName == "Metallic") {
        m_metallic = material->getMetallic();
    } else if (propertyName == "Roughness") {
        m_roughness = material->getRoughness();
    } else if (propertyName == "Opacity") {
        m_materialOpacity = material->getOpacity();
    } else if (propertyName == "Ambient") {
        m_ambient = material->getAmbient();
    } else if (propertyName == "Diffuse") {
        m_diffuse = material->getDiffuse();
    } else if (propertyName == "Specular") {
        m_specular = material->getSpecular();
    } else if (propertyName == "SpecularPower") {
        m_specularPower = material->getSpecularPower();
    } else if (propertyName == "RenderingMode") {
        m_renderingMode = material->getRenderingMode();
    } else if (propertyName == "EdgeVisibility") {
        m_edgeVisibility = material->getEdgeVisibility();
    } else if (propertyName == "EdgeColor") {
        auto edgeCol = material->getEdgeColor();
        m_edgeColor = QVector3D(edgeCol.x, edgeCol.y, edgeCol.z);
    } else if (propertyName == "LineWidth") {
        m_lineWidth = material->getLineWidth();
    } else if (propertyName == "Representation") {
        m_representation = material->getRepresentation();
    }

    LOG_DEBUG("SelectionBridge: Updated property {} from material", propertyName);
}

// ============================================================================
// PropertyBridgeBase Overrides
// ============================================================================

void SelectionBridge::onPropertyChanged(const DBInstanceID& id,
                                       const std::string& propertyName,
                                       const std::any& newValue) {
    LOG_DEBUG("SelectionBridge::onPropertyChanged - ID: {}, Property: {}", id.getValue(), propertyName);
    
    // 只处理选中对象的属性变化
    bool isSelected = false;
    for (const auto& selectedId : m_selectedIds) {
        if (selectedId == id) {
            isSelected = true;
            break;
        }
    }

    if (!isSelected) {
        LOG_DEBUG("SelectionBridge::onPropertyChanged - ID {} is not selected, ignoring", id.getValue());
        return;
    }

    LOG_DEBUG("SelectionBridge::onPropertyChanged - ID {} is selected, processing property: {}", id.getValue(), propertyName);

    // 如果是 Material 相关属性，更新本地缓存
    if (propertyName.find("Material.") == 0) {
        // 提取 Material 后面的属性名（例如 "Material.DiffuseColor" -> "DiffuseColor"）
        std::string materialProp = propertyName.substr(9);  // "Material." 长度为 9
        updateSpecificMaterialProperty(materialProp);
    } else {
        // 直接处理属性名（如 "Ambient", "Diffuse" 等）
        updateSpecificMaterialProperty(propertyName);
    }

    LOG_DEBUG("SelectionBridge: Property {} changed for selected object {}", propertyName, id.getValue());
}

void SelectionBridge::onObjectCreated(const DBInstanceID& id) {
    // 目前不需要特殊处理对象创建
    Q_UNUSED(id);
}

void SelectionBridge::onObjectDeleted(const DBInstanceID& id) {
    // 如果删除的是选中的对象，需要从选中列表中移除
    bool wasSelected = false;
    for (auto it = m_selectedIds.begin(); it != m_selectedIds.end(); ++it) {
        if (*it == id) {
            m_selectedIds.erase(it);
            wasSelected = true;
            break;
        }
    }

    if (wasSelected) {
        m_cacheValid = false;
        updateMaterialProperties();
        clearListeners();
        if (!m_selectedIds.empty()) {
            startListeningTo(m_selectedIds.front());
        }

        emit selectedModelChanged();
        emit hasSelectionChanged();
        emit selectionCountChanged();
        
        LOG_INFO("SelectionBridge: Deleted object {} was selected, updated selection", id.getValue());
    }
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    SelectionBridge, "SelectionBridge", &SelectionBridge::create)
