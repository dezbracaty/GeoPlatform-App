#pragma once

#include <BaseID.hpp>
#include <ChangeTypes.hpp>
#include <DBRelationTypes.hpp>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <set>
#include <string>

// Forward declarations
class ChainUpdater;
class AutoRegisterDB;
class DocumentManager;

/**
 * @brief 级联更新系统
 *
 * 管理DB对象之间的依赖关系，当一个DB对象发生变化时，
 * 自动触发所有依赖它的对象进行级联更新。
 *
 * 支持多级依赖：如果C依赖B，B依赖A，当A更新时会自动更新B，然后更新C。
 */
class ChainUpdateSystem {
private:
    ChainUpdateSystem() = default;
    friend class DocumentManager;

    // 依赖图：主对象ID -> 更新器列表
    std::unordered_map<DBInstanceID, std::vector<std::shared_ptr<ChainUpdater>>> m_updaterMap;

    // 反向索引：从对象ID -> 主对象ID列表（用于清理）
    std::unordered_map<DBInstanceID, std::set<DBInstanceID>> m_reverseDependencies;

    // 批处理模式相关
    bool m_batchMode = false;
    
    // 批处理更新信息
    struct BatchUpdate {
        DBInstanceID masterId;
        ChangeType changeType;
        std::string propertyName;
        
        // 用于去重的比较操作符
        bool operator<(const BatchUpdate& other) const {
            if (masterId != other.masterId) return masterId < other.masterId;
            if (changeType != other.changeType) return changeType < other.changeType;
            return propertyName < other.propertyName;
        }
    };
    
    // 批处理待执行的更新（使用set自动去重）
    std::set<BatchUpdate> m_batchedUpdates;

    struct ActiveUpdateEdge {
        DBInstanceID masterId;
        DBInstanceID slaveId;
        std::string updaterKind;
        uint64_t ruleId = 0;
        ChangeType changeType = ChangeType::UNKNOWN;
        std::string propertyName;

        bool operator<(const ActiveUpdateEdge& other) const {
            if (masterId != other.masterId) return masterId < other.masterId;
            if (slaveId != other.slaveId) return slaveId < other.slaveId;
            if (updaterKind != other.updaterKind) return updaterKind < other.updaterKind;
            if (ruleId != other.ruleId) return ruleId < other.ruleId;
            if (changeType != other.changeType) return changeType < other.changeType;
            return propertyName < other.propertyName;
        }
    };

    struct UpdateExecutionContext {
        std::set<ActiveUpdateEdge> activeEdges;
        size_t depth = 0;
        size_t updatesThisFlush = 0;
    };

    UpdateExecutionContext* m_currentExecution = nullptr;
    size_t m_maxUpdateDepth = 32;
    size_t m_maxUpdatesPerFlush = 10000;

    // 单例实例
    static std::unique_ptr<ChainUpdateSystem> s_instance;

public:
    /**
     * @brief 获取单例实例
     */
    static ChainUpdateSystem& instance();

    // 禁止拷贝和移动
    ChainUpdateSystem(const ChainUpdateSystem&) = delete;
    ChainUpdateSystem& operator=(const ChainUpdateSystem&) = delete;
    ChainUpdateSystem(ChainUpdateSystem&&) = delete;
    ChainUpdateSystem& operator=(ChainUpdateSystem&&) = delete;

    /**
     * @brief 清理对象相关的所有依赖关系
     *
     * 当一个DB对象被删除时调用，清理所有相关的更新器
     *
     * @param id 要清理的对象ID
     */
    void cleanupDependencies(const DBInstanceID& id);

    /**
     * @brief 触发级联更新
     *
     * 当一个DB对象发生变化时调用，会自动触发所有依赖它的对象进行更新
     *
     * @param masterId 发生变化的主对象ID
     * @param changeType 变化类型
     * @param propertyName 发生变化的属性名称
     */
    void triggerUpdate(const DBInstanceID& masterId,
                       ChangeType changeType,
                       const std::string& propertyName);
    
    /**
     * @brief 批量更新模式 - 开始批处理
     * 
     * 开启批处理模式后，所有的更新会被收集而不是立即执行。
     * 调用 endBatchUpdate() 时会一次性执行所有收集的更新。
     * 批处理模式可以显著提升性能，特别是当有大量关联更新时。
     */
    void beginBatchUpdate();
    
    /**
     * @brief 批量更新模式 - 结束批处理并执行更新
     * 
     * 结束批处理模式，并执行所有收集的更新。
     * 相同的更新会被合并，避免重复执行。
     */
    void endBatchUpdate();
    
    /**
     * @brief 检查是否处于批处理模式
     */
    bool isInBatchMode() const { return m_batchMode; }

    /**
     * @brief 检查是否会造成循环依赖
     *
     * @param master 主对象ID
     * @param slave 从对象ID
     * @return true 如果会造成循环依赖
     */
    bool wouldCreateCycle(const DBInstanceID& master, const DBInstanceID& slave) const;

    size_t dependencyCount() const;

    /**
     * @brief 获取依赖关系的调试信息
     *
     * @return 依赖关系图的字符串表示
     */
    std::string getDependencyGraph() const;

    /**
     * @brief 清空所有依赖关系
     */
    void clear();

private:
    bool attachDependencyFromRelation(const DBInstanceID& masterId,
                                      const DBInstanceID& slaveId,
                                      const std::string& relationName);
    void detachDependencyFromRelation(const DBInstanceID& masterId,
                                      const DBInstanceID& slaveId,
                                      const std::string& relationName,
                                      DBRelationDetachReason reason = DBRelationDetachReason::ExplicitDetach);

    /**
     * @brief 递归更新依赖对象
     *
     * @param masterId 主对象ID
     * @param changeType 变化类型
     * @param propertyName 属性名称
     */
    void runRootUpdate(const std::function<void(UpdateExecutionContext&)>& updateBody);
    void executeUpdate(UpdateExecutionContext& context,
                       const DBInstanceID& masterId,
                       ChangeType changeType,
                       const std::string& propertyName);
    void updateRecursive(UpdateExecutionContext& context,
                         const DBInstanceID& masterId,
                         ChangeType changeType,
                         const std::string& propertyName);

    /**
     * @brief 深度优先搜索检测循环
     *
     * @param current 当前节点
     * @param target 目标节点
     * @param visited 已访问节点集合
     * @return true 如果存在从current到target的路径
     */
    bool hasPathDFS(const DBInstanceID& current,
                    const DBInstanceID& target,
                    std::set<DBInstanceID>& visited) const;
};
