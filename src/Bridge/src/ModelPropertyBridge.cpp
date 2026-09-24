#include "../include/ModelPropertyBridge.hpp"
#include "BridgeRegistration.hpp"
#include "../include/SelectionBridge.hpp"
#include "../include/SliceSettingsBridge.hpp"
#include <ActionManager.hpp>
#include <DocumentManager.hpp>
#include <MaterialDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelObjectDB.hpp>
#include <ModelPartDB.hpp>
#include <ModelGraphUtil.hpp>
#include <TransactionManager.hpp>
#include "Foundation/Log.h"

#include <algorithm>

ModelPropertyBridge::ModelPropertyBridge(QObject* parent)
    : PropertyBridgeBase(parent) {
    // LOG_INFO("ModelPropertyBridge created");
    connectToSelectionBridge();
    connect(SliceSettingsBridge::instance(),
            &SliceSettingsBridge::filamentSlotsChanged,
            this, [this] {
                emit filamentSlotsChanged();
                if (const auto actor = getSelectedActor()) {
                    updateFromActor(actor);
                }
            });
}

ModelPropertyBridge::~ModelPropertyBridge() {
    // LOG_INFO("ModelPropertyBridge destroyed");
}

QObject* ModelPropertyBridge::create(QQmlEngine* engine, QJSEngine* scriptEngine) {
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    return new ModelPropertyBridge();
}

void ModelPropertyBridge::connectToSelectionBridge() {
    auto* selectionBridge = SelectionBridge::instance();
    if (selectionBridge) {
        connect(selectionBridge, &SelectionBridge::selectedModelChanged,
                this, [this, selectionBridge]() {
                    onSelectionChanged(selectionBridge->selectedId());
                });

        // Initialize with current selection
        onSelectionChanged(selectionBridge->selectedId());
    }
}

void ModelPropertyBridge::onSelectionChanged(const DBInstanceID& selectedId) {
    // LOG_INFO("ModelPropertyBridge::onSelectionChanged - ID: {}", selectedId.getValue());

    // Stop listening to previous selection
    if (m_selectedId.isValid()) {
        stopListeningTo(m_selectedId);
    }

    m_selectedId = selectedId;

    // Start listening to new selection
    if (m_selectedId.isValid()) {
        startListeningTo(m_selectedId);

        // Get the actor and update properties
        auto& docManager = *DocumentManager::instance();
        if (auto dbInstance = docManager.getDBInstance(m_selectedId)) {
            if (auto actor = std::dynamic_pointer_cast<ActorDB>(dbInstance)) {
                updateFromActor(actor);
            }
        }
    } else {
        clearProperties();
    }
}

void ModelPropertyBridge::updateFromActor(const std::shared_ptr<ActorDB>& actor) {
    if (!actor) {
        clearProperties();
        return;
    }

    m_hasSelectedModel = true;

    m_displayName = QString::fromStdString(actor->getDisplayName());
    const auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(actor);
    const auto object = instance ? instance->object() : nullptr;
    m_modelType = object && !object->getFileFormat().empty()
        ? QString::fromStdString(object->getFileFormat()).toUpper()
        : QStringLiteral("Model");

    // Transform properties
    auto pos = actor->getPosition();
    m_position = QVector3D(pos.x, pos.y, pos.z);

    auto rot = actor->getRotation();
    m_rotation = QVector3D(rot.x, rot.y, rot.z);

    auto scale = actor->getScale();
    m_scale = QVector3D(scale.x, scale.y, scale.z);

    // Material properties
    std::vector<std::shared_ptr<MaterialDB>> materials;
    if (instance) {
        for (const auto& part : instance->parts()) {
            if (part && part->getMaterial()) {
                materials.push_back(part->getMaterial());
            }
        }
    } else if (actor->getMaterial()) {
        materials.push_back(actor->getMaterial());
    }
    updateFilamentBinding(instance);
    updateModelStatistics(instance);
    if (!materials.empty()) {
        const Vector3 color = materials.front()->getDiffuseColor();
        const bool common = std::all_of(
            materials.begin() + 1, materials.end(),
            [&](const auto& material) {
                return material && material->getDiffuseColor() == color;
            });
        if (common) {
            m_diffuseColor = QVector3D(color.x, color.y, color.z);
        }
    }
    if (instance) {
        m_opacity = instance->getOpacity();
    } else if (!materials.empty()) {
        m_opacity = materials.front()->getOpacity();
    }

    // Bounds
    updateBounds(actor);

    // LOG_INFO("ModelPropertyBridge updated from actor ID: {}", m_selectedId.getValue());
}

void ModelPropertyBridge::clearProperties() {
    m_hasSelectedModel = false;
    m_modelType = QString();
    m_displayName = QString();
    m_position = QVector3D();
    m_rotation = QVector3D();
    m_scale = QVector3D(1, 1, 1);
    m_diffuseColor = QVector3D(1, 1, 1);
    m_opacity = 1.0f;
    if (m_filamentSlot != 0) {
        m_filamentSlot = 0;
        emit filamentBindingChanged();
    }
    if (m_partCount != 0 || m_vertexCount != 0 || m_triangleCount != 0) {
        m_partCount = 0;
        m_vertexCount = 0;
        m_triangleCount = 0;
        emit modelStatisticsChanged();
    }
    m_boundsValid = false;
    m_boundsCenter = QVector3D();
    m_boundsSize = QVector3D();
}

void ModelPropertyBridge::updateSpecificProperty(const std::string& propertyName) {
    auto actor = getSelectedActor();
    if (!actor) return;

    // Transform 变化时更新所有变换属性
    // Position/Rotation/Scale 都从 Transform 矩阵中派生
    if (propertyName == "Transform") {
        // 从 Transform 获取最新值
        auto pos = actor->getPosition();
        m_position = QVector3D(pos.x, pos.y, pos.z);

        auto rot = actor->getRotation();
        m_rotation = QVector3D(rot.x, rot.y, rot.z);

        auto scale = actor->getScale();
        m_scale = QVector3D(scale.x, scale.y, scale.z);
        updateBounds(actor);
    }
    else if (propertyName == ModelInstanceDB::kObjectRelation ||
             propertyName == ModelInstanceDB::kSlicingConfigRelation ||
             propertyName == "SlicingConfigDBId") {
        updateFromActor(actor);
    }
    else if (propertyName == "displayName") {
        m_displayName = QString::fromStdString(actor->getDisplayName());
    }
    else if (propertyName == "DiffuseColor" || propertyName == "Opacity" ||
             (propertyName.rfind("Part.", 0) == 0 &&
              propertyName.find("Material") != std::string::npos)) {
        // Material properties - need to check if material exists
        std::vector<std::shared_ptr<MaterialDB>> materials;
        if (const auto instance =
                std::dynamic_pointer_cast<ModelInstanceDB>(actor)) {
            for (const auto& part : instance->parts()) {
                if (part && part->getMaterial()) {
                    materials.push_back(part->getMaterial());
                }
            }
        } else if (actor->getMaterial()) {
            materials.push_back(actor->getMaterial());
        }
        if (!materials.empty()) {
            const Vector3 color = materials.front()->getDiffuseColor();
            const bool common = std::all_of(
                materials.begin() + 1, materials.end(),
                [&](const auto& material) {
                    return material && material->getDiffuseColor() == color;
                });
            if (common) {
                m_diffuseColor = QVector3D(color.x, color.y, color.z);
            }
        }
        if (const auto instance =
                std::dynamic_pointer_cast<ModelInstanceDB>(actor)) {
            m_opacity = instance->getOpacity();
        } else if (!materials.empty()) {
            m_opacity = materials.front()->getOpacity();
        }
    }
    else if (propertyName == "Bounds" || propertyName == "WorldBounds") {
        updateBounds(actor);
    }
    else {
        // 对于未知的属性，记录警告但不做全量更新
        LOG_WARN("Unknown property changed: '{}', no specific update performed", propertyName);
    }
}

void ModelPropertyBridge::onPropertyChanged(const DBInstanceID& id,
                                            const std::string& propertyName,
                                            const std::any& newValue) {
    if (id != m_selectedId) return;


    // 只更新变化的属性，而不是重新获取所有属性
    updateSpecificProperty(propertyName);
}

void ModelPropertyBridge::onObjectDeleted(const DBInstanceID& id) {
    if (id == m_selectedId) {
        m_selectedId = DBInstanceID(0);  // Set to invalid ID
        clearProperties();
    }
}

std::shared_ptr<ActorDB> ModelPropertyBridge::getSelectedActor() const {
    if (!m_selectedId.isValid()) {
        return nullptr;
    }

    auto& docManager = *DocumentManager::instance();
    if (auto dbInstance = docManager.getDBInstance(m_selectedId)) {
        return std::dynamic_pointer_cast<ActorDB>(dbInstance);
    }
    return nullptr;
}

// Property getters
bool ModelPropertyBridge::hasSelectedModel() const { return m_hasSelectedModel; }
QString ModelPropertyBridge::modelType() const { return m_modelType; }
QString ModelPropertyBridge::displayName() const { return m_displayName; }
QVector3D ModelPropertyBridge::position() const { return m_position; }
QVector3D ModelPropertyBridge::rotation() const { return m_rotation; }
QVector3D ModelPropertyBridge::scale() const { return m_scale; }
QVector3D ModelPropertyBridge::diffuseColor() const { return m_diffuseColor; }
float ModelPropertyBridge::opacity() const { return m_opacity; }
int ModelPropertyBridge::filamentSlot() const { return m_filamentSlot; }
QVariantList ModelPropertyBridge::filamentSlots() const {
    return SliceSettingsBridge::instance()->filamentSlots();
}
bool ModelPropertyBridge::boundsValid() const { return m_boundsValid; }
QVector3D ModelPropertyBridge::boundsCenter() const { return m_boundsCenter; }
QVector3D ModelPropertyBridge::boundsSize() const { return m_boundsSize; }
int ModelPropertyBridge::partCount() const { return m_partCount; }
qulonglong ModelPropertyBridge::vertexCount() const { return m_vertexCount; }
qulonglong ModelPropertyBridge::triangleCount() const {
    return m_triangleCount;
}

// Property setters using the base class executeInTransaction
void ModelPropertyBridge::setPosition(const QVector3D& pos) {
    if (m_position == pos) return;

    executeInTransaction("Set Position", [this, pos]() {
        if (auto actor = getSelectedActor()) {
            actor->setPosition(Vector3(pos.x(), pos.y(), pos.z()));
        }
    });
    m_position = pos;
}

void ModelPropertyBridge::setRotation(const QVector3D& rot) {
    if (m_rotation == rot) return;

    executeInTransaction("Set Rotation", [this, rot]() {
        if (auto actor = getSelectedActor()) {
            // 不能直接设置旋转，需要重置后按顺序旋转
            actor->resetRotation();
            actor->rotateX(rot.x());
            actor->rotateY(rot.y());
            actor->rotateZ(rot.z());
        }
    });
    m_rotation = rot;
}

void ModelPropertyBridge::setScale(const QVector3D& scale) {
    if (m_scale == scale) return;

    executeInTransaction("Set Scale", [this, scale]() {
        if (auto actor = getSelectedActor()) {
            actor->setScale(Vector3(scale.x(), scale.y(), scale.z()));
        }
    });
    m_scale = scale;
}

void ModelPropertyBridge::setDiffuseColor(const QVector3D& color) {
    if (m_diffuseColor == color && m_filamentSlot == 0) return;

    executeInTransaction("Set Diffuse Color", [this, color]() {
        if (auto actor = getSelectedActor()) {
            if (const auto instance =
                    std::dynamic_pointer_cast<ModelInstanceDB>(actor)) {
                const auto sourceParts = instance->parts();
                const auto unique =
                    ModelGraphUtil::ensureUniqueObjectForInstance(
                        instance->getDBInstanceID());
                if (!unique) {
                    LOG_ERROR(
                        "ModelPropertyBridge::setDiffuseColor - COW failed: {}",
                        unique.error);
                    return;
                }
                auto* document = DocumentManager::instance();
                if (document) {
                    if (const auto targetObject =
                            document->getDB<ModelObjectDB>(unique.objectId)) {
                        targetObject->setUseSourceAppearance(false);
                    }
                }
                for (const auto& sourcePart : sourceParts) {
                    if (!sourcePart || !document) continue;
                    const DBInstanceID targetPartId = unique.mapPart(
                        sourcePart->getDBInstanceID());
                    const auto targetPart =
                        document->getDB<ModelPartDB>(targetPartId);
                    if (!targetPart) continue;
                    targetPart->setFilamentBindingMode(static_cast<int>(
                        ModelFilamentBindingMode::InheritProjectDefault));
                    if (targetPart->getMaterial()) {
                        targetPart->getMaterial()->setDiffuseColor(
                            Vector3(color.x(), color.y(), color.z()));
                    }
                }
            } else if (actor->getMaterial()) {
                actor->getMaterial()->setDiffuseColor(
                    Vector3(color.x(), color.y(), color.z()));
            }
        }
    });
    m_diffuseColor = color;
    if (m_filamentSlot != 0) {
        m_filamentSlot = 0;
        emit filamentBindingChanged();
    }
}

void ModelPropertyBridge::updateFilamentBinding(
    const std::shared_ptr<ModelInstanceDB>& instance) {
    int projected = 0;
    bool initialized = false;
    if (instance) {
        for (const auto& part : instance->parts()) {
            if (!part) continue;
            const int value = part->getFilamentBindingMode() ==
                    static_cast<int>(ModelFilamentBindingMode::ExplicitSlot)
                ? part->getDefaultFilamentSlot()
                : 0;
            if (!initialized) {
                projected = value;
                initialized = true;
            } else if (projected != value) {
                projected = -1;
                break;
            }
        }
    }
    if (m_filamentSlot == projected) return;
    m_filamentSlot = projected;
    emit filamentBindingChanged();
}

void ModelPropertyBridge::updateBounds(
    const std::shared_ptr<ActorDB>& actor) {
    const auto bounds = actor ? actor->worldBounds() : ActorDB::BoundingBox{};
    m_boundsValid = bounds.valid;
    if (!bounds.valid) {
        m_boundsCenter = QVector3D();
        m_boundsSize = QVector3D();
        return;
    }
    const auto center = bounds.getCenter();
    const auto size = bounds.getSize();
    m_boundsCenter = QVector3D(center.x, center.y, center.z);
    m_boundsSize = QVector3D(size.x, size.y, size.z);
}

void ModelPropertyBridge::updateModelStatistics(
    const std::shared_ptr<ModelInstanceDB>& instance) {
    int parts = 0;
    qulonglong vertices = 0;
    qulonglong triangles = 0;
    if (instance) {
        for (const auto& part : instance->parts()) {
            if (!part) continue;
            ++parts;
            if (const auto geometry = part->geometry()) {
                vertices += static_cast<qulonglong>(geometry->vertexCount());
                triangles += static_cast<qulonglong>(geometry->triangleCount());
            }
        }
    }
    if (parts == m_partCount && vertices == m_vertexCount &&
        triangles == m_triangleCount) return;
    m_partCount = parts;
    m_vertexCount = vertices;
    m_triangleCount = triangles;
    emit modelStatisticsChanged();
}

bool ModelPropertyBridge::assignFilamentSlot(int slot) {
    if (!m_selectedId.isValid() || slot < 1) return false;
    const bool accepted = ActionManager::getInstance()->triggerAction(
        QStringLiteral("model.filament.assign"),
        QVariantMap{
            {QStringLiteral("modelId"),
             QVariant::fromValue<qulonglong>(m_selectedId.getValue())},
            {QStringLiteral("slot"), slot}});
    if (accepted) {
        if (const auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(
                getSelectedActor())) {
            updateFilamentBinding(instance);
        }
    }
    return accepted;
}

bool ModelPropertyBridge::useProjectDefaultFilament() {
    if (!m_selectedId.isValid() || m_filamentSlot == 0) return false;
    const bool accepted = ActionManager::getInstance()->triggerAction(
        QStringLiteral("model.filament.use_default"),
        QVariantMap{{QStringLiteral("modelId"),
                     QVariant::fromValue<qulonglong>(
                         m_selectedId.getValue())}});
    if (accepted) {
        if (const auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(
                getSelectedActor())) {
            updateFilamentBinding(instance);
        }
    }
    return accepted;
}

void ModelPropertyBridge::setOpacity(float opacity) {
    if (qFuzzyCompare(m_opacity, opacity)) return;

    executeInTransaction("Set Opacity", [this, opacity]() {
        if (auto actor = getSelectedActor()) {
            if (const auto instance =
                    std::dynamic_pointer_cast<ModelInstanceDB>(actor)) {
                instance->setOpacity(opacity);
            } else if (actor->getMaterial()) {
                actor->getMaterial()->setOpacity(opacity);
            }
        }
    });
    m_opacity = opacity;
}

// Bindables
QBindable<bool> ModelPropertyBridge::bindableHasSelectedModel() { return &m_hasSelectedModel; }
QBindable<QString> ModelPropertyBridge::bindableModelType() { return &m_modelType; }
QBindable<QString> ModelPropertyBridge::bindableDisplayName() { return &m_displayName; }
QBindable<QVector3D> ModelPropertyBridge::bindablePosition() { return &m_position; }
QBindable<QVector3D> ModelPropertyBridge::bindableRotation() { return &m_rotation; }
QBindable<QVector3D> ModelPropertyBridge::bindableScale() { return &m_scale; }
QBindable<QVector3D> ModelPropertyBridge::bindableDiffuseColor() { return &m_diffuseColor; }
QBindable<float> ModelPropertyBridge::bindableOpacity() { return &m_opacity; }
QBindable<bool> ModelPropertyBridge::bindableBoundsValid() { return &m_boundsValid; }
QBindable<QVector3D> ModelPropertyBridge::bindableBoundsCenter() { return &m_boundsCenter; }
QBindable<QVector3D> ModelPropertyBridge::bindableBoundsSize() { return &m_boundsSize; }

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    ModelPropertyBridge, "ModelPropertyBridge", &ModelPropertyBridge::create)
