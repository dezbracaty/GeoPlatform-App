#include "AIPanelHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(AIPanelHandler, "ui.ai.open", "ui.ai.close", "ui.ai.toggle")
#include "ActionContext.hpp"
#include "ActionHandlerRegistry.hpp"
#include <AIChatBridge.hpp>

AIPanelHandler::AIPanelHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

void AIPanelHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    const QString actionCode = getActionCode();
    auto* bridge = AIChatBridge::instance();
    if (!bridge) {
        if (context) {
            context->setError(ActionErrorCode::SystemUnavailable, "AIChatBridge is not available");
        }
        return;
    }

    if (actionCode == "ui.ai.open") {
        bridge->openPanel();
    } else if (actionCode == "ui.ai.close") {
        bridge->closePanel();
    } else if (actionCode == "ui.ai.toggle") {
        bridge->togglePanel();
    } else {
        if (context) {
            context->setError(ActionErrorCode::InvalidParams, QString("Unsupported AI panel action: %1").arg(actionCode));
        }
    }
}
