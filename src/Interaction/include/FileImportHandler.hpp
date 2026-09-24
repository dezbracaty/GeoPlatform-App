#pragma once

#include "StandardActionHandler.hpp"

// Common file entry for pickers, drops and external ActionManager callers.
// Parsing and background task completion remain owned by the concrete importer.
class FileImportHandler final : public StandardActionHandler {
    Q_OBJECT

public:
    using StandardActionHandler::StandardActionHandler;

    // Dispatching must not replace the concrete importer's exclusive slot.
    HandlerType getHandlerType() const override { return HandlerType::Middleware; }
    void onEnter(std::shared_ptr<ActionContext> context) override;
};
