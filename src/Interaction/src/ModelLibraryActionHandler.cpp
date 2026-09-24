#include "ModelLibraryActionHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    ModelLibraryActionHandler,
    "model.library.local.search",
    "model.library.local.refresh",
    "model.library.local.delete",
    "model.library.local.reveal")

#include "AIDescriptorHelper.hpp"
#include "ActionHandlerRegistry.hpp"
#include "Foundation/Log.h"
#include "LocalModelLibraryService.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

ModelLibraryActionHandler::ModelLibraryActionHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

const QHash<QString, QVariantMap>& ModelLibraryActionHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = [] {
        QVariantMap descriptor = makeDescriptor(
            "model.library.local.search",
            "Search Local Model Library",
            "Search models already present in the user's local model library. Results use opaque libraryItemId values and never expose local file paths.",
            "read",
            makeInputSchema({
                {"query", "string", true},
                {"limit", "number", false, 5}
            }),
            {"model-library", "local", "search"});
        descriptor.insert("idempotent", true);
        return QHash<QString, QVariantMap>{{"model.library.local.search", descriptor}};
    }();
    return table;
}

void ModelLibraryActionHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(std::move(context));
    if (!m_context) {
        setActive(false);
        return;
    }

    auto* service = LocalModelLibraryService::instance();
    const QString actionCode = getActionCode();
    QVariantMap result;
    QString error;

    if (actionCode == QStringLiteral("model.library.local.search")) {
        result = service->search(getParam(QStringLiteral("query")).toString(),
                                 getParam(QStringLiteral("limit"), 5).toInt());
        if (!result.value(QStringLiteral("success")).toBool()) {
            error = result.value(QStringLiteral("error")).toString();
        }
    } else if (actionCode == QStringLiteral("model.library.local.refresh")) {
        const bool success = service->refresh(error);
        result.insert(QStringLiteral("success"), success);
        result.insert(QStringLiteral("count"), service->records().size());
    } else if (actionCode == QStringLiteral("model.library.local.delete")) {
        const QString libraryItemId =
            getParam(QStringLiteral("libraryItemId")).toString();
        const bool success = service->deleteModel(libraryItemId, error);
        result.insert(QStringLiteral("success"), success);
        result.insert(QStringLiteral("libraryItemId"), libraryItemId);
    } else if (actionCode == QStringLiteral("model.library.local.reveal")) {
        LocalModelRecord record;
        const QString libraryItemId =
            getParam(QStringLiteral("libraryItemId")).toString();
        bool success = service->resolve(libraryItemId, record, error);
        if (success) {
            const QUrl folderUrl = QUrl::fromLocalFile(QFileInfo(record.filePath).absolutePath());
            success = QDesktopServices::openUrl(folderUrl);
            if (!success) {
                error = QStringLiteral("Failed to open the model folder");
            }
        }
        result.insert(QStringLiteral("success"), success);
        result.insert(QStringLiteral("libraryItemId"), libraryItemId);
    } else {
        error = QStringLiteral("Unsupported model library action");
    }

    if (!error.isEmpty()) {
        m_context->setError(ActionErrorCode::InvalidParams, error);
        LOG_ERROR("Model library action '{}' failed: {}",
                  actionCode.toStdString(), error.toStdString());
    } else {
        m_context->setResult(result);
        LOG_INFO("Model library action '{}' completed", actionCode.toStdString());
    }
    setActive(false);
}

void ModelLibraryActionHandler::onExit() {
    StandardActionHandler::onExit();
}
