#pragma once

#include <DBRelationTypes.hpp>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class DBRelationRegistry {
public:
    static DBRelationRegistry& instance();

    bool registerOwnership(TypeID sourceType,
                           TypeID targetType,
                           const std::string& relationName,
                           bool many,
                           DBRelationDeletePolicy deletePolicy);

    bool registerDependency(TypeID sourceType,
                            TypeID targetType,
                            const std::string& relationName,
                            const std::string& ruleName,
                            std::set<std::string> watchedProperties,
                            DBRelationUpdateFunction update,
                            DBRelationAttachFunction attach = nullptr,
                            DBRelationDetachFunction detach = nullptr);

    bool registerParentReference(TypeID childType,
                                 const std::string& fieldName,
                                 TypeID parentType,
                                 const std::string& relationName);
    bool registerDependencyReference(TypeID targetType,
                                     const std::string& fieldName,
                                     TypeID sourceType,
                                     const std::string& relationName);

    std::optional<DBOwnershipRule> findOwnershipRule(TypeID parentType,
                                                     TypeID childType,
                                                     const std::string& relationName) const;

    std::vector<DBDependencyRule> findDependencyRules(TypeID sourceType,
                                                      TypeID targetType,
                                                      const std::string& relationName) const;

    std::optional<DBRelationReferenceRule> findRelationReferenceRule(TypeID childType,
                                                                     const std::string& fieldName) const;

    std::vector<DBRelationReferenceRule> relationReferenceRules() const;

    bool wouldCreateTypeCycle(TypeID sourceType, TypeID targetType) const;

    void clear();

private:
    DBRelationRegistry() = default;

    bool hasTypePath(TypeID current, TypeID target, std::set<TypeID>& visited) const;
    bool registerRelationReference(TypeID childType,
                                   const std::string& fieldName,
                                   TypeID parentType,
                                   const std::string& relationName,
                                   bool ownsChild,
                                   bool attachesDependency);
    const DBOwnershipRule* findOwnershipRulePtr(TypeID parentType,
                                                TypeID childType,
                                                const std::string& relationName) const;
    const DBDependencyRule* findDependencyRulePtr(TypeID sourceType,
                                                  TypeID targetType,
                                                  const std::string& relationName,
                                                  const std::string& ruleName) const;

    uint64_t m_nextRuleId = 1;
    std::vector<DBOwnershipRule> m_ownershipRules;
    std::vector<DBDependencyRule> m_dependencyRules;
    std::vector<DBRelationReferenceRule> m_relationReferenceRules;
    std::unordered_map<TypeID, std::set<TypeID>> m_typeDependencyGraph;
};
