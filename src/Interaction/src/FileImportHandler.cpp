#include "FileImportHandler.hpp"
#include "ImportFileFormat.hpp"
#include "ActionManager.hpp"
#include "EnvironmentSwitchHandler.hpp"
#include "InteractionRegistration.hpp"
#include "AIDescriptorHelper.hpp"
#include "LocalModelLibraryService.h"

#include <QFileInfo>
#include <QUrl>

REGISTER_INTERACTION_ACTION(FileImportHandler, "file.import", "import_model", "model.library.import")

const QHash<QString, QVariantMap>& FileImportHandler::aiDescriptorTable() const {
    static const auto table = [] {
        QHash<QString, QVariantMap> result;
        const auto schema = makeInputSchema({{"filePath", "string", false}, {"libraryItemId", "string", false},
            {"intent", "string", false}, {"printBedId", "number", false},
            {"confirmReplace", "boolean", false},
            {"requestId", "string", false},
            {"acceptLegacyInfillRotationFix", "boolean", false}, {"unit", "string", false},
            {"scale", "number", false}, {"name", "string", false}, {"confirm", "boolean", false},
            {"x", "number", false}, {"y", "number", false}, {"z", "number", false},
            {"position", "object", false}, {"color", "string", false}});
        for (const auto* action : {"file.import", "import_model", "model.library.import"}) {
            auto descriptor = makeDescriptor(action, "Import File", "Import STL, 3MF models/projects or G-code through the application import workflow.",
                "write", schema, {"import", "file"});
            // Routing completes synchronously; importOutcome identifies the concrete task.
            descriptor.insert("async", false);
            result.insert(action, descriptor);
        }
        return result;
    }();
    return table;
}

void FileImportHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);
    // This action completes dispatch synchronously, not the background import.
    setActive(false);

    auto params = context->getParams();
    if (context->getActionCode() == "model.library.import") {
        LocalModelRecord record;
        QString error;
        if (!LocalModelLibraryService::instance()->resolve(params.value("libraryItemId").toString(), record, error)) {
            context->setError(ActionErrorCode::TargetNotFound, error);
            return;
        }
        params.insert("filePath", record.filePath);
        params.insert("name", record.name);
    }
    const auto input = params.value("filePath");
    if (input.toString().isEmpty()) {
        context->setError(ActionErrorCode::InvalidParams, tr("filePath is required"));
        return;
    }

    const QUrl url = input.toUrl();
    if (!url.scheme().isEmpty() && !url.isLocalFile() &&
        !QFileInfo(input.toString()).isAbsolute()) {
        context->setError(ActionErrorCode::InvalidParams, tr("Only local files can be imported"));
        return;
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : input.toString();
    const QFileInfo file(path);
    if (!file.exists()) {
        context->setError(ActionErrorCode::TargetNotFound,
                          tr("File not found: %1").arg(path));
        return;
    }
    if (!file.isFile() || !file.isReadable()) {
        context->setError(ActionErrorCode::InvalidParams,
                          tr("Not a readable file: %1").arg(path));
        return;
    }

    params.insert("filePath", file.absoluteFilePath());
    const QString suffix = file.suffix().toLower();
    const auto format = classifyImportFile(file.absoluteFilePath());
    if (format == ImportFileFormat::Unsupported) {
        context->setError(ActionErrorCode::InvalidParams,
                          tr("Unsupported file format: %1. Supported files: STL, 3MF and G-code.")
                              .arg(suffix));
        return;
    }
    const bool isGCode = format == ImportFileFormat::GCode;
    if (isGCode && context->getActionCode() != "file.import") {
        context->setError(ActionErrorCode::InvalidParams,tr("Model import accepts STL or 3MF files."));
        return;
    }
    const QString importer = isGCode ? "load_gcode_file" :
        format == ImportFileFormat::ThreeMf ? "project.import" : "import_mesh";
    if (context->getActionCode() != "file.import" && format == ImportFileFormat::ThreeMf)
        params.insert("intent", "geometry");
    if (isGCode && !params.contains("confirm")) {
        params.insert("confirm", true);
    }

    auto* manager = ActionManager::getInstance();
    const QVariantMap importOutcome = manager->invokeActionWithReceipt(importer, params);
    const bool accepted = importOutcome.value("accepted").toBool() && importOutcome.value("success").toBool();
    // Keep the original receipt, including its pending/error information.
    context->setResult(QVariantMap{{"importAction", importer},
                                  {"importAccepted", accepted},
                                  {"importOutcome", importOutcome}});
    if (!accepted) {
        auto errorCode = ActionErrorCode::Internal;
        for (const auto code : {ActionErrorCode::TargetRequired,
                                ActionErrorCode::TargetNotFound,
                                ActionErrorCode::InvalidParams,
                                ActionErrorCode::ContractValidation,
                                ActionErrorCode::SystemUnavailable,
                                ActionErrorCode::Internal}) {
            if (actionErrorCodeToString(code) == importOutcome.value("error_code").toString()) {
                errorCode = code;
                break;
            }
        }
        context->setError(errorCode, importOutcome.value("error").toString());
        return;
    }

    if (isGCode) {
        // The importer is suspended while parsing. An exclusive environment.switch
        // action would be rejected here; reuse the existing C++ switching operation.
        EnvironmentSwitchHandler::switchEnvironment("slicing");
        if (manager->currentEnvironment() != "slicing") {
            context->setError(ActionErrorCode::Internal,
                              "GCode import accepted, but switching to preview failed");
        }
    }
}
