#include "DeleteHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(DeleteHandler, "edit.delete")
#include "ActionHandlerRegistry.hpp"
#include <DocumentManager.hpp>
#include "SelectionBridge.hpp"
#include "Foundation/Log.h"
#include <ActorDB.hpp>
#include <ModelGraphUtil.hpp>
#include <ModelInstanceDB.hpp>
#include <transdb.h>

DeleteHandler::DeleteHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

void DeleteHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    QString actionCode = context->getActionCode();

    if (actionCode == "edit.delete") {
        deleteSelectedModels();
    } else {
        LOG_WARN("Unknown delete action: {}", actionCode.toStdString());
    }
}

void DeleteHandler::deleteSelectedModels() {
    auto* selectionBridge = SelectionBridge::instance();
    if (!selectionBridge) {
        LOG_WARN("SelectionBridge not available");
        return;
    }

    if (!selectionBridge->hasSelection()) {
        LOG_DEBUG("No models selected for deletion");
        return;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_WARN("DocumentManager not available");
        return;
    }

    // 获取所有选中的对象IDs
    const auto& selectedIds = selectionBridge->getSelectedIds();

    if (selectedIds.empty()) {
        LOG_DEBUG("No valid selected objects to delete");
        return;
    }

    LOG_INFO("Deleting {} selected models", selectedIds.size());

    // 在事务中删除选中的对象
    {
        TransactionGuard guard("Delete Selected Models");

        for (const auto& id : selectedIds) {
            auto instance = docManager->getDBInstance(id);
            if (instance) {
                if (auto model =
                        std::dynamic_pointer_cast<ModelInstanceDB>(instance)) {
                    LOG_INFO("Deleting model: {} (ID: {})",
                             model->getDisplayName(), id.getValue());
                    std::string error;
                    if (!ModelGraphUtil::removeInstance(id, &error)) {
                        LOG_WARN("Unable to delete model {}: {}",
                                 id.getValue(), error);
                    }
                } else if (auto actor =
                               std::dynamic_pointer_cast<ActorDB>(instance)) {
                    LOG_INFO("Deleting non-graph actor: {} (ID: {})",
                             actor->getDisplayName(), id.getValue());
                    if (!actor->removeMaterial() ||
                        !docManager->unregisterDBInstance(id)) {
                        LOG_WARN("Unable to delete non-graph actor {}",
                                 id.getValue());
                    }
                } else {
                    LOG_DEBUG("Skipping non-model object: {} (ID: {})",
                             instance->getDisplayName(), id.getValue());
                }
            } else {
                LOG_WARN("Could not find object with ID: {}", id.getValue());
            }
        }
    }

    // 清除选择状态
    selectionBridge->clearSelection();

    LOG_INFO("Selected models deletion completed");
}
