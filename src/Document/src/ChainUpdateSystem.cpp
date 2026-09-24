#include <ChainUpdateSystem.hpp>
#include <ChainUpdater.hpp>
#include <DBRelationRegistry.hpp>
#include <DependencyRuleUpdater.hpp>
#include <DocumentManager.hpp>
#include "Foundation/Log.h"
#include <algorithm>
#include <sstream>

// 初始化静态成员
std::unique_ptr<ChainUpdateSystem> ChainUpdateSystem::s_instance = nullptr;

ChainUpdateSystem& ChainUpdateSystem::instance() {
    if (!s_instance) {
        s_instance = std::unique_ptr<ChainUpdateSystem>(new ChainUpdateSystem());
    }
    return *s_instance;
}

bool ChainUpdateSystem::attachDependencyFromRelation(const DBInstanceID& masterId,
                                                    const DBInstanceID& slaveId,
                                                    const std::string& relationName) {
    // Relation dependencies are intentionally one-way. Ownership is only lifecycle
    // structure; callers must attach dependency relations explicitly when data sync is needed.
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return false;
    }

    const TypeID masterType = docManager->getDBInstanceType(masterId);
    const TypeID slaveType = docManager->getDBInstanceType(slaveId);
    const auto rules = DBRelationRegistry::instance().findDependencyRules(
        masterType,
        slaveType,
        relationName);
    if (rules.empty()) {
        return false;
    }

    std::vector<std::shared_ptr<ChainUpdater>> updatersToRegister;
    for (const auto& rule : rules) {
        auto updater = std::make_shared<DependencyRuleUpdater>(masterId, slaveId, rule);
        const auto updaterIdentity = updater->getIdentity();

        bool alreadyRegistered = false;
        auto existingIt = m_updaterMap.find(masterId);
        if (existingIt != m_updaterMap.end()) {
            alreadyRegistered = std::any_of(
                existingIt->second.begin(),
                existingIt->second.end(),
                [&](const std::shared_ptr<ChainUpdater>& existing) {
                    return existing && existing->getIdentity() == updaterIdentity;
                });
        }
        if (alreadyRegistered) {
            continue;
        }

        if (wouldCreateCycle(masterId, slaveId)) {
            LOG_ERROR("ChainUpdateSystem: Relation dependency attach rejected by cycle check {} -> {} relation='{}' rule='{}'",
                      masterId.getValue(),
                      slaveId.getValue(),
                      relationName,
                      rule.ruleName);
            return false;
        }

        updatersToRegister.push_back(std::move(updater));
    }

    for (const auto& updater : updatersToRegister) {
        m_updaterMap[masterId].push_back(updater);
        m_reverseDependencies[slaveId].insert(masterId);
        LOG_DEBUG("ChainUpdateSystem: Registered relation updater {} -> {}",
                  masterId.getValue(),
                  slaveId.getValue());
    }

    auto source = docManager->getDBInstance(masterId);
    auto target = docManager->getDBInstance(slaveId);
    if (source && target) {
        for (const auto& updater : updatersToRegister) {
            auto dependencyUpdater = std::dynamic_pointer_cast<DependencyRuleUpdater>(updater);
            if (dependencyUpdater) {
                dependencyUpdater->onAttach(source, target);
            }
        }
    }

    return true;
}

void ChainUpdateSystem::detachDependencyFromRelation(const DBInstanceID& masterId,
                                                    const DBInstanceID& slaveId,
                                                    const std::string& relationName,
                                                    DBRelationDetachReason reason) {
    // Relation lifecycle behavior belongs to DependencyRuleUpdater. DocumentManager
    // owns relation edges; ChainUpdateSystem owns the updater lifecycle for those edges.
    bool stillHasMasterSlaveUpdater = false;
    auto it = m_updaterMap.find(masterId);
    if (it != m_updaterMap.end()) {
        auto* docManager = DocumentManager::instance();
        auto source = docManager ? docManager->getDBInstance(masterId) : nullptr;
        auto target = docManager ? docManager->getDBInstance(slaveId) : nullptr;
        auto& updaters = it->second;
        if (reason != DBRelationDetachReason::TargetDeleted && target) {
            for (const auto& updater : updaters) {
                auto dependencyUpdater = std::dynamic_pointer_cast<DependencyRuleUpdater>(updater);
                if (dependencyUpdater &&
                    dependencyUpdater->getSlaveId() == slaveId &&
                    dependencyUpdater->getRelationName() == relationName) {
                    dependencyUpdater->onDetach(source, target, reason);
                }
            }
        }
        updaters.erase(
            std::remove_if(updaters.begin(), updaters.end(),
                           [&](const std::shared_ptr<ChainUpdater>& updater) {
                               auto dependencyUpdater = std::dynamic_pointer_cast<DependencyRuleUpdater>(updater);
                               return dependencyUpdater &&
                                      dependencyUpdater->getSlaveId() == slaveId &&
                                      dependencyUpdater->getRelationName() == relationName;
                           }),
            updaters.end());
        stillHasMasterSlaveUpdater = std::any_of(
            updaters.begin(),
            updaters.end(),
            [&slaveId](const std::shared_ptr<ChainUpdater>& updater) {
                return updater && updater->getSlaveId() == slaveId;
            });
        if (updaters.empty()) {
            m_updaterMap.erase(it);
        }
    }

    if (!stillHasMasterSlaveUpdater) {
        auto reverseIt = m_reverseDependencies.find(slaveId);
        if (reverseIt != m_reverseDependencies.end()) {
            reverseIt->second.erase(masterId);
            if (reverseIt->second.empty()) {
                m_reverseDependencies.erase(reverseIt);
            }
        }
    }
}

void ChainUpdateSystem::cleanupDependencies(const DBInstanceID& id) {
    // Deletion does not dispatch hidden lifecycle updates. Relation-specific
    // cleanup is owned by DocumentManager relation detach logic.
    m_updaterMap.erase(id);

    // 清理作为从对象的更新器
    auto reverseIt = m_reverseDependencies.find(id);
    if (reverseIt != m_reverseDependencies.end()) {
        for (const auto& masterId : reverseIt->second) {
            auto it = m_updaterMap.find(masterId);
            if (it != m_updaterMap.end()) {
                auto& updaters = it->second;
                updaters.erase(
                    std::remove_if(updaters.begin(), updaters.end(),
                                   [&id](const std::shared_ptr<ChainUpdater>& updater) {
                                       return updater->getSlaveId() == id;
                                   }),
                    updaters.end());

                if (updaters.empty()) {
                    m_updaterMap.erase(it);
                }
            }
        }
        m_reverseDependencies.erase(reverseIt);
    }

    LOG_DEBUG("ChainUpdateSystem: Cleaned up dependencies for {}", id.getValue());
}

void ChainUpdateSystem::triggerUpdate(const DBInstanceID& masterId,
                                      ChangeType changeType,
                                      const std::string& propertyName) {
    // 如果处于批处理模式，收集更新而不是立即执行
    if (m_batchMode) {
        BatchUpdate update{masterId, changeType, propertyName};
        m_batchedUpdates.insert(update);
        // Batched update for chain system
        return;
    }

    runRootUpdate([&](UpdateExecutionContext& context) {
        executeUpdate(context, masterId, changeType, propertyName);
    });
}

void ChainUpdateSystem::beginBatchUpdate() {
    if (m_batchMode) {
        LOG_WARN("ChainUpdateSystem: Already in batch mode");
        return;
    }

    m_batchMode = true;
    m_batchedUpdates.clear();
    // Batch update mode started
}

void ChainUpdateSystem::endBatchUpdate() {
    if (!m_batchMode) {
        LOG_WARN("ChainUpdateSystem: Not in batch mode");
        return;
    }

    m_batchMode = false;

    // 执行所有批处理的更新
    size_t updateCount = m_batchedUpdates.size();
    // Executing batched updates

    runRootUpdate([&](UpdateExecutionContext& context) {
        for (const auto& update : m_batchedUpdates) {
            executeUpdate(context, update.masterId, update.changeType, update.propertyName);
        }
    });

    m_batchedUpdates.clear();
    // Batch update completed
}

void ChainUpdateSystem::runRootUpdate(const std::function<void(UpdateExecutionContext&)>& updateBody) {
    if (!updateBody) {
        return;
    }

    if (m_currentExecution) {
        updateBody(*m_currentExecution);
        return;
    }

    UpdateExecutionContext context;
    m_currentExecution = &context;
    updateBody(context);
    m_currentExecution = nullptr;
}

void ChainUpdateSystem::executeUpdate(UpdateExecutionContext& context,
                                      const DBInstanceID& masterId,
                                      ChangeType changeType,
                                      const std::string& propertyName) {
    ++context.depth;
    updateRecursive(context, masterId, changeType, propertyName);
    --context.depth;
}

void ChainUpdateSystem::updateRecursive(UpdateExecutionContext& context,
                                        const DBInstanceID& masterId,
                                        ChangeType changeType,
                                        const std::string& propertyName) {
    if (context.depth > m_maxUpdateDepth) {
        LOG_ERROR("ChainUpdateSystem: Max update depth exceeded at {}", masterId.getValue());
        return;
    }
    if (++context.updatesThisFlush > m_maxUpdatesPerFlush) {
        LOG_ERROR("ChainUpdateSystem: Max updates per flush exceeded at {}", masterId.getValue());
        return;
    }

    auto it = m_updaterMap.find(masterId);
    if (it != m_updaterMap.end()) {
        auto* docManager = DocumentManager::instance();
        auto masterDB = docManager->getDBInstance(masterId);

        if (!masterDB) {
            LOG_WARN("ChainUpdateSystem: Master object {} not found",
                     masterId.getValue());
            return;
        }

        const auto updatersSnapshot = it->second;
        for (const auto& updater : updatersSnapshot) {
            if (!updater) {
                continue;
            }

            const auto updaterIdentity = updater->getIdentity();
            auto currentIt = m_updaterMap.find(masterId);
            if (currentIt == m_updaterMap.end()) {
                break;
            }
            const bool stillRegistered = std::any_of(
                currentIt->second.begin(),
                currentIt->second.end(),
                [&](const std::shared_ptr<ChainUpdater>& currentUpdater) {
                    return currentUpdater &&
                           currentUpdater->getIdentity() == updaterIdentity;
                });
            if (!stillRegistered) {
                continue;
            }

            if (!updater->shouldUpdate(changeType, propertyName)) {
                continue;
            }

            auto slaveDB = docManager->getDBInstance(updater->getSlaveId());
            if (slaveDB) {
                ActiveUpdateEdge activeEdge{
                    masterId,
                    updater->getSlaveId(),
                    updaterIdentity.kind,
                    updaterIdentity.ruleId,
                    changeType,
                    propertyName
                };
                if (context.activeEdges.count(activeEdge) > 0) {
                    LOG_ERROR("ChainUpdateSystem: Recursive edge update skipped {} -> {} kind='{}' rule={} changeType={} property='{}'",
                              masterId.getValue(),
                              updater->getSlaveId().getValue(),
                              updaterIdentity.kind,
                              updaterIdentity.ruleId,
                              static_cast<int>(changeType),
                              propertyName);
                    continue;
                }

                LOG_DEBUG("ChainUpdateSystem: Updating {} -> {}",
                          masterId.getValue(), updater->getSlaveId().getValue());

                context.activeEdges.insert(activeEdge);
                updater->update(masterDB, slaveDB, changeType, propertyName);
                context.activeEdges.erase(activeEdge);
            } else {
                LOG_WARN("ChainUpdateSystem: Slave object {} not found",
                         updater->getSlaveId().getValue());
            }
        }
    }
}

size_t ChainUpdateSystem::dependencyCount() const {
    size_t count = 0;
    for (const auto& [masterId, updaters] : m_updaterMap) {
        (void)masterId;
        count += updaters.size();
    }
    return count;
}

bool ChainUpdateSystem::wouldCreateCycle(const DBInstanceID& master,
                                         const DBInstanceID& slave) const {
    // 检查是否从slave可以到达master（如果可以，添加master->slave会造成循环）
    std::set<DBInstanceID> visited;
    return hasPathDFS(slave, master, visited);
}

bool ChainUpdateSystem::hasPathDFS(const DBInstanceID& current,
                                   const DBInstanceID& target,
                                   std::set<DBInstanceID>& visited) const {
    // 找到目标
    if (current == target) {
        return true;
    }

    // 已访问过
    if (visited.count(current)) {
        return false;
    }

    visited.insert(current);

    // 遍历current的所有从对象
    auto it = m_updaterMap.find(current);
    if (it != m_updaterMap.end()) {
        for (const auto& updater : it->second) {
            if (hasPathDFS(updater->getSlaveId(), target, visited)) {
                return true;
            }
        }
    }

    return false;
}

std::string ChainUpdateSystem::getDependencyGraph() const {
    std::stringstream ss;
    ss << "=== Chain Update Dependency Graph ===" << std::endl;

    for (const auto& pair : m_updaterMap) {
        const auto& masterId = pair.first;
        ss << "Master: " << masterId.getValue() << std::endl;

        for (const auto& updater : pair.second) {
            ss << "  -> Slave: " << updater->getSlaveId().getValue() << std::endl;
        }
    }

    ss << "=== Reverse Dependencies ===" << std::endl;
    for (const auto& pair : m_reverseDependencies) {
        const auto& slaveId = pair.first;
        ss << "Slave: " << slaveId.getValue() << " depends on:";

        for (const auto& masterId : pair.second) {
            ss << " " << masterId.getValue();
        }
        ss << std::endl;
    }

    return ss.str();
}

void ChainUpdateSystem::clear() {
    m_updaterMap.clear();
    m_reverseDependencies.clear();
    m_currentExecution = nullptr;

    LOG_DEBUG("ChainUpdateSystem: All dependencies cleared");
}
