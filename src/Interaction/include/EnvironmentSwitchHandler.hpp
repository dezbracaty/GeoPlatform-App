#pragma once

#include "StandardActionHandler.hpp"

/**
 * @brief 环境切换动作处理器
 *
 * 处理 "environment.switch" action，统一管理环境与交互路由切换：
 * - 调用 EnvironmentManager 切换环境
 * - 通知 ActionManager 暂停或恢复环境相关的交互 Handler
 *
 * 环境表示交互路由，不表示场景外观。页面切换不得修改共享的
 * SkyboxDB、PrintBedDB、MaterialDB 或模型属性。
 *
 * 参数:
 *   - environment: 目标环境名称 (normal, editing, slicing, preview, support)
 */
class EnvironmentSwitchHandler : public StandardActionHandler {
    Q_OBJECT

public:
    explicit EnvironmentSwitchHandler(QObject* parent = nullptr);

    // 统一的环境切换接口
    static void switchEnvironment(const QString& environment);

protected:
    void onEnter(std::shared_ptr<ActionContext> context) override;
};
