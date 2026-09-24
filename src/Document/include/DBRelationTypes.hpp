#pragma once

#include <BaseID.hpp>
#include <ChangeTypes.hpp>
#include <SystemTypes.hpp>
#include <functional>
#include <memory>
#include <set>
#include <string>

class AutoRegisterDB;

enum class DBRelationDeletePolicy {
    DetachChild,
    CascadeDelete
};

enum class DBRelationDetachReason {
    ExplicitDetach,
    SourceDeleted,
    TargetDeleted,
    UndoRedo
};

struct DBRelationUpdateContext {
    DBInstanceID sourceId;
    DBInstanceID targetId;
    std::shared_ptr<AutoRegisterDB> source;
    std::shared_ptr<AutoRegisterDB> target;
    ChangeType changeType = ChangeType::UNKNOWN;
    std::string propertyName;
    std::string relationName;
    std::string ruleName;

    template <typename T>
    std::shared_ptr<T> sourceAs() const {
        return std::dynamic_pointer_cast<T>(source);
    }

    template <typename T>
    std::shared_ptr<T> targetAs() const {
        return std::dynamic_pointer_cast<T>(target);
    }
};

using DBRelationUpdateFunction = std::function<void(DBRelationUpdateContext&)>;

struct DBRelationAttachContext {
    DBInstanceID sourceId;
    DBInstanceID targetId;
    std::shared_ptr<AutoRegisterDB> source;
    std::shared_ptr<AutoRegisterDB> target;
    std::string relationName;
    std::string ruleName;

    template <typename T>
    std::shared_ptr<T> sourceAs() const {
        return std::dynamic_pointer_cast<T>(source);
    }

    template <typename T>
    std::shared_ptr<T> targetAs() const {
        return std::dynamic_pointer_cast<T>(target);
    }
};

using DBRelationAttachFunction = std::function<void(DBRelationAttachContext&)>;

struct DBRelationDetachContext {
    DBInstanceID sourceId;
    DBInstanceID targetId;
    std::shared_ptr<AutoRegisterDB> source;
    std::shared_ptr<AutoRegisterDB> target;
    std::string relationName;
    std::string ruleName;
    DBRelationDetachReason reason = DBRelationDetachReason::ExplicitDetach;

    template <typename T>
    std::shared_ptr<T> sourceAs() const {
        return std::dynamic_pointer_cast<T>(source);
    }

    template <typename T>
    std::shared_ptr<T> targetAs() const {
        return std::dynamic_pointer_cast<T>(target);
    }
};

using DBRelationDetachFunction = std::function<void(DBRelationDetachContext&)>;

struct DBOwnershipRule {
    uint64_t id = 0;
    TypeID parentType = TypeID::UNKNOWN;
    TypeID childType = TypeID::UNKNOWN;
    std::string relationName;
    bool many = true;
    DBRelationDeletePolicy deletePolicy = DBRelationDeletePolicy::DetachChild;
};

struct DBRelationReferenceRule {
    TypeID childType = TypeID::UNKNOWN;
    std::string fieldName;
    TypeID parentType = TypeID::UNKNOWN;
    std::string relationName;
    bool ownsChild = true;
    bool attachesDependency = true;
};

struct DBDependencyRule {
    uint64_t id = 0;
    TypeID sourceType = TypeID::UNKNOWN;
    TypeID targetType = TypeID::UNKNOWN;
    std::string relationName;
    std::string ruleName;
    std::set<std::string> watchedProperties;
    DBRelationUpdateFunction update;
    DBRelationAttachFunction attach;
    DBRelationDetachFunction detach;
};
