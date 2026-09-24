#pragma once

#include "IActionHandlerBase.hpp"
#include "ActionContext.hpp"
#include "Foundation/Log.h"

// 标准Handler基类，提供默认实现
class StandardActionHandler : public IActionHandlerBase {
    Q_OBJECT

public:
    explicit StandardActionHandler(QObject* parent = nullptr)
        : IActionHandlerBase(parent) {
    }

    virtual ~StandardActionHandler() = default;

    // 默认为独占类型
    HandlerType getHandlerType() const override {
        return HandlerType::Exclusive;
    }

    // 默认生命周期实现
    void onEnter(std::shared_ptr<ActionContext> context) override {
        m_context = context;
        setActive(true);
        LOG_DEBUG("Handler activated: {}", metaObject()->className());
    }

    void onExit() override {
        setActive(false);
        m_context.reset();
        LOG_DEBUG("Handler deactivated: {}", metaObject()->className());
    }

    bool onKeyPressEvent(QKeyEvent* event) override {
        // ESC cancels the active interactive tool. Middleware/background
        // handlers must not consume it through this default implementation.
        if (event && event->key() == Qt::Key_Escape && isActive() &&
            getHandlerType() == HandlerType::Exclusive) {
            emit requestExit();
            return true;
        }
        return false;
    }

    // ============================================================
    // AI Schema 辅助方法 - 请使用 AIDescriptorHelper.hpp 中的 makeDescriptor/makeInputSchema
    // ============================================================

protected:
    /**
     * @brief AI 描述符静态表
     * @return QHash<actionCode, descriptor>
     *
     * 子类需要重写此方法，返回自己支持的 AI action descriptors 表。
     * 基类默认实现返回空表（不支持任何 AI action）。
     */
    virtual const QHash<QString, QVariantMap>& aiDescriptorTable() const {
        static const QHash<QString, QVariantMap> emptyTable;
        return emptyTable;
    }

public:
    /**
     * @brief 获取指定 action 的 AI 描述符
     * @param actionCode 动作代码
     * @return 描述符（如果支持该 action），否则返回 std::nullopt
     *
     * 使用 aiDescriptorTable() 进行查找，O(1) 复杂度。
     */
    std::optional<QVariantMap> getAIDescriptor(const QString& actionCode) const override {
        const auto& table = aiDescriptorTable();
        auto it = table.find(actionCode);
        if (it != table.end()) {
            return std::make_optional(*it);
        }
        return std::nullopt;
    }

protected:
    // 保存上下文供子类使用
    std::shared_ptr<ActionContext> m_context;

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
};
