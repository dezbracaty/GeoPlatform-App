#pragma once

#include "BaseID.hpp"
#include "SystemTypes.hpp"
#include <QString>
#include <QVariant>
#include <chrono>
#include <string>
#include <vector>

/**
 * @brief 变化类型枚举（简化版 - Phase 1）
 *
 * 将原有的 13 种变化类型简化为 3 种核心类型。
 */
enum class ChangeType : uint32_t {
    UNKNOWN = 0,
    OBJECT_CREATED = 1,   // 对象创建
    OBJECT_DELETED = 2,   // 对象删除
    PROPERTY_CHANGED = 10 // 属性变化
};

/**
 * @brief 变化信息结构
 */
struct ChangeInfo {
    ChangeType type = ChangeType::UNKNOWN;
    std::string fieldName;   // 变化的字段名
    Timestamp timestamp;     // 时间戳
    std::string description; // 变化描述

    ChangeInfo() : timestamp(getCurrentTimestamp()) {
    }

    ChangeInfo(ChangeType t, const std::string& field = "")
        : type(t), fieldName(field), timestamp(getCurrentTimestamp()) {
    }
};

/**
 * @brief 变化记录结构（用于DocumentManager）
 */
struct ChangeRecord {
    void* object = nullptr;    // 变化的对象指针
    DBInstanceID dbInstanceId; // DB实例ID
    ChangeType changeType = ChangeType::UNKNOWN;
    std::string fieldName;                             // 字段名称
    Timestamp timestamp;                               // 时间戳
    RenderTargets renderTargets = RenderTargets::NONE; // 需要重新渲染的目标

    ChangeRecord() : timestamp(getCurrentTimestamp()) {
    }

    ChangeRecord(void* obj, ChangeType type, const std::string& field = "")
        : object(obj), changeType(type), fieldName(field),
          timestamp(getCurrentTimestamp()) {
    }
};

/**
 * @brief 变化类型辅助函数
 */
inline QString changeTypeToString(ChangeType type) {
    switch (type) {
        case ChangeType::UNKNOWN:
            return "UNKNOWN";
        case ChangeType::OBJECT_CREATED:
            return "OBJECT_CREATED";
        case ChangeType::OBJECT_DELETED:
            return "OBJECT_DELETED";
        case ChangeType::PROPERTY_CHANGED:
            return "PROPERTY_CHANGED";
        default:
            return "UNKNOWN";
    }
}

inline ChangeType stringToChangeType(const QString& str) {
    if (str == "OBJECT_CREATED")
        return ChangeType::OBJECT_CREATED;
    if (str == "OBJECT_DELETED")
        return ChangeType::OBJECT_DELETED;
    if (str == "PROPERTY_CHANGED")
        return ChangeType::PROPERTY_CHANGED;
    return ChangeType::UNKNOWN;
}

/**
 * @brief 根据变化类型确定需要的渲染目标
 *
 * @param changeType 变化类型
 * @return 需要更新的渲染目标组合
 */
inline RenderTargets getRequiredRenderTargets(ChangeType changeType) {
    switch (changeType) {
        case ChangeType::OBJECT_CREATED:
        case ChangeType::OBJECT_DELETED:
            return RenderTargets::ALL;

        case ChangeType::PROPERTY_CHANGED:
            return RenderTargets::ALL; // 属性变化需要更新所有渲染目标

        default:
            return RenderTargets::NONE;
    }
}

// Qt元对象系统支持
Q_DECLARE_METATYPE(ChangeType)
Q_DECLARE_METATYPE(ChangeInfo)
Q_DECLARE_METATYPE(ChangeRecord)