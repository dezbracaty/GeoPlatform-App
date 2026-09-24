#pragma once

#include "AutoRegisterDB.hpp"
#include "BaseID.hpp"
#include "ChangeTypes.hpp"
#include "SystemTypes.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * Stable, read-only view of the application Document for renderer backends.
 *
 * Backends may understand concrete AppDB types. This port only prevents them
 * from coupling object discovery, relationship lookup and change subscription
 * to DocumentManager's mutable singleton API.
 */
struct DocumentChange {
    DBInstanceID id;
    TypeID dbType{TypeID::UNKNOWN};
    ChangeType changeType{ChangeType::UNKNOWN};
    std::string propertyName;
    DBInstanceID ownerId;
};

class IDocumentReadView {
public:
    using SubscriptionId = std::size_t;
    using ChangeCallback = std::function<void(const DocumentChange&)>;

    virtual ~IDocumentReadView() = default;

    [[nodiscard]] virtual std::shared_ptr<const AutoRegisterDB> getDB(
        const DBInstanceID& id) const = 0;

    [[nodiscard]] virtual std::vector<std::shared_ptr<const AutoRegisterDB>> getDBs(
        TypeID type = TypeID::UNKNOWN) const = 0;

    [[nodiscard]] virtual TypeID getDBType(const DBInstanceID& id) const = 0;

    [[nodiscard]] virtual DBInstanceID getOwner(
        const DBInstanceID& childId) const = 0;

    [[nodiscard]] virtual std::vector<DBInstanceID> getOwnedChildren(
        const DBInstanceID& ownerId,
        const std::string& relationName = {}) const = 0;

    virtual SubscriptionId subscribe(ChangeCallback callback) = 0;
    virtual void unsubscribe(SubscriptionId id) = 0;

    template <typename T>
    [[nodiscard]] std::shared_ptr<const T> get(const DBInstanceID& id) const {
        return std::dynamic_pointer_cast<const T>(getDB(id));
    }
};
