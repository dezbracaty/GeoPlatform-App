#include "../include/AutoRegisterDB.hpp"
#include "../include/IDocumentRegistry.hpp"
#include "Foundation/Log.h"
#include <algorithm>
#include <set>
#include <typeindex>
#include <atomic>
#include <cmath>
#include <unordered_set>

// 静态成员已移除，直接使用单例
namespace {
thread_local size_t g_documentLoadDepth = 0;
thread_local std::vector<std::vector<DBInstanceID>*> g_tempCreationCollectors;
}

AutoRegisterDB::AutoRegisterDB()
    : trans::TransDB() // 调用基类构造函数
      ,
      m_dbInstanceId(isTempCreationActive()
                         ? DBInstanceID::generateTemp()
                         : DBInstanceID::generate()),
      m_displayName("") {
    // 不在构造函数中注册，而是在 onCreated 中
}

AutoRegisterDB::~AutoRegisterDB() {
    unregisterFromDocumentManager();
}

SnapGeometryPtr AutoRegisterDB::snapGeometry() const {
    return std::atomic_load_explicit(&m_snapGeometry, std::memory_order_acquire);
}

bool AutoRegisterDB::publishSnapGeometry(SnapGeometry geometry) {
    const auto finite = [](const Vector3& point) {
        return std::isfinite(point.x) && std::isfinite(point.y) &&
               std::isfinite(point.z);
    };
    if (geometry.features.size()>std::numeric_limits<std::uint32_t>::max()) return false;
    std::unordered_set<std::uint64_t> ids;
    for (const auto& feature : geometry.features) {
        if (feature.kind != SnapFeatureKind::Point &&
            feature.kind != SnapFeatureKind::Segment &&
            feature.kind != SnapFeatureKind::Line) return false;
        if (std::uint64_t(feature.groupBegin) + feature.groupCount > geometry.features.size())
            return false;
        if (!std::isfinite(feature.radiusMm) || feature.radiusMm < 0 ||
            !ids.insert(feature.id).second || !finite(feature.first) ||
            (feature.kind != SnapFeatureKind::Point && !finite(feature.second)))
            return false;
    }
    SnapGeometryPtr snapshot;
    geometry.buildIndex();
    if (!geometry.features.empty())
        snapshot = std::make_shared<const SnapGeometry>(std::move(geometry));
    std::atomic_store_explicit(&m_snapGeometry, std::move(snapshot),
                               std::memory_order_release);
    return true;
}

const DBInstanceID& AutoRegisterDB::getDBInstanceID() const {
    return m_dbInstanceId;
}

std::string AutoRegisterDB::getDisplayName() const {
    return m_displayName.empty() ? ("Object_" + m_dbInstanceId.toString()) : m_displayName;
}

void AutoRegisterDB::setDisplayName(const std::string& name) {
    if (m_displayName != name) {
        auto oldName = m_displayName;
        m_displayName = name;
        trans::Prop prop(typeid(*this), "displayName");
        notifyPropertyChanged(prop);
    }
}

bool AutoRegisterDB::isValid() const {
    return m_dbInstanceId.isValid();
}


std::vector<std::string_view> AutoRegisterDB::getPropertyNames() const {
    // 获取 TransDB 中所有的属性（通过 FIELD_VALUE 宏定义的）
    std::vector<std::string_view> names = trans::TransDB::getPropertyNames();

    // 添加 AutoRegisterDB 特有的成员变量（不在属性字典中的）
    names.push_back("dbInstanceId");
    names.push_back("displayName");

    return names;
}

PropertyMap AutoRegisterDB::serialize() const {
    PropertyMap properties;

    // 1. 遍历所有 FIELD_VALUE 宏管理的属性（在 m_properties 字典中）
    std::vector<std::string_view> propNames = trans::TransDB::getPropertyNames();
    for (const auto& propName : propNames) {
        std::any value = getPropertyByName(propName);
        if (value.has_value()) {
            // 将 std::any 转换为 QVariant
            // 注意：这里需要处理不同类型的转换
            try {
                std::string propNameStr(propName);  // 转换为 std::string
                // 尝试常见类型
                if (value.type() == typeid(int)) {
                    properties[QString::fromStdString(propNameStr)] = std::any_cast<int>(value);
                } else if (value.type() == typeid(float)) {
                    properties[QString::fromStdString(propNameStr)] = std::any_cast<float>(value);
                } else if (value.type() == typeid(double)) {
                    properties[QString::fromStdString(propNameStr)] = std::any_cast<double>(value);
                } else if (value.type() == typeid(bool)) {
                    properties[QString::fromStdString(propNameStr)] = std::any_cast<bool>(value);
                } else if (value.type() == typeid(std::string)) {
                    properties[QString::fromStdString(propNameStr)] = QString::fromStdString(std::any_cast<std::string>(value));
                } else if (value.type() == typeid(DBInstanceID)) {
                    properties[QString::fromStdString(propNameStr)] =
                        QVariant::fromValue<qlonglong>(std::any_cast<DBInstanceID>(value).getValue());
                } else if (value.type() == typeid(Vector3)) {
                    Vector3 vec = std::any_cast<Vector3>(value);
                    QVariantList list;
                    list << vec.x << vec.y << vec.z;
                    properties[QString::fromStdString(propNameStr)] = list;
                }
                // 可以根据需要添加更多类型
            } catch (const std::bad_any_cast&) {
                // 如果类型转换失败，跳过这个属性
            }
        }
    }

    // 2. 添加不在属性字典中的成员变量
    properties["dbInstanceId"] = m_dbInstanceId.getValue();
    properties["displayName"] = QString::fromStdString(m_displayName);
    properties["typeId"] = static_cast<uint32_t>(getTypeID());

    return properties;
}

bool AutoRegisterDB::deserialize(const PropertyMap& properties) {
    // 获取当前对象的所有属性名称
    std::vector<std::string_view> knownProperties = getPropertyNames();
    std::set<std::string> knownPropSet(knownProperties.begin(), knownProperties.end());
    const std::string_view runtimeTypeName = std::type_index(typeid(*this)).name();

    // 遍历传入的属性
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        std::string propName = it.key().toStdString();
        QVariant value = it.value();

        // 特殊处理非属性字典中的成员变量
        if (propName == "displayName") {
            m_displayName = value.toString().toStdString();
            continue;
        }
        if (propName == "dbInstanceId") {
            if (isDocumentLoadActive()) {
                DBInstanceID restoredId(value.toLongLong());
                if (restoredId.isValid() && !restoredId.isTemp()) {
                    m_dbInstanceId = restoredId;
                    DBInstanceID::reserveAtLeast(restoredId);
                }
            }
            continue;
        }
        if (propName == "typeId") {
            // typeId 是只读的，跳过
            continue;
        }

        // 只处理已知的属性（在属性字典中的）
        if (knownPropSet.find(propName) == knownPropSet.end()) {
            // 不在属性列表中的属性，忽略
            continue;
        }

        // 将 QVariant 转换为 std::any 并设置到属性字典
        try {
            const trans::Prop prop = resolvePropForWrite(runtimeTypeName, propName);
            if (value.typeId() == QMetaType::Int) {
                setPropertyImpl(prop, std::any(value.toInt()));
            } else if (value.typeId() == QMetaType::Double) {
                // QVariant 可能将 float 存储为 double
                setPropertyImpl(prop, std::any(value.toDouble()));
            } else if (value.typeId() == QMetaType::Bool) {
                setPropertyImpl(prop, std::any(value.toBool()));
            } else if (value.typeId() == QMetaType::QString) {
                const std::any currentValue = getPropertyByName(propName);
                if (currentValue.type() == typeid(DBInstanceID)) {
                    setPropertyImpl(prop, std::any(DBInstanceID(value.toString().toLongLong())));
                } else {
                    setPropertyImpl(prop, std::any(value.toString().toStdString()));
                }
            } else if (value.typeId() == QMetaType::ULongLong || value.typeId() == QMetaType::LongLong) {
                const std::any currentValue = getPropertyByName(propName);
                if (currentValue.type() == typeid(DBInstanceID)) {
                    setPropertyImpl(prop, std::any(DBInstanceID(value.toLongLong())));
                }
            } else if (value.typeId() == QMetaType::QVariantList) {
                // 处理 Vector3 类型
                QVariantList list = value.toList();
                if (list.size() == 3) {
                    Vector3 vec(list[0].toFloat(), list[1].toFloat(), list[2].toFloat());
                    setPropertyImpl(prop, std::any(vec));
                }
            }
            // 可以根据需要添加更多类型
        } catch (...) {
            // 如果转换失败，继续处理下一个属性
        }
    }

    return true;
}

bool AutoRegisterDB::isDocumentLoadActive() {
    return g_documentLoadDepth > 0;
}

void AutoRegisterDB::pushDocumentLoad() {
    ++g_documentLoadDepth;
}

void AutoRegisterDB::popDocumentLoad() {
    if (g_documentLoadDepth > 0) {
        --g_documentLoadDepth;
    }
}

DocumentLoadGuard::DocumentLoadGuard() {
    AutoRegisterDB::pushDocumentLoad();
}

DocumentLoadGuard::~DocumentLoadGuard() {
    AutoRegisterDB::popDocumentLoad();
}

AutoRegisterDB::TempCreationGuard::TempCreationGuard(
    std::vector<DBInstanceID>* createdIds) {
    AutoRegisterDB::pushTempCreation(createdIds);
}

AutoRegisterDB::TempCreationGuard::~TempCreationGuard() {
    AutoRegisterDB::popTempCreation();
}

void AutoRegisterDB::pushTempCreation(std::vector<DBInstanceID>* createdIds) {
    g_tempCreationCollectors.push_back(createdIds);
}

void AutoRegisterDB::popTempCreation() {
    if (!g_tempCreationCollectors.empty()) {
        g_tempCreationCollectors.pop_back();
    }
}

bool AutoRegisterDB::isTempCreationActive() {
    return !g_tempCreationCollectors.empty();
}

void AutoRegisterDB::recordTempCreation(const DBInstanceID& id) {
    if (!id.isTemp()) return;
    for (auto it = g_tempCreationCollectors.rbegin();
         it != g_tempCreationCollectors.rend(); ++it) {
        auto* ids = *it;
        if (!ids) continue;
        if (std::find(ids->begin(), ids->end(), id) == ids->end()) {
            ids->push_back(id);
        }
        return;
    }
}

void AutoRegisterDB::notifyPropertyChanged(const trans::Prop& prop) {
    // 注意：不要在这里调用 backupProperty
    // 因为 FIELD_VALUE 宏已经在 setter 中调用了 backupProperty
    // 这里只负责通知

    // 通知DocumentManager变化
    if (auto* docManager = documentRegistry()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, prop.name());
    }
}

void AutoRegisterDB::notifyChange(ChangeType changeType, const std::string& fieldName) {
    if (auto* docManager = documentRegistry()) {
        docManager->notifyChange(this, changeType, fieldName);
    }
}

void AutoRegisterDB::transWithChange(const std::string& field, ChangeType changeType) {
    if (auto* docManager = documentRegistry()) {
        docManager->transWithChange(this, field, changeType);
    }
}

void AutoRegisterDB::onCreated() {
    // 这个方法现在是 final，子类不能覆盖
    // 通过钩子函数提供扩展点，保证初始化顺序正确

    // // 1. 调用基类
    // trans::TransDB::onCreated();

    // 2-3. Construction defaults are part of the object's initial snapshot,
    // not independent user edits. An outer TransactionGuard records the
    // lifecycle creation below; later property edits remain fully undoable.
    {
        DerivedUpdateGuard initializationGuard;
        initializeProperties();
        afterPropertiesInitialized();
    }

    // 4. 自动注册到 DocumentManager（固定流程）
    if (shouldAutoRegister()) {
        autoRegisterToDocumentManager();
        recordTempCreation(m_dbInstanceId);

        auto sharedThis = std::static_pointer_cast<trans::TransDB>(shared_from_this());
        trans::DBInstanceID transId(m_dbInstanceId.getValue());
        trans::TypeID transTypeId(static_cast<uint32_t>(getTypeID()));

        TransactionManager::instance().recordObjectCreation(
            sharedThis, transId, transTypeId);
    }

    // 5. 所有初始化完成后的钩子
    onFullyInitialized();
}

void AutoRegisterDB::autoRegisterToDocumentManager() {
    if (auto* docManager = documentRegistry()) {
        // 现在可以安全使用 shared_from_this()
        auto self = std::static_pointer_cast<AutoRegisterDB>(shared_from_this());

        // 注册到 DocumentManager
        m_dbInstanceId = docManager->registerDBInstance(self, getTypeID());
        if (m_dbInstanceId == INVALID_DB_ID) {
            LOG_ERROR("Failed to register DB instance (TypeID: {}) to DocumentManager - object will operate in unmanaged mode", static_cast<int>(getTypeID()));
            // 注意：对象仍然可以使用，只是不会被DocumentManager管理（没有通知、同步等）
            // 继续执行，让对象在"无管理"模式下工作
        }
    }
}

void AutoRegisterDB::unregisterFromDocumentManager() {
    if (auto* docManager = documentRegistry()) {
        docManager->unregisterDBInstance(m_dbInstanceId);
    }
}
void AutoRegisterDB::setPropertyImpl(const trans::Prop& prop, const std::any& value) {

    // 首先调用基类的实现来设置属性值
    trans::TransDB::setPropertyImpl(prop, value);

    // 发送通知
    if (auto* docManager = documentRegistry()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, prop.name());
    }
}
