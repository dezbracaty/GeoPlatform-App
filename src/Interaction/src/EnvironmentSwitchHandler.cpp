#include "EnvironmentSwitchHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(EnvironmentSwitchHandler, "environment.switch")
#include "ActionContext.hpp"
#include "ActionManager.hpp"
#include "ActionHandlerRegistry.hpp"
#include "EnvironmentManager.hpp"
#include "Foundation/Log.h"

EnvironmentSwitchHandler::EnvironmentSwitchHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

void EnvironmentSwitchHandler::switchEnvironment(const QString& environment) {
    LOG_INFO("EnvironmentSwitchHandler: Switching to environment '{}'", environment.toStdString());

    // 调用 EnvironmentManager 执行环境切换
    bool success = EnvironmentManager::instance()->switchTo(environment);
    if (!success) {
        LOG_WARN("EnvironmentSwitchHandler: Failed to switch to environment '{}'", environment.toStdString());
        return;
    }

    if (auto* actionManager = ActionManager::getInstance()) {
        actionManager->setEnvironment(environment);
    }
}

void EnvironmentSwitchHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    auto params = getParams();
    QString targetEnv = params.value("environment").toString();

    if (targetEnv.isEmpty()) {
        LOG_ERROR("EnvironmentSwitchHandler: 'environment' parameter is missing");
        return;
    }

    // 调用统一的环境切换接口
    switchEnvironment(targetEnv);

    // 立即退出，不占用 active slot
    onExit();
}
