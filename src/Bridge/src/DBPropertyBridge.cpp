#include "DBPropertyBridge.hpp"
#include "BridgeRegistration.hpp"
#include <DocumentManager.hpp>
#include "BaseID.hpp"
#include "SystemTypes.hpp"
#include "ChangeTypes.hpp"
#include "Foundation/Log.h"
#include <QPropertyBindingSourceLocation>
#include <ModelInstanceDB.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelPartDB.hpp>
#include <TransactionManager.hpp>
#include <TransientPolyDataActorDB.hpp>
#include <algorithm>

QVariantList DBPropertyBridge::models() const {
    auto instances = DocumentManager::instance()->getDBInstancesByType(TypeID::MODEL_INSTANCE_DB);
    std::sort(instances.begin(), instances.end(), [](const auto& a, const auto& b) {
        return a->getDBInstanceID().getValue() < b->getDBInstanceID().getValue();
    });
    QVariantList result;
    for (const auto& instance : instances) {
        const auto model = std::dynamic_pointer_cast<ModelInstanceDB>(instance);
        if (!model)
            continue;
        QVariantList children;
        const auto parts = model->parts();
        if (parts.size() > 1) {
            for (const auto& part : parts) {
                const auto role = static_cast<ModelPartRole>(part->getRole());
                const auto binding = static_cast<ModelFilamentBindingMode>(part->getFilamentBindingMode());
                children.append(QVariantMap{
                    {"id", QString::number(part->getDBInstanceID().getValue())},
                    {"name", QString::fromStdString(part->getDisplayName())},
                    {"modifier", role == ModelPartRole::ParameterModifier},
                    {"materialEditable", role == ModelPartRole::Model || role == ModelPartRole::ParameterModifier},
                    {"inheritsObject", binding == ModelFilamentBindingMode::InheritObject},
                    {"slot", binding == ModelFilamentBindingMode::ExplicitSlot ? part->getDefaultFilamentSlot() : 0}});
            }
        }
        result.append(QVariantMap{{"id", QString::number(model->getDBInstanceID().getValue())},
                                  {"name", QString::fromStdString(model->getDisplayName())},
                                  {"parts", children}});
    }
    return result;
}

void DBPropertyBridge::highlightModelPart(const QString& modelId, const QString& partId) {
    m_highlightModel = modelId.isEmpty() ? INVALID_DB_ID : DBInstanceID(modelId.toULongLong());
    m_highlightPart = partId.isEmpty() ? INVALID_DB_ID : DBInstanceID(partId.toULongLong());
    updatePartHighlight();
}

void DBPropertyBridge::updatePartHighlight() {
    TransientUpdateGuard guard;
    m_partHighlight.clear();
    m_highlightGeometry = INVALID_DB_ID;
    if (!m_highlightModel.isValid() || !m_highlightPart.isValid())
        return;
    const auto model = DocumentManager::instance()->getDB<ModelInstanceDB>(m_highlightModel);
    if (!model || !model->isVisible())
        return;
    for (const auto& part : model->parts()) {
        if (part->getDBInstanceID() != m_highlightPart)
            continue;
        const auto geometry = part->geometry();
        if (!geometry || !part->hasValidMesh())
            return;
        m_highlightGeometry = geometry->getDBInstanceID();
        const auto snapshot = geometry->read();
        if (!snapshot)
            return;
        auto mesh = vtkSmartPointer<vtkPolyData>::New();
        mesh->ShallowCopy(const_cast<vtkPolyData*>(&snapshot.polyData()));
        const auto actor = m_partHighlight.create<TransientPolyDataActorDB>();
        actor->setPickable(false);
        actor->setDragable(false);
        actor->setDrawOnTop(true);
        actor->setGeometry(mesh);
        Transform transform;
        transform.setMatrix(model->getTransformMatrix() * part->getLocalTransform().getMatrix());
        actor->setTransform(transform);
        if (const auto material = actor->getMaterial()) {
            const Vector3 color{1.0f, 0.65f, 0.08f};
            material->setColor(color);
            material->setDiffuseColor(color);
            material->setOpacity(0.45f);
            material->setLighting(false);
        }
        return;
    }
}

DBPropertyBridge::DBPropertyBridge(QObject* parent) : ObservableBridgeBase(parent) {
    // LOG_INFO("DBPropertyBridge created");
    setupBindings();
    initialize();  // Initialize the listener from base class
}

DBPropertyBridge::~DBPropertyBridge() {
    // LOG_INFO("DBPropertyBridge destroyed");
    // Base class will handle listener cleanup
}

void DBPropertyBridge::setupBindings() {
    auto* docMgr = DocumentManager::instance();

    QPropertyBindingSourceLocation loc;

    // 设置 gridDisplayExists 的绑定
    m_gridBinding = QPropertyBinding<bool>(
        [docMgr]() {
            // 获取所有 GRID_DB 类型的实例
            auto instances = docMgr->getAllDBInstanceIds(TypeID::GRID_DB);
            bool exists = !instances.empty();
            LOG_DEBUG("gridDisplayExists binding evaluated: {}", exists);
            return exists;
        },
        loc
    );
    m_gridDisplayExists.setBinding(m_gridBinding);

    m_meshCountBinding = QPropertyBinding<int>(
        [docMgr]() {
            return static_cast<int>(docMgr->getAllDBInstanceIds(
                TypeID::MODEL_INSTANCE_DB).size());
        },
        loc
    );
    m_meshCount.setBinding(m_meshCountBinding);

    // 不再需要手动注册监听器 - ObservableBridgeBase会处理
    // 但我们需要监听所有对象，因为我们关心类型而不是特定ID
    // 这需要在handleDatabaseChange中处理

    // LOG_INFO("DBPropertyBridge bindings setup completed");
}
void DBPropertyBridge::handleDatabaseChange(const DocumentManager::ChangeNotification& notification) {
    if (notification.dbType == TypeID::MODEL_INSTANCE_DB || notification.dbType == TypeID::MODEL_PART_DB ||
        notification.dbType == TypeID::MODEL_GEOMETRY_DB) {
        const auto id = notification.id;
        const auto type = notification.dbType;
        const bool partListChanged = type == TypeID::MODEL_PART_DB &&
            (notification.changeType != ChangeType::PROPERTY_CHANGED ||
             notification.propertyName == "displayName" || notification.propertyName == "Role" ||
             notification.propertyName == "OrderIndex" || notification.propertyName == "FilamentBindingMode" ||
             notification.propertyName == "DefaultFilamentSlot");
        const bool highlightChanged = notification.changeType != ChangeType::PROPERTY_CHANGED ||
            notification.propertyName == "Transform" || notification.propertyName == "Visible" ||
            notification.propertyName == "LocalTransform" || notification.propertyName == "GeometryVersion";
        if (partListChanged || highlightChanged)
            QMetaObject::invokeMethod(this, [this, id, partListChanged, highlightChanged] {
                if (partListChanged)
                    emit modelsChanged();
                if (highlightChanged &&
                    (id == m_highlightModel || id == m_highlightPart || id == m_highlightGeometry))
                    updatePartHighlight();
            }, Qt::QueuedConnection);
    }
    // 当文档发生变化时，Qt6 的绑定系统会自动重新计算
    // 我们只需要确保绑定表达式能够被重新求值

    // 获取类型名称用于日志
    QString typeName = typeIdToString(notification.dbType);

    // 记录日志
    LOG_DEBUG("DBPropertyBridge: Object {} changed, type: {}, change: {}",
              notification.id.getValue(), typeName.toStdString(), static_cast<int>(notification.changeType));

    // 检查是否是 Widget 相关的类型变化
    if (notification.dbType == TypeID::GRID_DB ||
        isPrintableModelType(notification.dbType)) {

        // Qt6 的属性绑定系统会自动处理重新计算
        // 当底层数据发生变化时，绑定会自动重新求值
        // 手动触发绑定重新评估
        markBindingsDirty();
        emit modelsChanged();

        LOG_DEBUG("Widget type changed: {}, bindings marked dirty for re-evaluation", typeName.toStdString());
    }
}

void DBPropertyBridge::markBindingsDirty() {
    // 重新设置绑定以触发重新评估
    // Qt6 的绑定系统在重新设置绑定时会立即评估

    // 先移除旧绑定
    m_gridDisplayExists.setBinding(QPropertyBinding<bool>());
    m_meshCount.setBinding(QPropertyBinding<int>());

    // 重新设置绑定，这会触发立即评估
    m_gridDisplayExists.setBinding(m_gridBinding);
    m_meshCount.setBinding(m_meshCountBinding);

    LOG_DEBUG("markBindingsDirty: bindings have been reset and will re-evaluate");
}

REGISTER_BRIDGE_QML_TYPE(DBPropertyBridge, "DBPropertyBridge")
