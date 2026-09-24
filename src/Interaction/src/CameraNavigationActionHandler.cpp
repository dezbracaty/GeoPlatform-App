#include "CameraNavigationActionHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    CameraNavigationActionHandler,
    "camera.reset",
    "camera.fitScene",
    "camera.resetDefault",
    "camera.view.front",
    "camera.view.back",
    "camera.view.right",
    "camera.view.left",
    "camera.view.bottom",
    "camera.view.top",
    "camera.view.isometric",
    "camera.projection.toggle")

#include "ActionContext.hpp"
#include "ActionHandlerRegistry.hpp"
#include "AIDescriptorHelper.hpp"
#include <CameraDB.hpp>
#include <CameraNavigationController.hpp>
#include "Foundation/Log.h"

CameraNavigationActionHandler::CameraNavigationActionHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

void CameraNavigationActionHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);
    performAction(context);
    onExit();
}

void CameraNavigationActionHandler::onEnterForAI(
    std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);
    const bool ok = performAction(context);
    if (ok && context && !context->hasError()) {
        context->setResult(QVariantMap{
            {"cameraAction", context->getActionCode()},
            {"success", true}
        });
    }
    onExit();
}

const QHash<QString, QVariantMap>&
CameraNavigationActionHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {"camera.reset", makeDescriptor(
            "camera.reset",
            "Reset Camera",
            "Smart-fit camera to current scene content.",
            "write",
            {},
            {"camera", "view"}
        )},
        {"camera.fitScene", makeDescriptor(
            "camera.fitScene",
            "Fit Camera to Scene",
            "Smart-fit camera to current scene content.",
            "write",
            {},
            {"camera", "view"}
        )}
    };
    return table;
}

bool CameraNavigationActionHandler::performAction(
    std::shared_ptr<ActionContext> context) {
    if (!context) {
        LOG_ERROR("Camera navigation action is missing its context");
        return false;
    }

    auto camera = CameraNavigationController::currentCamera();
    if (!camera) {
        context->setError(ActionErrorCode::TargetNotFound,
                          "No camera found in scene");
        return false;
    }

    const QString actionCode = context->getActionCode();
    bool success = false;
    if (actionCode == QStringLiteral("camera.reset")
        || actionCode == QStringLiteral("camera.fitScene")) {
        success = CameraNavigationController::fitScene(camera);
        if (success) {
            LOG_INFO("Camera fitted to scene");
        }
    } else if (actionCode == QStringLiteral("camera.resetDefault")) {
        success = CameraNavigationController::resetDefault(camera);
        if (success) {
            LOG_INFO("Camera reset to default view");
        }
    } else if (actionCode == QStringLiteral("camera.projection.toggle")) {
        success = CameraNavigationController::toggleProjection(camera);
        if (success) {
            LOG_INFO("Camera projection switched to {}",
                     camera->isPerspective() ? "perspective" : "orthographic");
        }
    } else if (const auto preset =
                   CameraNavigationController::presetFromAction(actionCode)) {
        success = CameraNavigationController::setPreset(*preset, camera);
    } else {
        context->setError(ActionErrorCode::InvalidParams,
                          QString("Unsupported camera action: %1").arg(actionCode));
        LOG_WARN("Unsupported camera action: {}", actionCode.toStdString());
        return false;
    }

    if (!success && !context->hasError()) {
        context->setError(ActionErrorCode::Internal,
                          QString("Camera action failed: %1").arg(actionCode));
    }
    return success;
}
