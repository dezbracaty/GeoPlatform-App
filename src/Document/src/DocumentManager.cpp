#include <DocumentManager.hpp>
#include <UndoRedoManager.hpp>
#include <ChainUpdateSystem.hpp>
#include <DBRelationRegistry.hpp>

#include "transdb.h"
#include "Foundation/Log.h"
#include <sstream>
#include <random>
#include <cmath>
#include <algorithm>
#include <any>

// 静态成员初始化
DocumentManager* DocumentManager::s_instance = nullptr;

DocumentManager* DocumentManager::instance() {
    if (!s_instance) {
        s_instance = new DocumentManager();
    }
    return s_instance;
}

DocumentManager::DocumentManager()
    : m_debugMode(false), m_lastProcessTime(std::chrono::steady_clock::now()), m_activeWindowId(INVALID_DB_ID) {
    // 依赖反转：把自己注册为 BaseDB 的文档注册表实现，供 AutoRegisterDB 经端口调用
    setDocumentRegistry(this);

    // 初始化 UndoRedoManager (friend class can access protected constructor)
    m_undoRedoManager = new UndoRedoManager();

    // 设置事务完成回调
    TransactionManager::instance().onTransactionCommitted =
        [this](const TransactionManager::Transaction&) { onTransactionCompleted(); };

    // 设置对象生命周期回调
    TransactionManager::instance().setObjectLifecycleCallback(
        [this](const trans::DBInstanceID& id, trans::TypeID dbType, bool shouldCreate,
               std::shared_ptr<trans::TransDB> cachedObject) {
            if (shouldCreate) {
                // 重新创建对象（redo 创建操作或 undo 删除操作）
                if (cachedObject) {
                    // 有缓存对象：直接使用（适用于 redo 创建 和 undo 删除）
                    LOG_DEBUG("[DocumentManager] Using cached object, ID: {}", id.getValue());
                    restoreObject(id, dbType, cachedObject);
                } else {
                    // 没有缓存对象：这种情况不应该发生
                    LOG_ERROR("[DocumentManager] No cached object for recreation, ID: {}", id.getValue());
                    // 这是一个严重错误，对象应该总是被缓存
                }
            } else {
                // 删除对象（undo 创建操作或 redo 删除操作）
                DBInstanceID documentId(id.getValue());
                m_applyingTransactionLifecycle = true;
                bool unregisterResult = unregisterDBInstanceInternal(documentId, false, true);
                m_applyingTransactionLifecycle = false;
            }
        });

    TransactionManager::instance().setOwnershipReplayHandler(
        [this](const trans::DBInstanceID& parentId,
               const trans::DBInstanceID& childId,
               const std::string& relationName, bool shouldAttach) {
            m_applyingOwnershipTransaction = true;
            if (shouldAttach) {
                attachOwnedChildInternal(
                    DBInstanceID(parentId.getValue()),
                    DBInstanceID(childId.getValue()), relationName, false);
            } else {
                detachOwnedChildInternal(
                    DBInstanceID(childId.getValue()), false);
            }
            m_applyingOwnershipTransaction = false;
        });

    TransactionManager::instance().setDependencyRelationReplayHandler(
        [this](const trans::DBInstanceID& sourceId, const trans::DBInstanceID& targetId,
               const std::string& relationName, bool shouldAttach) {
            m_applyingDependencyRelationTransaction = true;
            if (shouldAttach) {
                attachDependencyRelationInternal(DBInstanceID(sourceId.getValue()),
                                                 DBInstanceID(targetId.getValue()),
                                                 relationName,
                                                 false);
            } else {
                detachDependencyRelationInternal(DBInstanceID(sourceId.getValue()),
                                                 DBInstanceID(targetId.getValue()),
                                                 relationName,
                                                 false,
                                                 DBRelationDetachReason::UndoRedo);
            }
            m_applyingDependencyRelationTransaction = false;
        });
}

DocumentManager::~DocumentManager() {
    // Clean up UndoRedoManager (friend class can access protected destructor)
    delete m_undoRedoManager;
}

void DocumentManager::notifyChange(void* object, ChangeType changeType,
                                   const std::string& fieldName) {
    if (isInBatchMode()) {
        // 批量模式：累积变化
        ChangeRecord record(object, changeType, fieldName);
        record.renderTargets = determineRenderTargets(changeType);

        m_pendingChanges.push_back(record);

        if (m_debugMode) {
            LOG_DEBUG("DocumentManager: Batched change {} for field {}",
                      changeTypeToString(changeType).toStdString(), fieldName);
        }
    } else {
        // 即时模式：立即处理
        processChange(object, changeType, fieldName);
    }
}

void DocumentManager::transWithChange(AutoRegisterDB* dbObject, const std::string& field,
                                      ChangeType changeType) {
    if (!dbObject)
        return;

    // 在事务中记录变化
    // 无论是否在事务中，都通知变化
    // TransactionManager会自动处理事务内的变化记录
    notifyChange(dbObject, changeType, field);
}

void DocumentManager::setDebugMode(bool enabled) {
    m_debugMode = enabled;
    if (m_debugMode) {
        LOG_DEBUG("DocumentManager: Debug mode {}", enabled ? "enabled" : "disabled");
    }
}

void DocumentManager::flushPendingChanges() {
    if (!m_pendingChanges.empty()) {
        processBatchChanges();
    }
}

DBInstanceID DocumentManager::registerDBInstance(std::shared_ptr<AutoRegisterDB> instance,
                                                 TypeID dbType) {
    if (!instance) {
        return INVALID_DB_ID;
    }

    std::unique_lock<std::shared_mutex> lock(m_instancesMutex);

    DBInstanceID id = instance->getDBInstanceID();

    // 检查是否已存在
    if (m_allDBInstances.find(id) != m_allDBInstances.end()) {
        return id; // 已存在，直接返回
    }

    // 创建实例信息
    DBInstanceInfo info(instance, dbType);
    info.displayName = instance->getDisplayName();

    m_allDBInstances[id] = info;

    lock.unlock();

    rebuildRelationsFromReferenceFields();

    if (m_debugMode) {
        LOG_DEBUG("DocumentManager: Registered DB instance {} type {}",
                  id.getValue(), typeIdToString(dbType).toStdString());
    }

    // 通知监听器
    ChangeNotification notification(id, dbType, ChangeType::OBJECT_CREATED);
    notifyListeners(notification);


    return id;
}

std::shared_ptr<AutoRegisterDB> DocumentManager::getDBInstance(const DBInstanceID& id) const {
    std::shared_lock<std::shared_mutex> lock(m_instancesMutex);
    auto it = m_allDBInstances.find(id);
    if (it != m_allDBInstances.end()) {
        return it->second.dbInstance;
    }
    return nullptr;
}

std::vector<std::shared_ptr<AutoRegisterDB>>
    DocumentManager::getDBInstancesByType(TypeID dbType) const {
    std::vector<std::shared_ptr<AutoRegisterDB>> result;

    std::shared_lock<std::shared_mutex> lock(m_instancesMutex);
    for (const auto& [id, info] : m_allDBInstances) {
        if (dbType == TypeID::UNKNOWN || info.dbType == dbType) {
            result.push_back(info.dbInstance);
        }
    }

    return result;
}

bool DocumentManager::unregisterDBInstance(const DBInstanceID& id) {
    return unregisterDBInstanceInternal(id, !m_applyingTransactionLifecycle, true);
}

bool DocumentManager::unregisterDBInstanceInternal(const DBInstanceID& id,
                                                  bool recordTransaction,
                                                  bool cascadeOwnedChildren) {
    const DBInstanceID previousOwner = getOwner(id);
    if (cascadeOwnedChildren) {
        const auto ownershipEdges = getOwnershipEdges();
        for (const auto& edge : ownershipEdges) {
            if (edge.parentId != id) {
                continue;
            }

            const TypeID parentType = getDBInstanceType(edge.parentId);
            const TypeID childType = getDBInstanceType(edge.childId);
            const auto rule = DBRelationRegistry::instance().findOwnershipRule(
                parentType,
                childType,
                edge.relationName);
            const bool shouldCascade =
                !rule.has_value() ||
                rule->deletePolicy == DBRelationDeletePolicy::CascadeDelete;

            if (shouldCascade) {
                unregisterDBInstanceInternal(edge.childId, recordTransaction, true);
            } else {
                detachOwnedChildInternal(edge.childId, recordTransaction);
            }
        }
    }

    detachOwnedChildInternal(id, recordTransaction);
    detachDependencyRelationsForInstance(id, recordTransaction);

    std::unique_lock<std::shared_mutex> lock(m_instancesMutex);

    auto it = m_allDBInstances.find(id);
    if (it == m_allDBInstances.end()) {
        return false;
    }

    // 保存实例信息（在 erase 之前）
    auto instance = it->second.dbInstance;
    TypeID dbType = it->second.dbType;

    // 移除实例
    m_allDBInstances.erase(it);

    DBInstanceID replacementActiveWindow = m_activeWindowId;
    const bool removedActiveWindow =
        dbType == TypeID::WINDOW_DB && m_activeWindowId == id;
    if (removedActiveWindow) {
        replacementActiveWindow = INVALID_DB_ID;
        for (const auto& [candidateId, candidateInfo] : m_allDBInstances) {
            if (candidateInfo.dbType == TypeID::WINDOW_DB) {
                replacementActiveWindow = candidateId;
                break;
            }
        }
        m_activeWindowId = replacementActiveWindow;
    }

    lock.unlock();

    if (removedActiveWindow) {
        LOG_INFO("DocumentManager: Active window {} removed, replacement is {}",
                 id.getValue(), replacementActiveWindow.getValue());
        notifyActiveWindowListeners(id, replacementActiveWindow);
    }

    if (recordTransaction && TransactionManager::instance().isInTransaction()) {
        TransactionManager::instance().recordObjectDeletion(
            std::static_pointer_cast<trans::TransDB>(instance),
            trans::DBInstanceID(id.getValue()),
            trans::TypeID(static_cast<uint32_t>(dbType)));
    }

    // 清理 ChainUpdateSystem 中的依赖关系
    // 必须在通知监听器之前清理，避免级联更新访问已删除的对象
    ChainUpdateSystem::instance().cleanupDependencies(id);

    if (m_debugMode) {
        LOG_DEBUG("DocumentManager: Unregistered DB instance {}", id.getValue());
    }

    // 通知监听器
    ChangeNotification notification(id, dbType, ChangeType::OBJECT_DELETED);
    notification.ownerId = previousOwner;
    notifyListeners(notification);


    return true;
}

std::vector<DBInstanceID> DocumentManager::getAllDBInstanceIds(TypeID dbType) const {
    std::vector<DBInstanceID> result;

    std::shared_lock<std::shared_mutex> lock(m_instancesMutex);
    for (const auto& [id, info] : m_allDBInstances) {
        if (dbType == TypeID::UNKNOWN || info.dbType == dbType) {
            result.push_back(id);
        }
    }

    return result;
}

TypeID DocumentManager::getDBInstanceType(const DBInstanceID& id) const {
    std::shared_lock<std::shared_mutex> lock(m_instancesMutex);
    auto it = m_allDBInstances.find(id);
    if (it != m_allDBInstances.end()) {
        return it->second.dbType;
    }
    return TypeID::UNKNOWN;
}

bool DocumentManager::registerOwnershipRelation(
    TypeID parentType,
    TypeID childType,
    const std::string& relationName,
    bool many,
    DBRelationDeletePolicy deletePolicy) {
    if (parentType == TypeID::UNKNOWN || childType == TypeID::UNKNOWN ||
        relationName.empty()) {
        LOG_WARN(
            "DocumentManager::registerOwnershipRelation - invalid ownership declaration");
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(m_ownershipMutex);
    for (const auto& declaration : m_ownershipDeclarations) {
        if (declaration.parentType != parentType ||
            declaration.childType != childType ||
            declaration.relationName != relationName) {
            continue;
        }
        if (declaration.many == many &&
            declaration.deletePolicy == deletePolicy) {
            return true;
        }
        LOG_ERROR(
            "DocumentManager::registerOwnershipRelation - conflicting ownership declaration");
        return false;
    }

    if (!DBRelationRegistry::instance().registerOwnership(
            parentType, childType, relationName, many, deletePolicy)) {
        return false;
    }
    m_ownershipDeclarations.push_back({
        parentType, childType, relationName, many, deletePolicy});
    return true;
}

bool DocumentManager::ownershipRelationAllowed(TypeID parentType,
                                               TypeID childType,
                                               const std::string& relationName) const {
    return std::any_of(
        m_ownershipDeclarations.begin(), m_ownershipDeclarations.end(),
        [parentType, childType, &relationName](const auto& declaration) {
            return declaration.parentType == parentType &&
                declaration.childType == childType &&
                declaration.relationName == relationName;
        });
}

bool DocumentManager::ownershipWouldCreateCycle(const DBInstanceID& parentId,
                                                const DBInstanceID& childId) const {
    DBInstanceID current = parentId;
    while (current.isValid()) {
        if (current == childId) {
            return true;
        }
        auto it = m_ownerByChild.find(current);
        if (it == m_ownerByChild.end()) {
            break;
        }
        current = it->second.parentId;
    }
    return false;
}

bool DocumentManager::attachOwnedChild(
    const DBInstanceID& parentId,
    const DBInstanceID& childId,
    const std::string& relationName) {
    return attachOwnedChildInternal(
        parentId, childId, relationName,
        !m_applyingOwnershipTransaction);
}

bool DocumentManager::detachOwnedChild(const DBInstanceID& childId) {
    return detachOwnedChildInternal(
        childId, !m_applyingOwnershipTransaction);
}

bool DocumentManager::attachOwnedChildInternal(const DBInstanceID& parentId,
                                               const DBInstanceID& childId,
                                               const std::string& relationName,
                                               bool recordTransaction) {
    if (!parentId.isValid() || !childId.isValid() || parentId == childId || relationName.empty()) {
        LOG_WARN("DocumentManager::attachOwnedChild - invalid edge parent={} child={} relation='{}'",
                 parentId.getValue(), childId.getValue(), relationName);
        return false;
    }

    const TypeID parentType = getDBInstanceType(parentId);
    const TypeID childType = getDBInstanceType(childId);

    std::unique_lock<std::shared_mutex> lock(m_ownershipMutex);
    if (!ownershipRelationAllowed(parentType, childType, relationName)) {
        LOG_WARN("DocumentManager::attachOwnedChild - relation not declared: parentType={} childType={} relation='{}'",
                 static_cast<uint32_t>(parentType), static_cast<uint32_t>(childType), relationName);
        return false;
    }
    if (ownershipWouldCreateCycle(parentId, childId)) {
        LOG_ERROR("DocumentManager::attachOwnedChild - ownership cycle rejected parent={} child={}",
                  parentId.getValue(), childId.getValue());
        return false;
    }

    auto existingOwnerIt = m_ownerByChild.find(childId);
    if (existingOwnerIt != m_ownerByChild.end()) {
        if (existingOwnerIt->second.parentId == parentId &&
            existingOwnerIt->second.relationName == relationName) {
            return true;
        }
        LOG_WARN("DocumentManager::attachOwnedChild - child {} already owned by {}",
                 childId.getValue(), existingOwnerIt->second.parentId.getValue());
        return false;
    }

    const auto declaration = std::find_if(
        m_ownershipDeclarations.begin(), m_ownershipDeclarations.end(),
        [parentType, childType, &relationName](const auto& candidate) {
            return candidate.parentType == parentType &&
                candidate.childType == childType &&
                candidate.relationName == relationName;
        });
    if (declaration != m_ownershipDeclarations.end() && !declaration->many) {
        const auto parentIt = m_ownedChildren.find(parentId);
        if (parentIt != m_ownedChildren.end()) {
            for (const auto& edge : parentIt->second) {
                if (edge.relationName != relationName) continue;
                LOG_WARN("DocumentManager::attachOwnedChild - single relation '{}' already has child",
                         relationName);
                return false;
            }
        }
    }

    OwnershipEdge edge{parentId, childId, relationName};
    m_ownedChildren[parentId].push_back(edge);
    m_ownerByChild[childId] = edge;
    lock.unlock();

    if (recordTransaction &&
        TransactionManager::instance().isInTransaction()) {
        TransactionManager::instance().recordOwnershipChange(
            trans::DBInstanceID(parentId.getValue()),
            trans::DBInstanceID(childId.getValue()), relationName, true);
    }

    LOG_DEBUG("DocumentManager::attachOwnedChild - {} owns {} via '{}'",
              parentId.getValue(), childId.getValue(), relationName);
    if (const auto parent = getDBInstance(parentId)) {
        notifyChange(
            parent.get(), ChangeType::PROPERTY_CHANGED, relationName);
    }
    return true;
}

bool DocumentManager::detachOwnedChildInternal(const DBInstanceID& childId, bool recordTransaction) {
    if (!childId.isValid()) {
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(m_ownershipMutex);
    auto ownerIt = m_ownerByChild.find(childId);
    if (ownerIt == m_ownerByChild.end()) {
        return false;
    }

    const OwnershipEdge edge = ownerIt->second;
    m_ownerByChild.erase(ownerIt);

    auto childrenIt = m_ownedChildren.find(edge.parentId);
    if (childrenIt != m_ownedChildren.end()) {
        auto& children = childrenIt->second;
        children.erase(std::remove_if(children.begin(), children.end(),
                                      [&childId](const OwnershipEdge& candidate) {
                                          return candidate.childId == childId;
                                      }),
                       children.end());
        if (children.empty()) {
            m_ownedChildren.erase(childrenIt);
        }
    }
    lock.unlock();

    if (recordTransaction &&
        TransactionManager::instance().isInTransaction()) {
        TransactionManager::instance().recordOwnershipChange(
            trans::DBInstanceID(edge.parentId.getValue()),
            trans::DBInstanceID(edge.childId.getValue()),
            edge.relationName, false);
    }

    clearParentReferenceForDetachedOwnership(edge, recordTransaction);

    if (const auto parent = getDBInstance(edge.parentId)) {
        notifyChange(
            parent.get(), ChangeType::PROPERTY_CHANGED,
            edge.relationName);
    }

    LOG_DEBUG("DocumentManager::detachOwnedChild - detached child {} from parent {} relation '{}'",
              childId.getValue(), edge.parentId.getValue(), edge.relationName);
    return true;
}

DBInstanceID DocumentManager::getOwner(const DBInstanceID& childId) const {
    std::shared_lock<std::shared_mutex> lock(m_ownershipMutex);
    auto it = m_ownerByChild.find(childId);
    return it == m_ownerByChild.end() ? DBInstanceID() : it->second.parentId;
}

std::vector<DBInstanceID> DocumentManager::getOwnedChildren(const DBInstanceID& parentId,
                                                           const std::string& relationName) const {
    std::vector<DBInstanceID> result;
    std::shared_lock<std::shared_mutex> lock(m_ownershipMutex);
    auto it = m_ownedChildren.find(parentId);
    if (it == m_ownedChildren.end()) {
        return result;
    }
    for (const auto& edge : it->second) {
        if (relationName.empty() || edge.relationName == relationName) {
            result.push_back(edge.childId);
        }
    }
    return result;
}

std::vector<DocumentManager::OwnershipEdge> DocumentManager::getOwnershipEdges() const {
    std::vector<OwnershipEdge> result;
    std::shared_lock<std::shared_mutex> lock(m_ownershipMutex);
    for (const auto& [parentId, edges] : m_ownedChildren) {
        (void)parentId;
        result.insert(result.end(), edges.begin(), edges.end());
    }
    return result;
}

bool DocumentManager::attachDependencyRelation(const DBInstanceID& sourceId,
                                               const DBInstanceID& targetId,
                                               const std::string& relationName) {
    return attachDependencyRelationInternal(
        sourceId,
        targetId,
        relationName,
        !m_applyingDependencyRelationTransaction);
}

bool DocumentManager::attachDependencyRelationInternal(const DBInstanceID& sourceId,
                                                       const DBInstanceID& targetId,
                                                       const std::string& relationName,
                                                       bool recordTransaction) {
    if (!sourceId.isValid() || !targetId.isValid() || sourceId == targetId || relationName.empty()) {
        LOG_WARN("DocumentManager::attachDependencyRelation - invalid edge source={} target={} relation='{}'",
                 sourceId.getValue(), targetId.getValue(), relationName);
        return false;
    }

    const TypeID sourceType = getDBInstanceType(sourceId);
    const TypeID targetType = getDBInstanceType(targetId);
    const auto rules = DBRelationRegistry::instance().findDependencyRules(sourceType, targetType, relationName);
    if (rules.empty()) {
        LOG_WARN("DocumentManager::attachDependencyRelation - relation not declared: sourceType={} targetType={} relation='{}'",
                 static_cast<uint32_t>(sourceType), static_cast<uint32_t>(targetType), relationName);
        return false;
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_dependencyRelationMutex);
        auto& edges = m_dependencyRelationsBySource[sourceId];
        const auto existing = std::find_if(edges.begin(), edges.end(), [&](const DependencyRelationEdge& edge) {
            return edge.targetId == targetId && edge.relationName == relationName;
        });
        if (existing != edges.end()) {
            return true;
        }
        edges.push_back(DependencyRelationEdge{sourceId, targetId, relationName});
    }

    if (!ChainUpdateSystem::instance().attachDependencyFromRelation(sourceId, targetId, relationName)) {
        std::unique_lock<std::shared_mutex> lock(m_dependencyRelationMutex);
        auto sourceIt = m_dependencyRelationsBySource.find(sourceId);
        if (sourceIt != m_dependencyRelationsBySource.end()) {
            auto& edges = sourceIt->second;
            edges.erase(std::remove_if(edges.begin(), edges.end(), [&](const DependencyRelationEdge& edge) {
                            return edge.targetId == targetId && edge.relationName == relationName;
                        }),
                        edges.end());
            if (edges.empty()) {
                m_dependencyRelationsBySource.erase(sourceIt);
            }
        }
        return false;
    }

    if (recordTransaction && TransactionManager::instance().isInTransaction()) {
        TransactionManager::instance().recordDependencyRelationChange(
            trans::DBInstanceID(sourceId.getValue()),
            trans::DBInstanceID(targetId.getValue()),
            relationName,
            true);
    }

    return true;
}

bool DocumentManager::detachDependencyRelation(const DBInstanceID& sourceId,
                                               const DBInstanceID& targetId,
                                               const std::string& relationName) {
    return detachDependencyRelationInternal(
        sourceId,
        targetId,
        relationName,
        !m_applyingDependencyRelationTransaction);
}

bool DocumentManager::detachDependencyRelationInternal(const DBInstanceID& sourceId,
                                                       const DBInstanceID& targetId,
                                                       const std::string& relationName,
                                                       bool recordTransaction,
                                                       DBRelationDetachReason reason) {
    if (!sourceId.isValid() || !targetId.isValid() || relationName.empty()) {
        return false;
    }

    bool removed = false;
    {
        std::unique_lock<std::shared_mutex> lock(m_dependencyRelationMutex);
        auto sourceIt = m_dependencyRelationsBySource.find(sourceId);
        if (sourceIt == m_dependencyRelationsBySource.end()) {
            return false;
        }
        auto& edges = sourceIt->second;
        const auto oldSize = edges.size();
        edges.erase(std::remove_if(edges.begin(), edges.end(), [&](const DependencyRelationEdge& edge) {
                        return edge.targetId == targetId && edge.relationName == relationName;
                    }),
                    edges.end());
        removed = edges.size() != oldSize;
        if (edges.empty()) {
            m_dependencyRelationsBySource.erase(sourceIt);
        }
    }

    if (!removed) {
        return false;
    }

    ChainUpdateSystem::instance().detachDependencyFromRelation(sourceId, targetId, relationName, reason);

    if (recordTransaction && TransactionManager::instance().isInTransaction()) {
        TransactionManager::instance().recordDependencyRelationChange(
            trans::DBInstanceID(sourceId.getValue()),
            trans::DBInstanceID(targetId.getValue()),
            relationName,
            false);
    }

    return true;
}

std::vector<DocumentManager::DependencyRelationEdge> DocumentManager::getDependencyRelationEdges() const {
    std::vector<DependencyRelationEdge> result;
    std::shared_lock<std::shared_mutex> lock(m_dependencyRelationMutex);
    for (const auto& [sourceId, edges] : m_dependencyRelationsBySource) {
        (void)sourceId;
        result.insert(result.end(), edges.begin(), edges.end());
    }
    return result;
}

DBInstanceID DocumentManager::findDependencyReferenceSource(
    TypeID sourceType, const DBInstanceID& targetId,
    const std::string& relationName) const {
    std::shared_lock<std::shared_mutex> lock(m_dependencyRelationMutex);
    for (const auto& [sourceId, edges] : m_dependencyRelationsBySource) {
        if (sourceType != getDBInstanceType(sourceId)) {
            continue;
        }
        for (const auto& edge : edges) {
            if (edge.targetId == targetId &&
                edge.relationName == relationName) {
                return sourceId;
            }
        }
    }
    return DBInstanceID{};
}

DBInstanceID DocumentManager::relationReferenceValue(
    AutoRegisterDB* object, const std::string& fieldName) const {
    if (!object || fieldName.empty()) return DBInstanceID{};
    const std::any value = object->getPropertyByName(fieldName);
    if (!value.has_value()) return DBInstanceID{};
    try {
        if (value.type() == typeid(DBInstanceID)) {
            return std::any_cast<DBInstanceID>(value);
        }
        if (value.type() == typeid(uint64_t)) {
            return DBInstanceID(static_cast<DBInstanceID::ValueType>(
                std::any_cast<uint64_t>(value)));
        }
        if (value.type() == typeid(int)) {
            return DBInstanceID(static_cast<DBInstanceID::ValueType>(
                std::any_cast<int>(value)));
        }
    } catch (const std::bad_any_cast&) {
    }
    return DBInstanceID{};
}

void DocumentManager::reconcileRelationReference(
    AutoRegisterDB* childObject, const std::string& fieldName,
    bool recordTransaction) {
    if (!childObject || fieldName.empty()) return;
    const auto rule = DBRelationRegistry::instance().findRelationReferenceRule(
        childObject->getTypeID(), fieldName);
    if (!rule) return;

    const DBInstanceID childId = childObject->getDBInstanceID();
    const DBInstanceID desiredParentId =
        relationReferenceValue(childObject, fieldName);
    const DBInstanceID currentParentId = rule->ownsChild
        ? getOwner(childId)
        : findDependencyReferenceSource(
              rule->parentType, childId, rule->relationName);

    if (currentParentId == desiredParentId) {
        if (desiredParentId.isValid() && rule->attachesDependency) {
            attachDependencyRelationInternal(
                desiredParentId, childId, rule->relationName,
                recordTransaction);
        }
        return;
    }

    if (currentParentId.isValid()) {
        if (rule->attachesDependency) {
            detachDependencyRelationInternal(
                currentParentId, childId, rule->relationName,
                recordTransaction, DBRelationDetachReason::ExplicitDetach);
        }
        if (rule->ownsChild) {
            detachOwnedChildInternal(childId, recordTransaction);
        }
    }
    if (!desiredParentId.isValid()) return;

    const TypeID desiredParentType = getDBInstanceType(desiredParentId);
    if (rule->parentType != desiredParentType) {
        LOG_WARN("DocumentManager::reconcileRelationReference - field '{}' points to invalid parent type child={} parent={} expectedType={} actualType={}",
                 fieldName, childId.getValue(), desiredParentId.getValue(),
                 static_cast<uint32_t>(rule->parentType),
                 static_cast<uint32_t>(desiredParentType));
        return;
    }
    if (rule->ownsChild &&
        !attachOwnedChildInternal(
            desiredParentId, childId, rule->relationName,
            recordTransaction)) {
        return;
    }
    if (rule->attachesDependency) {
        attachDependencyRelationInternal(
            desiredParentId, childId, rule->relationName,
            recordTransaction);
    }
}

void DocumentManager::clearParentReferenceForDetachedOwnership(
    const OwnershipEdge& edge, bool recordTransaction) {
    const auto childObject = getDBInstance(edge.childId);
    if (!childObject) return;
    for (const auto& rule :
         DBRelationRegistry::instance().relationReferenceRules()) {
        if (rule.childType != childObject->getTypeID() ||
            !rule.ownsChild ||
            rule.parentType != getDBInstanceType(edge.parentId) ||
            rule.relationName != edge.relationName ||
            relationReferenceValue(childObject.get(), rule.fieldName) !=
                edge.parentId) {
            continue;
        }
        const auto prop = childObject->findExistingPropByName(rule.fieldName);
        if (!prop) continue;
        if (recordTransaction) {
            childObject->setPropertyAny(*prop, std::any(DBInstanceID{}));
        } else {
            childObject->setPropertyImpl(*prop, std::any(DBInstanceID{}));
        }
    }
}

void DocumentManager::rebuildRelationsFromReferenceFields() {
    const auto referenceRules = DBRelationRegistry::instance().relationReferenceRules();
    if (referenceRules.empty()) {
        return;
    }

    std::vector<std::shared_ptr<AutoRegisterDB>> instances;
    {
        std::shared_lock<std::shared_mutex> lock(m_instancesMutex);
        instances.reserve(m_allDBInstances.size());
        for (const auto& [id, info] : m_allDBInstances) {
            (void)id;
            if (info.dbInstance) {
                instances.push_back(info.dbInstance);
            }
        }
    }

    for (const auto& instance : instances) {
        for (const auto& rule : referenceRules) {
            if (instance && instance->getTypeID() == rule.childType) {
                reconcileRelationReference(
                    instance.get(), rule.fieldName, false);
            }
        }
    }
}

void DocumentManager::detachDependencyRelationsForInstance(const DBInstanceID& id, bool recordTransaction) {
    const auto edges = getDependencyRelationEdges();
    for (const auto& edge : edges) {
        if (edge.sourceId == id || edge.targetId == id) {
            const auto reason = edge.sourceId == id
                ? DBRelationDetachReason::SourceDeleted
                : DBRelationDetachReason::TargetDeleted;
            detachDependencyRelationInternal(edge.sourceId, edge.targetId, edge.relationName, recordTransaction, reason);
        }
    }
}

void DocumentManager::onTransactionChanged(const DBInstanceID& id, const ChangeInfo& change) {
    // 处理事务变化
    if (m_debugMode) {
        LOG_DEBUG("DocumentManager: Transaction change for {} type {} field {}",
                  id.getValue(), changeTypeToString(change.type).toStdString(), change.fieldName);
    }

    // 创建通知
    TypeID dbType = getDBInstanceType(id);
    ChangeNotification notification(id, dbType, change.type);
    notification.propertyName = change.fieldName;
    notifyListeners(notification);
}

// === 简化的变更监听器实现 ===

DocumentManager::ListenerID DocumentManager::addChangeListener(ChangeListener listener) {
    if (!listener) {
        return 0;
    }

    std::unique_lock<std::shared_mutex> lock(m_listenerMutex);
    ListenerID id = ++m_nextListenerId;
    m_changeListeners[id] = listener;

    if (m_debugMode) {
        LOG_DEBUG("DocumentManager: Added change listener {}", id);
    }

    return id;
}

void DocumentManager::removeChangeListener(ListenerID id) {
    std::unique_lock<std::shared_mutex> lock(m_listenerMutex);
    m_changeListeners.erase(id);

    if (m_debugMode) {
        LOG_DEBUG("DocumentManager: Removed change listener {}", id);
    }
}

DocumentManager::ListenerID DocumentManager::addActiveWindowListener(
    ActiveWindowListener listener) {
    if (!listener) {
        return 0;
    }

    std::unique_lock<std::shared_mutex> lock(m_listenerMutex);
    const ListenerID id = ++m_nextListenerId;
    m_activeWindowListeners[id] = std::move(listener);
    return id;
}

void DocumentManager::removeActiveWindowListener(ListenerID id) {
    std::unique_lock<std::shared_mutex> lock(m_listenerMutex);
    m_activeWindowListeners.erase(id);
}

void DocumentManager::notifyActiveWindowListeners(
    const DBInstanceID& oldWindowId,
    const DBInstanceID& newWindowId) {
    std::vector<ActiveWindowListener> listeners;
    {
        std::shared_lock<std::shared_mutex> lock(m_listenerMutex);
        listeners.reserve(m_activeWindowListeners.size());
        for (const auto& [id, listener] : m_activeWindowListeners) {
            (void)id;
            if (listener) {
                listeners.push_back(listener);
            }
        }
    }

    for (const auto& listener : listeners) {
        listener(oldWindowId, newWindowId);
    }
}

void DocumentManager::notifyListeners(const ChangeNotification& notification) {
    std::vector<ChangeListener> listeners;
    {
        std::shared_lock<std::shared_mutex> lock(m_listenerMutex);
        listeners.reserve(m_changeListeners.size());
        for (const auto& [id, listener] : m_changeListeners) {
            if (listener) {
                listeners.push_back(listener);
            } else {
                LOG_WARN("DocumentManager::notifyListeners - Listener {} is null!", id);
            }
        }
    }

    // Never hold the registry lock while invoking application code. A
    // concrete relationship may emit a targeted parent invalidation here.
    for (const auto& listener : listeners) listener(notification);
}

void DocumentManager::onTransactionCompleted() {
    size_t drainCount = 0;
    while (!m_pendingChanges.empty() && drainCount < 32) {
        processBatchChanges();
        ++drainCount;
    }
    if (!m_pendingChanges.empty()) {
        LOG_WARN("DocumentManager::onTransactionCompleted - pending change drain limit reached, remaining={}",
                 m_pendingChanges.size());
    }
}

bool DocumentManager::isInBatchMode() const {
    // 如果启用了强制立即通知，则不使用批处理模式
    if (ImmediateNotifyGuard::isForceImmediate()) {
        return false;
    }

    // 否则按照事务状态决定是否批处理
    return TransactionManager::instance().isInTransaction();
}

void DocumentManager::processChange(void* object, ChangeType changeType,
                                    const std::string& fieldName) {

    if (m_debugMode) {
        LOG_DEBUG("DocumentManager: Processing immediate change {} field: {}",
                  changeTypeToString(changeType).toStdString(), fieldName);
    }

    // 获取DB实例信息并通知监听器
    if (auto* dbObject = static_cast<AutoRegisterDB*>(object)) {
        DBInstanceID instanceId = dbObject->getDBInstanceID();
        TypeID dbType = dbObject->getTypeID();

        if (changeType == ChangeType::PROPERTY_CHANGED) {
            DerivedUpdateGuard guard;
            reconcileRelationReference(dbObject, fieldName, true);
        }

        // 创建通知
        ChangeNotification notification(instanceId, dbType, changeType);
        notification.propertyName = fieldName;

        // 通知所有监听器
        notifyListeners(notification);

        // 触发级联更新系统
        ChainUpdateSystem::instance().triggerUpdate(instanceId, changeType, fieldName);

    } else {
        LOG_ERROR("DocumentManager::processChange - object is not AutoRegisterDB! object: {}",
                  static_cast<void*>(object));
    }
}

void DocumentManager::processBatchChanges() {
    if (m_pendingChanges.empty()) {
        return;
    }

    auto pendingChanges = std::move(m_pendingChanges);
    m_pendingChanges.clear();

    if (m_debugMode) {
        LOG_DEBUG("DocumentManager: Processing batch changes, count: {}", pendingChanges.size());
    }

    // 批量通知监听器
    for (const auto& record : pendingChanges) {
        // 尝试获取DB实例信息
        if (auto* dbObject = static_cast<AutoRegisterDB*>(record.object)) {
            DBInstanceID instanceId = dbObject->getDBInstanceID();
            TypeID dbType = dbObject->getTypeID();

            if (record.changeType == ChangeType::PROPERTY_CHANGED) {
                DerivedUpdateGuard guard;
                reconcileRelationReference(
                    dbObject, record.fieldName, true);
            }

            // 创建通知
            ChangeNotification notification(instanceId, dbType, record.changeType);
            notification.propertyName = record.fieldName;

            notifyListeners(notification);

            // 触发级联更新系统
            ChainUpdateSystem::instance().triggerUpdate(instanceId, record.changeType, record.fieldName);
        }
    }

    m_lastProcessTime = std::chrono::steady_clock::now();
}

RenderTargets DocumentManager::determineRenderTargets(ChangeType changeType) const {
    return getRequiredRenderTargets(changeType);
}

std::string DocumentManager::getObjectId(void* object) const {
    // 尝试将对象转换为AutoRegisterDB
    AutoRegisterDB* dbObject = static_cast<AutoRegisterDB*>(object);
    if (dbObject) {
        return dbObject->getDBInstanceID().toString();
    }

    // 如果不是AutoRegisterDB，使用指针地址作为ID
    std::ostringstream oss;
    oss << "ptr_" << object;
    return oss.str();
}

DBInstanceID DocumentManager::generateUniqueId() const {
    return DBInstanceID::generate();
}

TypeID DocumentManager::detectDBType(AutoRegisterDB* instance) const {
    if (instance) {
        return instance->getTypeID();
    }
    return TypeID::UNKNOWN;
}

// ===== 工具方法实现 =====

void DocumentManager::clearAllGeometry() {
    TransactionManager::instance().beginTransaction("Clear All Geometry");

    const TypeID geometryTypes[] = {
        TypeID::CUBE_DB,
        TypeID::SPHERE_DB,
        TypeID::CONE_DB,
        TypeID::CYLINDER_DB,
        TypeID::GLB_DB,
        TypeID::MODEL_OBJECT_DB,
    };
    for (const TypeID type : geometryTypes) {
        const auto ids = getAllDBInstanceIds(type);
        for (const auto& id : ids) {
            unregisterDBInstance(id);
        }
    }

    TransactionManager::instance().commitTransaction();
}

void DocumentManager::restoreObject(const trans::DBInstanceID& id, trans::TypeID dbType,
                                    std::shared_ptr<trans::TransDB> object) {
    TypeID systemTypeId = static_cast<TypeID>(dbType.getValue());

    LOG_DEBUG("[DocumentManager::restoreObject] Restoring cached object - ID: {}, Type: {}",
              id.getValue(), static_cast<uint32_t>(systemTypeId));

    // 尝试转换为 AutoRegisterDB
    auto autoRegDB = std::dynamic_pointer_cast<AutoRegisterDB>(object);
    if (!autoRegDB) {
        LOG_ERROR("[DocumentManager::restoreObject] Failed to cast to AutoRegisterDB");
        return;
    }

    // 直接注册缓存的对象
    registerDBInstance(autoRegDB, systemTypeId);
}

// recreateObject 方法已删除 - 现在使用缓存对象和 restoreObject() 方法

// ===== 撤销/重做功能实现 =====

void DocumentManager::undo() {
    if (m_undoRedoManager) {
        m_undoRedoManager->undo();
    }
}

void DocumentManager::redo() {
    if (m_undoRedoManager) {
        m_undoRedoManager->redo();
    }
}

bool DocumentManager::canUndo() const {
    return m_undoRedoManager ? m_undoRedoManager->canUndo() : false;
}

bool DocumentManager::canRedo() const {
    return m_undoRedoManager ? m_undoRedoManager->canRedo() : false;
}

bool DocumentManager::setActiveWindow(const DBInstanceID& windowId) {
    DBInstanceID oldActiveId = INVALID_DB_ID;
    {
        std::unique_lock<std::shared_mutex> lock(m_instancesMutex);

        // 验证窗口存在
        auto it = m_allDBInstances.find(windowId);
        if (it == m_allDBInstances.end() || it->second.dbType != TypeID::WINDOW_DB) {
            LOG_ERROR("DocumentManager::setActiveWindow - Window ID {} not found", windowId.getValue());
            return false;
        }

        oldActiveId = m_activeWindowId;
        if (oldActiveId == windowId) {
            return true;
        }
        m_activeWindowId = windowId;
    }

    LOG_INFO("DocumentManager: Active window changed from {} to {}",
             oldActiveId.getValue(), windowId.getValue());
    notifyActiveWindowListeners(oldActiveId, windowId);
    return true;
}

DBInstanceID DocumentManager::getActiveWindowId() const {
    std::shared_lock<std::shared_mutex> lock(m_instancesMutex);
    return m_activeWindowId;
}

void DocumentManager::registerHandlerToWindow(std::weak_ptr<void> handler, const DBInstanceID& windowId) {
    // 使用弱引用的地址作为唯一标识
    auto handlerPtr = handler.lock();
    if (!handlerPtr) {
        LOG_WARN("DocumentManager::registerHandlerToWindow - Handler has expired");
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_instancesMutex);

    // 验证窗口存在
    if (m_allDBInstances.find(windowId) == m_allDBInstances.end()) {
        LOG_ERROR("DocumentManager::registerHandlerToWindow - Window ID {} not found", windowId.getValue());
        return;
    }

    void* handlerAddress = handlerPtr.get();
    m_handlerToWindow[handlerAddress] = windowId;

    LOG_DEBUG("DocumentManager: Registered handler {} to window {}",
              handlerAddress, windowId.getValue());
}

void DocumentManager::unregisterHandlerFromWindow(std::weak_ptr<void> handler) {
    auto handlerPtr = handler.lock();
    if (!handlerPtr) {
        // Handler 已经被销毁，这是正常情况
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_instancesMutex);
    void* handlerAddress = handlerPtr.get();

    auto it = m_handlerToWindow.find(handlerAddress);
    if (it != m_handlerToWindow.end()) {
        LOG_DEBUG("DocumentManager: Unregistered handler {} from window {}",
                  handlerAddress, it->second.getValue());
        m_handlerToWindow.erase(it);
    }
}
