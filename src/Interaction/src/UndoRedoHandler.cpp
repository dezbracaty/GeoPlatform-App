#include "UndoRedoHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(UndoRedoHandler, "edit.undo", "edit.redo", "edit.clear_history")
#include "ActionHandlerRegistry.hpp"
#include <DocumentManager.hpp>
#include "AIDescriptorHelper.hpp"
#include "Foundation/Log.h"
#include "transdb.h"
#include <QVariantList>

UndoRedoHandler::UndoRedoHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

void UndoRedoHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    QString actionCode = context->getActionCode();

    // 根据actionCode执行不同的操作
    if (actionCode == "edit.undo") {
        performUndo();
    } else if (actionCode == "edit.redo") {
        performRedo();
    } else if (actionCode == "edit.clear_history") {
        clearHistory();
    } else {
        LOG_WARN("Unknown undo/redo action: {}", actionCode.toStdString());
    }
}

void UndoRedoHandler::onEnterForAI(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    bool handled = true;
    const QString actionCode = getActionCode();
    if (actionCode == "edit.undo") {
        performUndo();
    } else if (actionCode == "edit.redo") {
        performRedo();
    } else {
        handled = false;
        if (context) {
            context->setError(ActionErrorCode::InvalidParams, 
                QString("AI invoke not supported for action '%1'. Allowed: edit.undo/edit.redo")
                    .arg(actionCode));
        }
        LOG_WARN("UndoRedoHandler AI invoke rejected for action '{}'",
                 actionCode.toStdString());
    }

    if (handled && context && !context->hasError()) {
        context->setResult(QVariantMap{
            {"actionCode", actionCode},
            {"applied", true}
        });
    }

    StandardActionHandler::onExit();
}

const QHash<QString, QVariantMap>& UndoRedoHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {"edit.undo", makeDescriptor(
            "edit.undo",
            "Undo",
            "Undo the last user-editable operation.",
            "write",
            {},
            {"history", "undo"}
        )},
        {"edit.redo", makeDescriptor(
            "edit.redo",
            "Redo",
            "Redo the latest undone operation.",
            "write",
            {},
            {"history", "redo"}
        )}
    };
    return table;
}

void UndoRedoHandler::performUndo() {
    auto* docManager = DocumentManager::instance();
    if (docManager) {
        if (docManager->canUndo()) {
            const std::string desc = TransactionManager::instance().getNextUndoDescription();
            LOG_INFO("UndoRedoHandler::performUndo executing: '{}'", desc);
            docManager->undo();
        } else {
            LOG_DEBUG("Nothing to undo");
        }
    }
}

void UndoRedoHandler::performRedo() {
    auto* docManager = DocumentManager::instance();
    if (docManager) {
        if (docManager->canRedo()) {
            const std::string desc = TransactionManager::instance().getNextRedoDescription();
            LOG_INFO("UndoRedoHandler::performRedo executing: '{}'", desc);
            docManager->redo();
        } else {
            LOG_DEBUG("Nothing to redo");
        }
    }
}

void UndoRedoHandler::clearHistory() {
    auto* docManager = DocumentManager::instance();
    if (docManager && docManager->getUndoRedoManager()) {
        docManager->getUndoRedoManager()->clear();
        LOG_DEBUG("Undo/Redo history cleared");
    }
}
