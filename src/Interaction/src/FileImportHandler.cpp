#include "FileImportHandler.hpp"
#include "ActionManager.hpp"
#include "EnvironmentSwitchHandler.hpp"
#include "InteractionRegistration.hpp"

#include <QFileInfo>
#include <QUrl>

REGISTER_INTERACTION_ACTION(FileImportHandler, "file.import")

void FileImportHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);
    // This action completes dispatch synchronously, not the background import.
    setActive(false);

    auto params = context->getParams();
    const auto input = params.value("filePath");
    if (input.toString().isEmpty()) {
        context->setError(ActionErrorCode::InvalidParams, "filePath is required");
        return;
    }

    const QUrl url = input.toUrl();
    if (!url.scheme().isEmpty() && !url.isLocalFile() &&
        !QFileInfo(input.toString()).isAbsolute()) {
        context->setError(ActionErrorCode::InvalidParams, "Only local files can be imported");
        return;
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : input.toString();
    const QFileInfo file(path);
    if (!file.exists()) {
        context->setError(ActionErrorCode::TargetNotFound,
                          QString("File not found: %1").arg(path));
        return;
    }
    if (!file.isFile() || !file.isReadable()) {
        context->setError(ActionErrorCode::InvalidParams,
                          QString("Not a readable file: %1").arg(path));
        return;
    }

    params.insert("filePath", file.absoluteFilePath());
    const QString suffix = file.suffix().toLower();
    const bool isGCode = suffix == "gcode" || suffix == "gco" || suffix == "g";
    const QString importer = isGCode ? "load_gcode_file" : "import_model";
    if (isGCode && !params.contains("confirm")) {
        params.insert("confirm", true);
    }

    auto* manager = ActionManager::getInstance();
    const bool accepted = manager->triggerAction(importer, params);
    const QVariantMap importOutcome = manager->lastActionOutcome();
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
