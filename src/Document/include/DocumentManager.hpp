#pragma once

#include <AutoRegisterDB.hpp>
#include <BaseID.hpp>
#include <ChangeTypes.hpp>
#include <DBRelationTypes.hpp>
#include <ImmediateNotifyGuard.hpp>
#include <SystemTypes.hpp>
#include <IDocumentRegistry.hpp>
#include "transdb.h"
#include <UndoRedoManager.hpp>
#include <QColor>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

// 前向声明
class DocumentRelationAccess;

/**
 * @brief DocumentManager - 变化跟踪和DB实例管理器
 *
 * 负责收集变化、分类处理、渲染调度的核心类。
 * 基于事务状态自动判断是否批量处理变化。
 * 统一DB实例管理，支持DBInstanceID和TypeID系统
 */
class DocumentManager : public IDocumentRegistry {
public:
    /**
     * @brief 获取单例实例
     */
    static DocumentManager* instance();

    /**
     * @brief 析构函数
     */
    virtual ~DocumentManager();

    /**
     * @brief 通知对象属性变化
     * @param object 变化的对象
     * @param changeType 变化类型
     * @param fieldName 字段名称（可选）
     */
    void notifyChange(void* object, ChangeType changeType, const std::string& fieldName = "");

    /**
     * @brief 在事务中标记变化
     * @param dbObject AutoRegisterDB对象
     * @param field 字段名
     * @param changeType 变化类型
     */
    void transWithChange(AutoRegisterDB* dbObject, const std::string& field, ChangeType changeType);

    /**
     * @brief 设置调试模式
     * @param enabled 是否启用调试输出
     */
    void setDebugMode(bool enabled);

    /**
     * @brief 刷新待处理的变化
     *
     * 强制处理当前累积的变化，无论是否在事务中
     */
    void flushPendingChanges();

    // ===== DB实例管理功能 =====

    /**
     * @brief 注册DB实例到管理器
     * @param instance DB实例的智能指针
     * @param dbType DB类型ID
     * @return 分配的唯一DBInstanceID
     */
    DBInstanceID registerDBInstance(std::shared_ptr<AutoRegisterDB> instance, TypeID dbType);

    /**
     * @brief 获取DB实例
     * @param id DB实例ID
     * @return DB实例的智能指针，如果不存在则返回nullptr
     */
    std::shared_ptr<AutoRegisterDB> getDBInstance(const DBInstanceID& id) const;

    /**
     * @brief 通过TypeID获取所有匹配的DB实例
     * @param dbType DB类型ID
     * @return 所有匹配类型的DB实例列表
     */
    std::vector<std::shared_ptr<AutoRegisterDB>> getDBInstancesByType(TypeID dbType) const;

    /**
     * @brief 注销DB实例
     * @param id 要注销的DB实例ID
     * @return 是否成功注销
     */
    bool unregisterDBInstance(const DBInstanceID& id);

    // ===== Ownership graph =====

    struct OwnershipRelationDeclaration {
        TypeID parentType = TypeID::UNKNOWN;
        TypeID childType = TypeID::UNKNOWN;
        std::string relationName;
        bool many = true;
        DBRelationDeletePolicy deletePolicy =
            DBRelationDeletePolicy::CascadeDelete;
    };

    struct OwnershipEdge {
        DBInstanceID parentId;
        DBInstanceID childId;
        std::string relationName;
    };

    struct DependencyRelationEdge {
        DBInstanceID sourceId;
        DBInstanceID targetId;
        std::string relationName;
    };

    bool registerOwnershipRelation(
        TypeID parentType,
        TypeID childType,
        const std::string& relationName,
        bool many,
        DBRelationDeletePolicy deletePolicy =
            DBRelationDeletePolicy::CascadeDelete);
    bool attachOwnedChild(const DBInstanceID& parentId,
                          const DBInstanceID& childId,
                          const std::string& relationName);
    bool detachOwnedChild(const DBInstanceID& childId);

    DBInstanceID getOwner(const DBInstanceID& childId) const;
    std::vector<DBInstanceID> getOwnedChildren(const DBInstanceID& parentId,
                                               const std::string& relationName = "") const;
    std::vector<OwnershipEdge> getOwnershipEdges() const;
    std::vector<DependencyRelationEdge> getDependencyRelationEdges() const;
    void rebuildRelationsFromReferenceFields();

    /**
     * @brief 类型安全的获取DB实例
     * @tparam T 目标DB类型
     * @param id DB实例ID
     * @param expectedType 期望的DB类型
     * @return 类型安全的DB实例指针
     */
    template <typename T>
    std::shared_ptr<T> getTypedDBInstance(const DBInstanceID& id, TypeID expectedType) const {
        std::shared_lock<std::shared_mutex> lock(m_instancesMutex);
        auto it = m_allDBInstances.find(id);
        if (it == m_allDBInstances.end() || it->second.dbType != expectedType) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<T>(it->second.dbInstance);
    }

    /**
     * @brief 获取只读DB实例（用于渲染层）
     * @tparam T 目标DB类型
     * @param id DB实例ID
     * @return const DB实例指针，确保调用者无法修改
     * 
     * 架构原则：渲染层只读取 DB，不修改。
     * 返回 const 指针在编译时强制此约束。
     */
    template <typename T>
    std::shared_ptr<const T> getConstDB(const DBInstanceID& id) const {
        std::shared_lock<std::shared_mutex> lock(m_instancesMutex);
        auto it = m_allDBInstances.find(id);
        if (it == m_allDBInstances.end()) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<const T>(it->second.dbInstance);
    }

    /**
     * @brief 获取可修改的DB实例（用于需要调用非const方法的场景）
     * @tparam T 目标DB类型
     * @param id DB实例ID
     * @return DB实例指针
     * 
     * 注意：仅在必要时使用，如Widget需要更新其内部状态。
     * 大多数情况下应使用 getConstDB()。
     */
    template <typename T>
    std::shared_ptr<T> getDB(const DBInstanceID& id) const {
        std::shared_lock<std::shared_mutex> lock(m_instancesMutex);
        auto it = m_allDBInstances.find(id);
        if (it == m_allDBInstances.end()) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<T>(it->second.dbInstance);
    }

    /**
     * @brief 获取所有DB实例ID
     * @param dbType 过滤的DB类型，UNKNOWN表示获取所有类型
     * @return DB实例ID列表
     */
    std::vector<DBInstanceID> getAllDBInstanceIds(TypeID dbType = TypeID::UNKNOWN) const;

    /**
     * @brief 获取DB实例类型
     * @param id DB实例ID
     * @return DB类型ID
     */
    TypeID getDBInstanceType(const DBInstanceID& id) const;

    /**
     * @brief 注册Handler与窗口的关联
     * @param handler Handler的弱引用（避免循环依赖）
     * @param windowId 关联的窗口ID
     */
    void registerHandlerToWindow(std::weak_ptr<void> handler, const DBInstanceID& windowId);

    /**
     * @brief 注销Handler与窗口的关联
     * @param handler Handler的弱引用
     */
    void unregisterHandlerFromWindow(std::weak_ptr<void> handler);

    /**
     * @brief 设置活动窗口
     * @param windowId 窗口DB的实例ID
     * @return 是否成功设置
     */
    bool setActiveWindow(const DBInstanceID& windowId);

    /**
     * @brief 获取活动窗口ID
     * @return 活动窗口ID，如果没有则返回INVALID_DB_ID
     */
    DBInstanceID getActiveWindowId() const;

    // ===== 调试相关 =====


    /**
     * @brief 清除所有几何对象
     */
    void clearAllGeometry();

    // ===== 撤销/重做功能 =====

    /**
     * @brief 获取撤销/重做管理器
     * @return 撤销/重做管理器的指针
     */
    UndoRedoManager* getUndoRedoManager() const {
        return m_undoRedoManager;
    }

    /**
     * @brief 执行撤销操作
     */
    void undo();

    /**
     * @brief 执行重做操作
     */
    void redo();

    /**
     * @brief 检查是否可以撤销
     */
    bool canUndo() const;

    /**
     * @brief 检查是否可以重做
     */
    bool canRedo() const;


    /**
     * @brief 事务变化监听（新版本，支持DBInstanceID）
     * @param id DB实例ID
     * @param change 变化信息
     */
    void onTransactionChanged(const DBInstanceID& id, const ChangeInfo& change);

    // === 简化的变更通知系统 ===

    /**
     * @brief 变更通知数据结构
     */
    struct ChangeNotification {
        DBInstanceID id;
        TypeID dbType;
        ChangeType changeType;
        std::string propertyName;          // 变化的属性名称
        // Captured before an ownership edge is removed during deletion.
        DBInstanceID ownerId;
        ChangeNotification(const DBInstanceID& _id, TypeID _type, ChangeType _change)
            : id(_id), dbType(_type), changeType(_change) {
        }
    };

    /**
     * @brief 变更监听器类型
     */
    using ChangeListener = std::function<void(const ChangeNotification&)>;
    using ActiveWindowListener = std::function<void(
        const DBInstanceID& oldWindowId,
        const DBInstanceID& newWindowId)>;
    using ListenerID = size_t;

    /**
     * @brief 添加变更监听器
     * @param listener 监听器函数
     * @return 监听器ID，用于后续移除
     */
    ListenerID addChangeListener(ChangeListener listener);

    /**
     * @brief 移除变更监听器
     * @param id 监听器ID
     */
    void removeChangeListener(ListenerID id);

    ListenerID addActiveWindowListener(ActiveWindowListener listener);
    void removeActiveWindowListener(ListenerID id);

    /**
     * @brief 通知所有监听器
     * @param notification 变更通知
     */
    void notifyListeners(const ChangeNotification& notification);

protected:
    /**
     * @brief 私有构造函数（单例模式）
     */
    DocumentManager();

    /**
     * @brief 事务完成处理槽
     */
    void onTransactionCompleted();

    /**
     * @brief 检查是否在批量模式中
     */
    bool isInBatchMode() const;

    /**
     * @brief 处理单个变化
     */
    void processChange(void* object, ChangeType changeType, const std::string& fieldName);

    /**
     * @brief 处理批量变化
     */
    void processBatchChanges();

    /**
     * @brief 根据变化类型确定渲染目标
     */
    RenderTargets determineRenderTargets(ChangeType changeType) const;

    /**
     * @brief 获取对象ID
     */
    std::string getObjectId(void* object) const;

private:
    friend class DocumentRelationAccess;

    static DocumentManager* s_instance;

    /**
     * @brief 待处理的变化记录
     */
    std::vector<ChangeRecord> m_pendingChanges;

    /**
     * @brief 事务管理器引用
     */
    // TransactionManager is now accessed via singleton

    /**
     * @brief 调试模式标志
     */
    bool m_debugMode = false;

    /**
     * @brief 最后一次处理的时间戳
     */
    std::chrono::steady_clock::time_point m_lastProcessTime;

    // ===== DB实例管理相关成员 =====

    /**
     * @brief DB实例信息结构
     */
    struct DBInstanceInfo {
        std::shared_ptr<AutoRegisterDB> dbInstance; // DB实例智能指针
        TypeID dbType;                              // DB类型ID
        int64_t createdTime;                        // 创建时间戳
        bool hasRender;                             // 是否有对应的Render
        RenderID correspondingRenderID;             // 对应的渲染ID
        std::string displayName;                    // 显示名称

        DBInstanceInfo() : dbType(TypeID::UNKNOWN), createdTime(0), hasRender(false) {
        }

        DBInstanceInfo(std::shared_ptr<AutoRegisterDB> instance, TypeID type)
            : dbInstance(instance), dbType(type), createdTime(0), hasRender(false) {
            createdTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
        }
    };

    /**
     * @brief 线程安全保护（读写锁）
     */
    mutable std::shared_mutex m_instancesMutex;

    /**
     * @brief 文档的撤销/重做管理器
     */
    UndoRedoManager* m_undoRedoManager;

    /**
     * @brief 所有DB实例的映射表
     */
    std::unordered_map<DBInstanceID, DBInstanceInfo> m_allDBInstances;

    /**
     * @brief Ownership relation schema and live graph.
     */
    std::vector<OwnershipRelationDeclaration> m_ownershipDeclarations;
    std::unordered_map<DBInstanceID, std::vector<OwnershipEdge>> m_ownedChildren;
    std::unordered_map<DBInstanceID, OwnershipEdge> m_ownerByChild;
    mutable std::shared_mutex m_ownershipMutex;
    std::unordered_map<DBInstanceID, std::vector<DependencyRelationEdge>> m_dependencyRelationsBySource;
    mutable std::shared_mutex m_dependencyRelationMutex;
    bool m_applyingTransactionLifecycle = false;
    bool m_applyingOwnershipTransaction = false;
    bool m_applyingDependencyRelationTransaction = false;

    /**
     * @brief 当前活动窗口的ID
     */
    DBInstanceID m_activeWindowId;

    /**
     * @brief Handler与窗口的关联映射
     * Key: Handler的弱引用（通过地址标识）
     * Value: 关联的窗口ID
     */
    std::map<void*, DBInstanceID> m_handlerToWindow;

    /**
     * @brief 变更监听器管理
     */
    std::map<ListenerID, ChangeListener> m_changeListeners;
    std::map<ListenerID, ActiveWindowListener> m_activeWindowListeners;
    ListenerID m_nextListenerId = 0;
    mutable std::shared_mutex m_listenerMutex;

    void notifyActiveWindowListeners(
        const DBInstanceID& oldWindowId,
        const DBInstanceID& newWindowId);


    /**
     * @brief 生成唯一的DB实例ID
     */
    DBInstanceID generateUniqueId() const;

    /**
     * @brief 检测DB类型（使用虚函数方案）
     */
    TypeID detectDBType(AutoRegisterDB* instance) const;

    // recreateObject 方法已删除 - 现在使用缓存对象和 restoreObject() 方法

    /**
     * @brief 恢复缓存的对象（用于redo）
     * @param id 对象ID
     * @param dbType 对象类型
     * @param object 缓存的对象指针
     */
    void restoreObject(const trans::DBInstanceID& id, trans::TypeID dbType,
                       std::shared_ptr<trans::TransDB> object);

    bool unregisterDBInstanceInternal(const DBInstanceID& id, bool recordTransaction, bool cascadeOwnedChildren);
    bool attachOwnedChildInternal(const DBInstanceID& parentId,
                                  const DBInstanceID& childId,
                                  const std::string& relationName,
                                  bool recordTransaction);
    bool detachOwnedChildInternal(const DBInstanceID& childId, bool recordTransaction);
    bool ownershipRelationAllowed(TypeID parentType, TypeID childType, const std::string& relationName) const;
    bool ownershipWouldCreateCycle(const DBInstanceID& parentId, const DBInstanceID& childId) const;
    bool attachDependencyRelationInternal(const DBInstanceID& sourceId,
                                          const DBInstanceID& targetId,
                                          const std::string& relationName,
                                          bool recordTransaction);
    bool attachDependencyRelation(const DBInstanceID& sourceId,
                                  const DBInstanceID& targetId,
                                  const std::string& relationName);
    bool detachDependencyRelation(const DBInstanceID& sourceId,
                                  const DBInstanceID& targetId,
                                  const std::string& relationName);
    bool detachDependencyRelationInternal(const DBInstanceID& sourceId,
                                          const DBInstanceID& targetId,
                                          const std::string& relationName,
                                          bool recordTransaction,
                                          DBRelationDetachReason reason = DBRelationDetachReason::ExplicitDetach);
    DBInstanceID findDependencyReferenceSource(TypeID sourceType,
                                               const DBInstanceID& targetId,
                                               const std::string& relationName) const;
    void reconcileRelationReference(AutoRegisterDB* childObject,
                                    const std::string& fieldName,
                                    bool recordTransaction);
    void clearParentReferenceForDetachedOwnership(const OwnershipEdge& edge,
                                                  bool recordTransaction);
    DBInstanceID relationReferenceValue(AutoRegisterDB* object,
                                        const std::string& fieldName) const;
    void detachDependencyRelationsForInstance(const DBInstanceID& id, bool recordTransaction);
};
