#pragma once

#include <IDocumentReadView.hpp>

class DocumentManager;

/** Thin read-only adapter over the application's DocumentManager. */
class DocumentManagerReadView final : public IDocumentReadView {
public:
    explicit DocumentManagerReadView(DocumentManager& document) noexcept;

    std::shared_ptr<const AutoRegisterDB> getDB(
        const DBInstanceID& id) const override;
    std::vector<std::shared_ptr<const AutoRegisterDB>> getDBs(
        TypeID type) const override;
    TypeID getDBType(const DBInstanceID& id) const override;
    DBInstanceID getOwner(const DBInstanceID& childId) const override;
    std::vector<DBInstanceID> getOwnedChildren(
        const DBInstanceID& ownerId,
        const std::string& relationName) const override;

    SubscriptionId subscribe(ChangeCallback callback) override;
    void unsubscribe(SubscriptionId id) override;

private:
    DocumentManager& m_document;
};
