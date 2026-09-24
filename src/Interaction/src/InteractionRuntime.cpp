#include "InteractionRuntime.hpp"

#include "ActionManager.hpp"
#include "ModelColorPaintComputeCoordinator.hpp"
#include "PickService.hpp"
#include "SnapService.hpp"
#include "Foundation/Log.h"
#include <mutex>

namespace {
std::shared_ptr<PickService>& pickServiceStorage() {
    static auto service = std::make_shared<PickService>();
    return service;
}

}

void registerInteractionRuntime() {
    static std::once_flag once;
    std::call_once(once, []() {
        pickServiceStorage();
        ModelColorPaintComputeCoordinator::instance();
    });
}

std::shared_ptr<GPlatform::Rendering::IRenderViewLifecycleSink>
interactionRenderViewLifecycleSink() {
    return pickServiceStorage();
}

PickService& interactionPickService() {
    return *pickServiceStorage();
}

const GPlatform::Interaction::SnapService& interactionSnapService() {
    static const GPlatform::Interaction::SnapService service;
    return service;
}

void startDefaultInteractionHandlers() {
    static std::once_flag once;
    std::call_once(once, []() {
        auto* actionManager = ActionManager::getInstance();
        if (!actionManager) {
            LOG_ERROR("InteractionRuntime: ActionManager is unavailable");
            return;
        }
        if (!actionManager->triggerAction("camera.panRotate")) {
            LOG_ERROR(
                "InteractionRuntime: failed to activate default camera handler");
            return;
        }
        if (!actionManager->triggerAction("interaction.model")) {
            LOG_ERROR(
                "InteractionRuntime: failed to activate model interaction handler");
            return;
        }
        if (!actionManager->triggerAction("preview.inspect")) {
            LOG_ERROR("InteractionRuntime: failed to activate preview inspection");
        }
        LOG_INFO(
            "InteractionRuntime: default interaction handlers activated");
    });
}
