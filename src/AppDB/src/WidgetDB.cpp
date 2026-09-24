#include "WidgetDB.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"

WidgetDB::WidgetDB() : AutoRegisterDB() {
    // 构造函数保持简单，初始化在onCreated中进行
}

WidgetDB::~WidgetDB() {
    // 析构时不需要特殊处理，AutoRegisterDB会处理注销
}

void WidgetDB::initializeProperties() {
    // 初始化所有Widget基础属性的默认值
    setVisible(true);      // 默认可见
    setRenderLayer(1);     // 默认在overlay层（1）
    setOpacity(1.0f);      // 默认完全不透明
    setInteractive(false); // 默认不可交互
    setPriority(0.5f);     // 默认中等优先级
    setName("Widget");     // 默认名称
    setHoveredPart(-1);

    // 调用子类钩子函数
    initializeSubWidgetProperties();
}

void WidgetDB::afterPropertiesInitialized() {
    // 调用子类钩子函数
    afterSubWidgetPropertiesInitialized();

    // Widget创建后的额外处理
    LOG_DEBUG("WidgetDB created: {}", getDBInstanceID().getValue());
}

void WidgetDB::setPropertyImpl(const trans::Prop& prop, const std::any& value) {
    // 直接调用AutoRegisterDB的setPropertyImpl，而不是TransDB的setProperty
    // 这样可以绕过TransDB中的事务记录逻辑
    // AutoRegisterDB::setPropertyImpl会调用TransDB::setPropertyImpl来设置值，
    // 并通过DocumentManager发送通知
    AutoRegisterDB::setPropertyImpl(prop, value);
}

void WidgetDB::notifyWidgetChange(const std::string& propertyName) {
    // 通知DocumentManager属性变化
    if (auto* docManager = DocumentManager::instance()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, propertyName);
    }

    LOG_DEBUG("Widget property '{}' changed", propertyName);
}
