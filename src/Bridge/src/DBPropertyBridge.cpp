#include "DBPropertyBridge.hpp"
#include "BridgeRegistration.hpp"
#include <DocumentManager.hpp>
#include "BaseID.hpp"
#include "SystemTypes.hpp"
#include "ChangeTypes.hpp"
#include "Foundation/Log.h"
#include <QPropertyBindingSourceLocation>

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
