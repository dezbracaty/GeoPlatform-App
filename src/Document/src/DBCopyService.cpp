#include "DBCopyService.hpp"

#include "DocumentManager.hpp"
#include "Foundation/Log.h"
#include <ImmediateNotifyGuard.hpp>
#include <TransactionManager.hpp>
#include <algorithm>
#include <functional>
#include <unordered_set>

namespace {

struct SourceNode {
    DBInstanceID id;
    std::shared_ptr<AutoRegisterDB> object;
    std::vector<DBRelationRef> relations;
};

bool containsReportedRelation(const std::vector<DBRelationRef>& relations,
                              const std::string& relationName,
                              const DBInstanceID& targetId) {
    return std::any_of(
        relations.begin(), relations.end(),
        [&](const DBRelationRef& relation) {
            return relation.relationName == relationName &&
                   relation.targetId == targetId;
        });
}

bool containsOwnershipEdge(
    const std::vector<DocumentManager::OwnershipEdge>& edges,
    const DBInstanceID& parentId,
    const DBInstanceID& childId,
    const std::string& relationName) {
    return std::any_of(
        edges.begin(), edges.end(),
        [&](const DocumentManager::OwnershipEdge& edge) {
            return edge.parentId == parentId && edge.childId == childId &&
                   edge.relationName == relationName;
        });
}

} // namespace

DBCopyService::DBCopyService(DocumentManager& document)
    : m_document(document) {
}

DBCopyResult DBCopyService::copy(
    const std::vector<DBInstanceID>& sourceRootIds) const {
    DBCopyResult result;
    if (!TransactionManager::instance().isInTransaction()) {
        result.error = "DBCopyService requires an active transaction";
        return result;
    }
    if (sourceRootIds.empty()) {
        result.error = "copy root list is empty";
        return result;
    }

    const auto sourceOwnershipEdges = m_document.getOwnershipEdges();
    std::unordered_map<DBInstanceID, std::vector<DocumentManager::OwnershipEdge>>
        childrenByParent;
    for (const auto& edge : sourceOwnershipEdges) {
        childrenByParent[edge.parentId].push_back(edge);
    }

    std::unordered_set<DBInstanceID> selectedRoots;
    for (const auto& id : sourceRootIds) {
        if (id.isValid()) selectedRoots.insert(id);
    }

    std::vector<DBInstanceID> normalizedRoots;
    for (const auto& id : sourceRootIds) {
        if (!id.isValid() || !m_document.getDBInstance(id)) continue;
        bool coveredBySelectedAncestor = false;
        DBInstanceID owner = m_document.getOwner(id);
        while (owner.isValid()) {
            if (selectedRoots.count(owner) != 0) {
                coveredBySelectedAncestor = true;
                break;
            }
            owner = m_document.getOwner(owner);
        }
        if (!coveredBySelectedAncestor &&
            std::find(normalizedRoots.begin(), normalizedRoots.end(), id) ==
                normalizedRoots.end()) {
            normalizedRoots.push_back(id);
        }
    }
    if (normalizedRoots.empty()) {
        result.error = "no valid copy roots";
        return result;
    }

    std::vector<SourceNode> sourceNodes;
    std::unordered_set<DBInstanceID> visited;
    std::function<bool(const DBInstanceID&)> collect =
        [&](const DBInstanceID& id) {
            if (visited.count(id) != 0) return true;
            auto object = m_document.getDBInstance(id);
            if (!object) {
                result.error = "copy graph contains a missing DB instance";
                return false;
            }
            visited.insert(id);
            sourceNodes.push_back(SourceNode{id, object, object->reportRelations()});
            const auto childIt = childrenByParent.find(id);
            if (childIt == childrenByParent.end()) return true;
            for (const auto& edge : childIt->second) {
                if (!collect(edge.childId)) return false;
            }
            return true;
        };

    for (const auto& rootId : normalizedRoots) {
        if (!collect(rootId)) return result;
    }

    std::unordered_map<DBInstanceID, std::shared_ptr<AutoRegisterDB>> copies;
    for (const auto& node : sourceNodes) {
        auto copy = node.object->clone();
        if (!copy || !copy->getDBInstanceID().isValid() ||
            copy->getDBInstanceID() == node.id ||
            copy->getTypeID() != node.object->getTypeID()) {
            result.error = "DB node clone failed or returned an incompatible type";
            return result;
        }
        result.idMap[node.id] = copy->getDBInstanceID();
        copies[node.id] = std::move(copy);
    }

    // Some DB types create a required owned child while their clone is being
    // initialized (ActorDB creates its default MaterialDB). The source graph
    // already contains that child as an independent node and it is cloned
    // above, so keeping the constructor-created placeholder would either
    // duplicate a one-to-one relation or prevent the real copied child from
    // being attached. Remove only placeholders for ownership edges that are
    // explicitly present in the copied source subgraph.
    for (const auto& edge : sourceOwnershipEdges) {
        const auto copiedParent = result.idMap.find(edge.parentId);
        const auto copiedChild = result.idMap.find(edge.childId);
        if (copiedParent == result.idMap.end() ||
            copiedChild == result.idMap.end()) {
            continue;
        }
        const auto initializedChildren = m_document.getOwnedChildren(
            copiedParent->second, edge.relationName);
        for (const auto& initializedChild : initializedChildren) {
            if (initializedChild == copiedChild->second) continue;
            if (!m_document.unregisterDBInstance(initializedChild)) {
                result.error =
                    "failed to remove a clone-initialized ownership placeholder";
                return result;
            }
        }
    }

    // Ownership is copied from the document graph itself. DB payloads do not
    // mirror these edges in ParentDBId/ChildDBId properties.
    for (const auto& edge : sourceOwnershipEdges) {
        const auto copiedChild = result.idMap.find(edge.childId);
        if (copiedChild == result.idMap.end()) continue;
        const auto copiedParent = result.idMap.find(edge.parentId);
        if (copiedParent == result.idMap.end()) continue;
        const DBInstanceID targetParent = copiedParent->second;
        if (!m_document.attachOwnedChild(
                targetParent, copiedChild->second, edge.relationName)) {
            result.error = "failed to rebuild copied document relation";
            return result;
        }
    }

    for (const auto& node : sourceNodes) {
        std::vector<DBRelationReplacement> replacements;
        replacements.reserve(node.relations.size());
        for (const auto& relation : node.relations) {
            const auto mappedTarget = result.idMap.find(relation.targetId);
            replacements.push_back(DBRelationReplacement{
                relation.relationName,
                relation.targetId,
                mappedTarget == result.idMap.end()
                    ? relation.targetId
                    : mappedTarget->second});
        }

        auto copyIt = copies.find(node.id);
        if (copyIt == copies.end()) {
            result.error = "copy disappeared before relation replacement";
            return result;
        }

        // Remaining reportRelations entries are non-ownership reference
        // fields and still require ID translation.
        {
            ImmediateNotifyGuard immediateNotify;
            if (!copyIt->second->replaceRelations(replacements)) {
                result.error = "DB node failed to replace copied relations";
                return result;
            }
        }

        const auto copiedRelations = copyIt->second->reportRelations();
        for (const auto& replacement : replacements) {
            if (!containsReportedRelation(
                    copiedRelations,
                    replacement.relationName,
                    replacement.newTargetId)) {
                result.error = "copied DB did not report a replaced relation";
                return result;
            }
        }
    }

    const auto copiedOwnershipEdges = m_document.getOwnershipEdges();
    for (const auto& edge : sourceOwnershipEdges) {
        const auto childIt = result.idMap.find(edge.childId);
        if (childIt == result.idMap.end()) continue;

        const auto parentIt = result.idMap.find(edge.parentId);
        if (parentIt == result.idMap.end()) continue;
        const DBInstanceID expectedParentId = parentIt->second;
        if (!containsOwnershipEdge(
                copiedOwnershipEdges,
                expectedParentId,
                childIt->second,
                edge.relationName)) {
            result.error = "copied ownership relation was not rebuilt";
            return result;
        }
    }

    for (const auto& rootId : normalizedRoots) {
        result.rootIds.push_back(result.idMap.at(rootId));
    }
    result.success = true;
    return result;
}
