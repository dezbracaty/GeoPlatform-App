#include <DependencyRuleUpdater.hpp>
#include "Foundation/Log.h"
#include <TransactionManager.hpp>

DependencyRuleUpdater::DependencyRuleUpdater(const DBInstanceID& sourceId,
                                             const DBInstanceID& targetId,
                                             DBDependencyRule rule)
    : ChainUpdater(sourceId, targetId),
      m_rule(std::move(rule)) {
}

void DependencyRuleUpdater::update(std::shared_ptr<AutoRegisterDB> source,
                                   std::shared_ptr<AutoRegisterDB> target,
                                   ChangeType changeType,
                                   const std::string& propertyName) {
    if (!target || !m_rule.update) {
        return;
    }

    DBRelationUpdateContext context;
    context.sourceId = getMasterId();
    context.targetId = getSlaveId();
    context.source = std::move(source);
    context.target = std::move(target);
    context.changeType = changeType;
    context.propertyName = propertyName;
    context.relationName = m_rule.relationName;
    context.ruleName = m_rule.ruleName;

    DerivedUpdateGuard guard;
    m_rule.update(context);
}

void DependencyRuleUpdater::onAttach(std::shared_ptr<AutoRegisterDB> source,
                                     std::shared_ptr<AutoRegisterDB> target) {
    if (!source || !target || !m_rule.attach) {
        return;
    }

    DBRelationAttachContext context;
    context.sourceId = getMasterId();
    context.targetId = getSlaveId();
    context.source = std::move(source);
    context.target = std::move(target);
    context.relationName = m_rule.relationName;
    context.ruleName = m_rule.ruleName;

    DerivedUpdateGuard guard;
    m_rule.attach(context);
}

void DependencyRuleUpdater::onDetach(std::shared_ptr<AutoRegisterDB> source,
                                     std::shared_ptr<AutoRegisterDB> target,
                                     DBRelationDetachReason reason) {
    if (!target || !m_rule.detach) {
        return;
    }

    DBRelationDetachContext context;
    context.sourceId = getMasterId();
    context.targetId = getSlaveId();
    context.source = std::move(source);
    context.target = std::move(target);
    context.relationName = m_rule.relationName;
    context.ruleName = m_rule.ruleName;
    context.reason = reason;

    DerivedUpdateGuard guard;
    m_rule.detach(context);
}

bool DependencyRuleUpdater::shouldUpdate(ChangeType changeType, const std::string& propertyName) const {
    if (changeType != ChangeType::PROPERTY_CHANGED) {
        return false;
    }
    if (m_rule.watchedProperties.empty()) {
        return true;
    }
    return m_rule.watchedProperties.count(propertyName) > 0;
}
