#pragma once

#include <ChainUpdater.hpp>
#include <DBRelationRegistry.hpp>

class DependencyRuleUpdater final : public ChainUpdater {
public:
    DependencyRuleUpdater(const DBInstanceID& sourceId,
                          const DBInstanceID& targetId,
                          DBDependencyRule rule);

    void update(std::shared_ptr<AutoRegisterDB> source,
                std::shared_ptr<AutoRegisterDB> target,
                ChangeType changeType,
                const std::string& propertyName) override;

    void onAttach(std::shared_ptr<AutoRegisterDB> source,
                  std::shared_ptr<AutoRegisterDB> target);

    void onDetach(std::shared_ptr<AutoRegisterDB> source,
                  std::shared_ptr<AutoRegisterDB> target,
                  DBRelationDetachReason reason);

    bool shouldUpdate(ChangeType changeType, const std::string& propertyName) const override;

    Identity getIdentity() const override {
        return Identity{getMasterId(), getSlaveId(), "DBRelationDependency", m_rule.id};
    }

    uint64_t getRuleId() const {
        return m_rule.id;
    }

    const std::string& getRelationName() const {
        return m_rule.relationName;
    }

private:
    DBDependencyRule m_rule;
};
