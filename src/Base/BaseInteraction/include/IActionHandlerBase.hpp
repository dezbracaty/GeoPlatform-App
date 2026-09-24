#pragma once

#include <QObject>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QVariantMap>
#include <QVariantList>
#include <optional>
#include <memory>
#include "ActionContext.hpp"
#include "InteractionInputEvent.hpp"

// Handler类型枚举
enum class HandlerType {
    Background, // 后台Handler，始终活跃
    Middleware, // 中间层Handler
    Exclusive   // 独占Handler，激活时其他Handler暂停
};

// Handler基础接口
class IActionHandlerBase : public QObject {
    Q_OBJECT

public:
    using SPtr = std::shared_ptr<IActionHandlerBase>;
    using WPtr = std::weak_ptr<IActionHandlerBase>;

    explicit IActionHandlerBase(QObject* parent = nullptr) : QObject(parent) {
    }
    virtual ~IActionHandlerBase() = default;

    // Handler类型
    virtual HandlerType getHandlerType() const = 0;

    // 生命周期方法
    virtual void onEnter(std::shared_ptr<ActionContext> context) = 0;
    virtual void onEnterForAI(std::shared_ptr<ActionContext> context) {
        // Default AI entry reuses the existing onEnter flow.
        onEnter(context);
    }
    virtual void onExit() = 0;

    // ============================================================
    // AI 相关接口
    // ============================================================

    /**
     * @brief 获取 AI 动作描述符
     * @param actionCode 动作代码
     * @return AI 描述符，包含 title、description、sideEffectLevel、inputSchema 等。
     *         返回 std::nullopt 表示此 Handler 不支持 AI 调用此动作。
     */
    virtual std::optional<QVariantMap> getAIDescriptor(
        const QString& actionCode) const {
        Q_UNUSED(actionCode);
        return std::nullopt;
    }

    // 兼容旧接口：通过 getAIDescriptor() 实现
    QVariantMap getAIActionDescriptorForAction(const QString& actionCode) const {
        auto desc = getAIDescriptor(actionCode);
        return desc ? *desc : QVariantMap{};
    }

    // 是否需要持久存在（不被ActionManager自动清理）
    // 对于需要持续交互的Exclusive Handler，返回true
    virtual bool isPersistent() const { return false; }

    // Whether this handler is allowed to run in the current application
    // environment. Handlers that are valid everywhere need no override.
    virtual bool supportsEnvironment(const QString& environment) const {
        Q_UNUSED(environment);
        return true;
    }

    // 后台执行支持（可选实现）
    virtual void onSuspend() {
    } // 切换到后台时调用
    virtual void onResume() {
    } // 从后台恢复时调用

    // 事件处理方法（返回true表示事件已处理）
    //
    // 坐标契约：
    // - QMouseEvent::pos() 是 viewport-local Qt 逻辑坐标。
    // - Handler 不关心 physical/DPR。
    // - screen/world/ray 转换必须走 ViewportCoordinateSystem。
    virtual bool onMousePressEvent(QMouseEvent* event) {
        Q_UNUSED(event);
        return false;
    }
    virtual bool onMousePressEvent(const MouseInputEvent& input) {
        m_inputViewId = input.viewId;
        return onMousePressEvent(input.event);
    }
    virtual bool onMouseMoveEvent(QMouseEvent* event) {
        Q_UNUSED(event);
        return false;
    }
    virtual bool onMouseMoveEvent(const MouseInputEvent& input) {
        m_inputViewId = input.viewId;
        return onMouseMoveEvent(input.event);
    }
    virtual bool onMouseReleaseEvent(QMouseEvent* event) {
        Q_UNUSED(event);
        return false;
    }
    virtual bool onMouseReleaseEvent(const MouseInputEvent& input) {
        m_inputViewId = input.viewId;
        return onMouseReleaseEvent(input.event);
    }
    virtual bool onWheelEvent(QWheelEvent* event) {
        Q_UNUSED(event);
        return false;
    }
    virtual bool onWheelEvent(const WheelInputEvent& input) {
        m_inputViewId = input.viewId;
        return onWheelEvent(input.event);
    }
    virtual bool onKeyPressEvent(QKeyEvent* event) {
        Q_UNUSED(event);
        return false;
    }
    virtual bool onKeyPressEvent(const KeyInputEvent& input) {
        m_inputViewId = input.viewId;
        return onKeyPressEvent(input.event);
    }
    virtual bool onKeyReleaseEvent(QKeyEvent* event) {
        Q_UNUSED(event);
        return false;
    }
    virtual bool onKeyReleaseEvent(const KeyInputEvent& input) {
        m_inputViewId = input.viewId;
        return onKeyReleaseEvent(input.event);
    }

    // 是否处于活跃状态
    bool isActive() const {
        return m_isActive;
    }

signals:
    void handlerActivated();
    void handlerDeactivated();
    void requestHandlerSwitch(const QString& handlerName);

    // 后台执行相关信号
    void requestBackgroundExecution(); // Handler请求进入后台
    void requestResume();              // Handler请求恢复到前台
    void requestExit();                // Handler请求退出自身（如 ESC）

    // 异步 Action 的真实完成回执。仅当 ActionContext 带 invocationId 时发出。
    void invocationCompleted(const QString& invocationId, const QVariantMap& outcome);

protected:
    std::uint64_t inputViewId() const noexcept {
        return m_inputViewId;
    }

    void completeInvocation(const std::shared_ptr<ActionContext>& context,
                            const QVariant& result = QVariant()) {
        if (!context) {
            return;
        }

        const QString invocationId = context->getInvocationId().trimmed();
        if (invocationId.isEmpty()) {
            return;
        }

        if (result.isValid()) {
            context->setResult(result);
        }

        const bool success = !context->hasError();
        const ActionErrorMeta errorMeta = actionErrorMeta(context->getErrorCode());
        QVariantMap outcome;
        outcome.insert("invocationId", invocationId);
        outcome.insert("actionCode", context->getActionCode());
        outcome.insert("success", success);
        outcome.insert("status", success ? QStringLiteral("completed")
                                          : QStringLiteral("failed"));
        outcome.insert("error", context->getError());
        outcome.insert("error_code", QString::fromLatin1(errorMeta.code));
        outcome.insert("error_category", QString::fromLatin1(errorMeta.category));
        outcome.insert("result", context->getResult());
        emit invocationCompleted(invocationId, outcome);
    }

    void setActive(bool active) {
        if (m_isActive != active) {
            m_isActive = active;
            if (active) {
                emit handlerActivated();
            } else {
                emit handlerDeactivated();
            }
        }
    }

private:
    bool m_isActive = false;
    std::uint64_t m_inputViewId{0};
};
