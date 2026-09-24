#pragma once

#include <BaseID.hpp>
#include <ChangeTypes.hpp>
#include <memory>
#include <string>
#include <typeinfo>

// Forward declarations
class AutoRegisterDB;

/**
 * @brief 级联更新器基类
 *
 * 负责定义主从DB对象之间的更新关系。
 * 当主对象（master）发生变化时，自动更新从对象（slave）。
 */
class ChainUpdater {
public:
    struct Identity {
        DBInstanceID masterId;
        DBInstanceID slaveId;
        std::string kind;
        uint64_t ruleId = 0;

        bool operator==(const Identity& other) const {
            return masterId == other.masterId &&
                   slaveId == other.slaveId &&
                   kind == other.kind &&
                   ruleId == other.ruleId;
        }
    };

    /**
     * @brief 构造函数
     * @param master 主对象ID（被依赖者）
     * @param slave 从对象ID（依赖者）
     */
    ChainUpdater(const DBInstanceID& master, const DBInstanceID& slave)
        : m_masterId(master), m_slaveId(slave) {
    }

    virtual ~ChainUpdater() = default;

    /**
     * @brief 执行更新
     *
     * 当主对象发生变化时调用，负责更新从对象的相关属性
     *
     * @param masterDB 主对象的DB实例
     * @param slaveDB 从对象的DB实例
     * @param changeType 变化类型
     * @param propertyName 发生变化的属性名称
     */
    virtual void update(std::shared_ptr<AutoRegisterDB> masterDB,
                        std::shared_ptr<AutoRegisterDB> slaveDB,
                        ChangeType changeType,
                        const std::string& propertyName) = 0;

    /**
     * @brief 获取主对象ID
     */
    const DBInstanceID& getMasterId() const {
        return m_masterId;
    }

    /**
     * @brief 获取从对象ID
     */
    const DBInstanceID& getSlaveId() const {
        return m_slaveId;
    }

    /**
     * @brief 检查是否应该响应此属性变化
     *
     * 子类可以重写此方法来过滤不需要响应的属性
     *
     * @param propertyName 属性名称
     * @return true 如果需要响应此属性变化
     */
    virtual bool shouldUpdate(ChangeType changeType, const std::string& propertyName) const {
        (void)changeType;
        (void)propertyName;
        // 默认响应所有属性变化
        return true;
    }

    virtual Identity getIdentity() const {
        return Identity{m_masterId, m_slaveId, typeid(*this).name(), 0};
    }

protected:
    DBInstanceID m_masterId; // 主对象（被依赖者）
    DBInstanceID m_slaveId;  // 从对象（依赖者）
};
