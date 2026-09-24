#include <DBRelationRegistry.hpp>
#include "Foundation/Log.h"
#include <algorithm>

DBRelationRegistry& DBRelationRegistry::instance() {
    static DBRelationRegistry registry;
    return registry;
}

bool DBRelationRegistry::registerOwnership(TypeID sourceType,
                                           TypeID targetType,
                                           const std::string& relationName,
                                           bool many,
                                           DBRelationDeletePolicy deletePolicy) {
    if (sourceType == TypeID::UNKNOWN || targetType == TypeID::UNKNOWN || relationName.empty()) {
        LOG_WARN("DBRelationRegistry::registerOwnership - invalid declaration");
        return false;
    }

    if (const auto* existing = findOwnershipRulePtr(sourceType, targetType, relationName)) {
        if (existing->many == many && existing->deletePolicy == deletePolicy) {
            return true;
        }
        LOG_ERROR("DBRelationRegistry::registerOwnership - conflicting declaration for {} owns {} via '{}'",
                  static_cast<uint32_t>(sourceType),
                  static_cast<uint32_t>(targetType),
                  relationName);
        return false;
    }

    DBOwnershipRule rule;
    rule.id = m_nextRuleId++;
    rule.parentType = sourceType;
    rule.childType = targetType;
    rule.relationName = relationName;
    rule.many = many;
    rule.deletePolicy = deletePolicy;
    m_ownershipRules.push_back(std::move(rule));
    LOG_DEBUG("DBRelationRegistry::registerOwnership - {} owns {} via '{}' cascadeDelete={}",
             static_cast<uint32_t>(sourceType),
             static_cast<uint32_t>(targetType),
             relationName,
             deletePolicy == DBRelationDeletePolicy::CascadeDelete);
    return true;
}

bool DBRelationRegistry::registerDependency(TypeID sourceType,
                                            TypeID targetType,
                                            const std::string& relationName,
                                            const std::string& ruleName,
                                            std::set<std::string> watchedProperties,
                                            DBRelationUpdateFunction update,
                                            DBRelationAttachFunction attach,
                                            DBRelationDetachFunction detach) {
    if (sourceType == TypeID::UNKNOWN || targetType == TypeID::UNKNOWN ||
        relationName.empty() || ruleName.empty() || !update) {
        LOG_WARN("DBRelationRegistry::registerDependency - invalid declaration");
        return false;
    }

    if (const auto* existing = findDependencyRulePtr(sourceType, targetType, relationName, ruleName)) {
        if (existing->watchedProperties == watchedProperties &&
            static_cast<bool>(existing->attach) == static_cast<bool>(attach) &&
            static_cast<bool>(existing->detach) == static_cast<bool>(detach)) {
            return true;
        }
        LOG_ERROR("DBRelationRegistry::registerDependency - conflicting declaration {} -> {} relation='{}' rule='{}'",
                  static_cast<uint32_t>(sourceType),
                  static_cast<uint32_t>(targetType),
                  relationName,
                  ruleName);
        return false;
    }

    if (wouldCreateTypeCycle(sourceType, targetType)) {
        LOG_ERROR("DBRelationRegistry::registerDependency - type cycle rejected {} -> {} rule='{}'",
                  static_cast<uint32_t>(sourceType),
                  static_cast<uint32_t>(targetType),
                  ruleName);
        return false;
    }

    DBDependencyRule rule;
    rule.id = m_nextRuleId++;
    rule.sourceType = sourceType;
    rule.targetType = targetType;
    rule.relationName = relationName;
    rule.ruleName = ruleName;
    rule.watchedProperties = std::move(watchedProperties);
    rule.update = std::move(update);
    rule.attach = std::move(attach);
    rule.detach = std::move(detach);
    m_dependencyRules.push_back(rule);
    m_typeDependencyGraph[sourceType].insert(targetType);

    LOG_DEBUG("DBRelationRegistry::registerDependency - {} -> {} relation='{}' rule='{}' watchedProps={}",
             static_cast<uint32_t>(sourceType),
             static_cast<uint32_t>(targetType),
             relationName,
             ruleName,
             rule.watchedProperties.size());
    return true;
}

bool DBRelationRegistry::registerParentReference(TypeID childType,
                                                 const std::string& fieldName,
                                                 TypeID parentType,
                                                 const std::string& relationName) {
    return registerRelationReference(childType, fieldName, parentType, relationName, true, true);
}

bool DBRelationRegistry::registerDependencyReference(TypeID targetType,
                                                     const std::string& fieldName,
                                                     TypeID sourceType,
                                                     const std::string& relationName) {
    return registerRelationReference(targetType, fieldName, sourceType, relationName, false, true);
}

bool DBRelationRegistry::registerRelationReference(TypeID childType,
                                                   const std::string& fieldName,
                                                   TypeID parentType,
                                                   const std::string& relationName,
                                                   bool ownsChild,
                                                   bool attachesDependency) {
    if (childType == TypeID::UNKNOWN || parentType == TypeID::UNKNOWN ||
        fieldName.empty() || relationName.empty()) {
        LOG_WARN("DBRelationRegistry::registerRelationReference - invalid declaration");
        return false;
    }

    for (const auto& rule : m_relationReferenceRules) {
        if (rule.childType == childType && rule.fieldName == fieldName) {
            if (rule.parentType == parentType &&
                rule.relationName == relationName &&
                rule.ownsChild == ownsChild &&
                rule.attachesDependency == attachesDependency) {
                return true;
            }
            LOG_ERROR("DBRelationRegistry::registerRelationReference - conflicting declaration childType={} field='{}'",
                      static_cast<uint32_t>(childType),
                      fieldName);
            return false;
        }
    }

    m_relationReferenceRules.push_back(DBRelationReferenceRule{
        childType,
        fieldName,
        parentType,
        relationName,
        ownsChild,
        attachesDependency
    });

    LOG_DEBUG("DBRelationRegistry::registerRelationReference - childType={} field='{}' parentType={} relation='{}' ownsChild={} attachesDependency={}",
             static_cast<uint32_t>(childType),
             fieldName,
             static_cast<uint32_t>(parentType),
             relationName,
             ownsChild,
             attachesDependency);
    return true;
}

std::optional<DBOwnershipRule> DBRelationRegistry::findOwnershipRule(
    TypeID sourceType,
    TypeID targetType,
    const std::string& relationName) const {
    for (const auto& rule : m_ownershipRules) {
        if (rule.parentType == sourceType &&
            rule.childType == targetType &&
            rule.relationName == relationName) {
            return rule;
        }
    }
    return std::nullopt;
}

std::vector<DBDependencyRule> DBRelationRegistry::findDependencyRules(
    TypeID sourceType,
    TypeID targetType,
    const std::string& relationName) const {
    std::vector<DBDependencyRule> result;
    for (const auto& rule : m_dependencyRules) {
        if (rule.sourceType == sourceType &&
            rule.targetType == targetType &&
            rule.relationName == relationName) {
            result.push_back(rule);
        }
    }
    return result;
}

std::optional<DBRelationReferenceRule> DBRelationRegistry::findRelationReferenceRule(
    TypeID childType,
    const std::string& fieldName) const {
    for (const auto& rule : m_relationReferenceRules) {
        if (rule.childType == childType && rule.fieldName == fieldName) {
            return rule;
        }
    }
    return std::nullopt;
}

std::vector<DBRelationReferenceRule> DBRelationRegistry::relationReferenceRules() const {
    return m_relationReferenceRules;
}

bool DBRelationRegistry::wouldCreateTypeCycle(TypeID sourceType, TypeID targetType) const {
    std::set<TypeID> visited;
    return hasTypePath(targetType, sourceType, visited);
}

void DBRelationRegistry::clear() {
    m_nextRuleId = 1;
    m_ownershipRules.clear();
    m_dependencyRules.clear();
    m_relationReferenceRules.clear();
    m_typeDependencyGraph.clear();
}

bool DBRelationRegistry::hasTypePath(TypeID current, TypeID target, std::set<TypeID>& visited) const {
    if (current == target) {
        return true;
    }
    if (visited.count(current) > 0) {
        return false;
    }
    visited.insert(current);

    auto it = m_typeDependencyGraph.find(current);
    if (it == m_typeDependencyGraph.end()) {
        return false;
    }

    for (const auto next : it->second) {
        if (hasTypePath(next, target, visited)) {
            return true;
        }
    }
    return false;
}

const DBOwnershipRule* DBRelationRegistry::findOwnershipRulePtr(
    TypeID parentType,
    TypeID childType,
    const std::string& relationName) const {
    auto it = std::find_if(m_ownershipRules.begin(), m_ownershipRules.end(), [&](const DBOwnershipRule& rule) {
        return rule.parentType == parentType &&
               rule.childType == childType &&
               rule.relationName == relationName;
    });
    return it == m_ownershipRules.end() ? nullptr : &(*it);
}

const DBDependencyRule* DBRelationRegistry::findDependencyRulePtr(
    TypeID sourceType,
    TypeID targetType,
    const std::string& relationName,
    const std::string& ruleName) const {
    auto it = std::find_if(m_dependencyRules.begin(), m_dependencyRules.end(), [&](const DBDependencyRule& rule) {
        return rule.sourceType == sourceType &&
               rule.targetType == targetType &&
               rule.relationName == relationName &&
               rule.ruleName == ruleName;
    });
    return it == m_dependencyRules.end() ? nullptr : &(*it);
}
