#pragma once

#include "IActionHandlerBase.hpp"
#include "ActionContext.hpp"
#include "ScopedTransactionContextMetadata.hpp"
#include "Foundation/Log.h"
#include "TaskStateNotifier.hpp"
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

// 标准后台Handler基类，处理可后台执行的Handler通用逻辑
// 自动集成 TaskStateNotifier，向 QML 报告后台任务状态
class StandardBackgroundHandler : public IActionHandlerBase {
    Q_OBJECT
    Q_DISABLE_COPY(StandardBackgroundHandler)

public:
    explicit StandardBackgroundHandler(QObject* parent = nullptr)
        : IActionHandlerBase(parent), m_state(State::Inactive) {
    }

    virtual ~StandardBackgroundHandler() = default;

    // 默认为独占类型
    HandlerType getHandlerType() const override {
        return HandlerType::Exclusive;
    }

    // 标准生命周期（final，子类不能重写）
    void onEnter(std::shared_ptr<ActionContext> context) final override {
        m_context = context;
        m_state = State::Active;
        setActive(true);

        LOG_INFO("Background Handler activated: {}", metaObject()->className());

        // 调用子类的初始化
        if (onInitialize()) {
            // 子类初始化成功，请求后台执行
            m_state = State::RequestingSuspend;
            emit requestBackgroundExecution();
        } else {
            // 初始化失败，设置错误并退出
            if (m_context && !m_context->hasError()) {
                m_context->setError(ActionErrorCode::Internal, "Handler initialization failed");
            }
            setActive(false);
        }
    }

    void onExit() final override {
        m_state = State::Inactive;

        // 确保任务被注销（防止意外退出时任务残留）
        if (!m_taskId.isEmpty()) {
            TaskStateNotifier::instance()->endTask(m_taskId);
            LOG_INFO("TaskStateNotifier: Task '{}' ended (onExit)", m_taskId.toStdString());
            m_taskId.clear();
        }

        onCleanup(); // 子类清理
        setActive(false);
        m_context.reset();

        LOG_INFO("Background Handler deactivated: {}", metaObject()->className());
    }

    void onSuspend() final override {
        if (m_state == State::RequestingSuspend) {
            m_state = State::Background;
            setActive(false);

            // 向 TaskStateNotifier 注册任务
            QString description = getTaskDescription();
            m_taskId = TaskStateNotifier::instance()->beginTask(
                metaObject()->className(), description);
            LOG_INFO("TaskStateNotifier: Task '{}' started", m_taskId.toStdString());

            onBackgroundStarted(); // 通知子类已进入后台

            LOG_DEBUG("Handler suspended to background: {}", metaObject()->className());
        }
    }

    void onResume() final override {
        if (m_state == State::Background) {
            m_state = State::Active;
            setActive(true);

            const bool trackedInvocation = m_context &&
                !m_context->getInvocationId().trimmed().isEmpty();
            const ScopedTransactionContextMetadata transactionScope(
                getActionCode(),
                m_context ? m_context->getInvocationMetadata() : QVariantMap{},
                trackedInvocation);
            onBackgroundCompleted(); // 子类处理后台结果

            // 从 TaskStateNotifier 注销任务
            if (!m_taskId.isEmpty()) {
                TaskStateNotifier::instance()->endTask(m_taskId);
                LOG_INFO("TaskStateNotifier: Task '{}' ended", m_taskId.toStdString());
                m_taskId.clear();
            }

            LOG_DEBUG("Handler resumed from background: {}", metaObject()->className());

            // 处理完成后自动退出（除非子类要求继续）
            if (!shouldKeepActive()) {
                // 延迟退出，让ActionManager在下一个事件循环中处理
                QTimer::singleShot(0, this, [this]() {
                    if (m_context && !m_context->hasError()) {
                        m_context->setResult(getCompletionResult());
                    }
                    completeInvocation(m_context);
                    // 设置为非活跃状态，让ActionManager知道Handler已完成
                    setActive(false);
                    LOG_INFO("Handler marked as completed: {}", metaObject()->className());
                });
            }
        }
    }

protected:
    // 子类需要实现的接口
    virtual bool onInitialize() = 0; // 初始化，返回false会直接退出
    virtual void onBackgroundStarted() {
    }                                         // 后台执行开始（可选实现）
    virtual void onBackgroundCompleted() = 0; // 后台执行完成，处理结果
    virtual void onCleanup() {
    } // 清理资源（可选实现）
    virtual bool shouldKeepActive() const {
        return false;
    } // 是否保持激活
    virtual QVariant getCompletionResult() const {
        return QVariant();
    }

    /**
     * @brief 获取任务描述（用于在状态栏显示）
     * 子类可以覆盖此方法提供更友好的描述
     */
    virtual QString getTaskDescription() const {
        return QString("正在处理...");
    }

    // 子类用于请求恢复的接口
    void requestResumeFromBackground() {
        if (m_state == State::Background) {
            emit requestResume();
        }
    }

    /**
     * @brief 更新任务进度（后台任务期间可调用）
     * @param progress 进度值 (0.0 - 1.0)
     */
    void updateTaskProgress(float progress) {
        if (!m_taskId.isEmpty()) {
            TaskStateNotifier::instance()->updateProgress(m_taskId, progress);
        }
    }

    /**
     * @brief 更新任务描述（后台任务期间可调用）
     * @param description 新描述
     */
    void updateTaskDescription(const QString& description) {
        if (!m_taskId.isEmpty()) {
            TaskStateNotifier::instance()->updateDescription(m_taskId, description);
        }
    }

    // 辅助方法
    QString getActionCode() const {
        return m_context ? m_context->getActionCode() : QString();
    }

    QVariantMap getParams() const {
        return m_context ? m_context->getParams() : QVariantMap();
    }

    QVariant getParam(const QString& key, const QVariant& defaultValue = QVariant()) const {
        if (m_context) {
            auto params = m_context->getParams();
            return params.value(key, defaultValue);
        }
        return defaultValue;
    }

    // 状态枚举
    enum class State {
        Inactive,          // 未激活
        Active,            // 激活状态
        RequestingSuspend, // 请求挂起
        Background         // 后台运行
    };

    State getState() const {
        return m_state;
    }

    // 保护成员供子类访问
    std::shared_ptr<ActionContext> m_context;

private:
    State m_state;
    QString m_taskId;  // TaskStateNotifier 任务 ID
};
