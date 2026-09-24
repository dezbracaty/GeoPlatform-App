#pragma once

#include <memory>
#include <string>

#include "BaseID.hpp"
#include "ChangeTypes.hpp"
#include "SystemTypes.hpp"

class AutoRegisterDB;

// BaseDB-side port implemented by the Document module.
class IDocumentRegistry {
public:
    virtual ~IDocumentRegistry() = default;

    virtual DBInstanceID registerDBInstance(std::shared_ptr<AutoRegisterDB> instance,
                                            TypeID dbType) = 0;
    virtual bool unregisterDBInstance(const DBInstanceID& id) = 0;
    virtual void notifyChange(void* object,
                              ChangeType changeType,
                              const std::string& fieldName) = 0;
    virtual void transWithChange(AutoRegisterDB* dbObject,
                                 const std::string& field,
                                 ChangeType changeType) = 0;
};

IDocumentRegistry* documentRegistry();
void setDocumentRegistry(IDocumentRegistry* registry);
