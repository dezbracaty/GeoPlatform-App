#include "ActionManager.hpp"
#include "BaseInteractionRegistration.hpp"
#include "IActionHandlerBase.hpp"
#include "ActionContext.hpp"
#include "ActionHandlerRegistry.hpp"
#include "Foundation/Log.h"
#include "../include/ActionError.hpp"
#include "ScopedTransactionContextMetadata.hpp"
#include <QQmlEngine>
#include <QJSEngine>
#include <QTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QUuid>
#include <algorithm>

namespace {

bool isNumberLike(const QVariant& value) {
    return value.canConvert<double>() &&
           (value.typeId() == QMetaType::Double ||
            value.typeId() == QMetaType::Float ||
            value.typeId() == QMetaType::Int ||
            value.typeId() == QMetaType::LongLong ||
            value.typeId() == QMetaType::UInt ||
            value.typeId() == QMetaType::ULongLong);
}

bool matchesPrimitiveType(const QVariant& value, const QString& expectedType) {
    const QString type = expectedType.trimmed().toLower();
    if (type == "string") {
        return value.typeId() == QMetaType::QString;
    }
    if (type == "number") {
        return isNumberLike(value);
    }
    if (type == "bool" || type == "boolean") {
        return value.typeId() == QMetaType::Bool;
    }
    if (type == "object") {
        return value.typeId() == QMetaType::QVariantMap;
    }
    if (type == "array") {
        return value.typeId() == QMetaType::QVariantList || value.typeId() == QMetaType::QStringList;
    }
    return true;
}

bool validateEnum(const QVariant& value, const QVariantList& allowedValues) {
    if (allowedValues.isEmpty()) {
        return true;
    }
    for (const auto& allowed : allowedValues) {
        if (allowed == value) {
            return true;
        }
        if (allowed.toString() == value.toString()) {
            return true;
        }
    }
    return false;
}

bool isStructuredContractSchema(const QVariantMap& schema) {
    if (schema.isEmpty()) {
        return false;
    }
    return schema.contains("required") ||
           schema.contains("anyOfRequired") ||
           schema.contains("properties") ||
           schema.contains("allowUnknownParams");
}

bool descriptorRequiresStructuredSchema(const QVariantMap& descriptor) {
    const QString level = descriptor.value("sideEffectLevel").toString().trimmed().toLower();
    return level == "write";
}

bool exposeInCapabilityIndex(const QVariantMap& descriptor) {
    return descriptor.value("exposeInCapabilityIndex", true).toBool();
}

bool allowAIInvoke(const QVariantMap& descriptor) {
    return descriptor.value("allowAIInvoke", true).toBool();
}

bool validateWithContract(const QString& actionCode,
                          const QVariantMap& params,
                          const QVariantMap& descriptor,
                          QString& error) {
    error.clear();
    const QVariantMap schema = descriptor.value("inputSchema").toMap();
    if (descriptorRequiresStructuredSchema(descriptor) && !isStructuredContractSchema(schema)) {
        error = QString("%1 missing structured inputSchema contract").arg(actionCode);
        return false;
    }
    if (schema.isEmpty()) {
        return true;
    }

    const QVariantList required = schema.value("required").toList();
    for (const auto& keyValue : required) {
        const QString key = keyValue.toString().trimmed();
        if (key.isEmpty()) {
            continue;
        }
        if (!params.contains(key)) {
            error = QString("%1 missing required param '%2'").arg(actionCode, key);
            return false;
        }
    }

    const QVariantList anyOfRequired = schema.value("anyOfRequired").toList();
    if (!anyOfRequired.isEmpty()) {
        bool anyGroupSatisfied = false;
        for (const auto& groupValue : anyOfRequired) {
            const QVariantList group = groupValue.toList();
            if (group.isEmpty()) {
                continue;
            }
            bool groupSatisfied = true;
            for (const auto& keyValue : group) {
                const QString key = keyValue.toString().trimmed();
                if (key.isEmpty()) {
                    continue;
                }
                if (!params.contains(key)) {
                    groupSatisfied = false;
                    break;
                }
            }
            if (groupSatisfied) {
                anyGroupSatisfied = true;
                break;
            }
        }
        if (!anyGroupSatisfied) {
            error = QString("%1 missing any required key group in anyOfRequired").arg(actionCode);
            return false;
        }
    }

    const QVariantMap properties = schema.value("properties").toMap();
    const bool allowUnknown = schema.value("allowUnknownParams", true).toBool();
    if (!allowUnknown) {
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            if (it.key().startsWith("__")) {
                continue;
            }
            if (!properties.contains(it.key())) {
                error = QString("%1 contains unknown param '%2'").arg(actionCode, it.key());
                return false;
            }
        }
    }

    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
        const QString key = it.key();
        if (!it.value().canConvert<QVariantMap>()) {
            error = QString("%1 inputSchema.properties['%2'] must be an object").arg(actionCode, key);
            return false;
        }

        if (!params.contains(key)) {
            continue;
        }

        const QVariantMap propRule = it.value().toMap();
        const QString expectedType = propRule.value("type").toString();
        if (!expectedType.isEmpty() && !matchesPrimitiveType(params.value(key), expectedType)) {
            error = QString("%1 param '%2' expects type '%3'").arg(actionCode, key, expectedType);
            return false;
        }

        const QVariantList enumValues = propRule.value("enum").toList();
        if (!enumValues.isEmpty() && !validateEnum(params.value(key), enumValues)) {
            error = QString("%1 param '%2' is outside enum set").arg(actionCode, key);
            return false;
        }
    }

    return true;
}

QVariantMap buildActionOutcome(qulonglong seq,
                               const QString& actionCode,
                               bool accepted,
                               bool success,
                               const QString& error,
                               const QVariant& result,
                               bool isAIInvoke,
                               HandlerType handlerType,
                               bool pending = false,
                               ActionErrorCode errorCode = ActionErrorCode::None) {
    QVariantMap outcome;
    const ActionErrorMeta meta = actionErrorMeta(errorCode);
    outcome.insert("seq", static_cast<qulonglong>(seq));
    outcome.insert("actionCode", actionCode);
    outcome.insert("accepted", accepted);
    outcome.insert("success", success);
    outcome.insert("error", error);
    outcome.insert("error_code", QString::fromLatin1(meta.code));
    outcome.insert("error_category", QString::fromLatin1(meta.category));
    outcome.insert("result", result);
    outcome.insert("isAIInvoke", isAIInvoke);
    outcome.insert("handlerType", static_cast<int>(handlerType));
    outcome.insert("pending", pending);
    return outcome;
}

} // namespace

ActionManager::ActionManager(QObject* parent)
    : QObject(parent) {
    // Debug: List all registered handlers after a short delay
    QTimer::singleShot(100, [this]() {
        auto registeredActions = ActionHandlerRegistry::instance().getRegisteredActions();
        LOG_DEBUG("=== Registered action handlers ===");
        LOG_DEBUG("Total count: {}", registeredActions.size());
        for (const auto& action : registeredActions) {
            LOG_DEBUG("  - {}", action.toStdString());
        }
        LOG_DEBUG("===================================");
    });
}

ActionManager::~ActionManager() {
    // LOG_INFO("ActionManager shutting down - cleaning all handlers");

    // 停止接受新的 actions
    m_acceptingActions = false;

    // 清理活跃 handler
    if (m_activeHandler) {
        LOG_INFO("Cleaning up active handler: {}", m_activeHandler->metaObject()->className());
        if (m_activeHandler->isActive()) {
            m_activeHandler->onExit();
        }
        m_activeHandler.reset();
    }

    // 清理暂停的 handler
    if (m_suspendedHandler) {
        LOG_INFO("Cleaning up suspended handler: {}", m_suspendedHandler->metaObject()->className());
        if (m_suspendedHandler->isActive()) {
            m_suspendedHandler->onExit();
        }
        m_suspendedHandler.reset();
    }

    // 清理中间层 handlers
    LOG_INFO("Cleaning up {} middleware handlers", m_middlewareHandlers.size());
    for (auto& handler : m_middlewareHandlers) {
        if (handler && handler->isActive()) {
            LOG_INFO("Cleaning up middleware handler: {}", handler->metaObject()->className());
            handler->onExit();
        }
    }
    m_middlewareHandlers.clear();

    // 清理后台 handlers
    LOG_INFO("Cleaning up {} background handlers", m_backgroundHandlers.size());
    for (auto& handler : m_backgroundHandlers) {
        if (handler && handler->isActive()) {
            LOG_INFO("Cleaning up background handler: {}", handler->metaObject()->className());
            handler->onExit();
        }
    }
    m_backgroundHandlers.clear();

    // 清理注册的 handlers
    m_handlers.clear();
    m_pendingInvocations.clear();
    m_environmentSuspendedHandlers.clear();

    // LOG_INFO("ActionManager shutdown complete");
}

void ActionManager::cleanupBeforeShutdown() {
    // 安全的关闭前清理 - 无日志记录，避免在程序关闭时崩溃

    // 停止接受新的 actions
    m_acceptingActions = false;

    // 清理活跃 handler
    if (m_activeHandler) {
        if (m_activeHandler->isActive()) {
            m_activeHandler->onExit();
        }
        m_activeHandler.reset();
    }

    // 清理暂停的 handler
    if (m_suspendedHandler) {
        if (m_suspendedHandler->isActive()) {
            m_suspendedHandler->onExit();
        }
        m_suspendedHandler.reset();
    }

    // 清理中间层 handlers
    for (auto& handler : m_middlewareHandlers) {
        if (handler && handler->isActive()) {
            handler->onExit();
        }
    }
    m_middlewareHandlers.clear();

    // 清理后台 handlers
    for (auto& handler : m_backgroundHandlers) {
        if (handler && handler->isActive()) {
            handler->onExit();
        }
    }
    m_backgroundHandlers.clear();

    // 清理注册的 handlers
    m_handlers.clear();
    m_pendingInvocations.clear();
    m_environmentSuspendedHandlers.clear();

    // 🔑 关键修复：清空ActionHandlerRegistry的缓存，确保Handler对象被真正销毁
    ActionHandlerRegistry::instance().clearCache();
}

bool ActionManager::triggerAction(const QString& actionCode, const QVariantMap& params) {
    return triggerActionInternal(actionCode, params, false);
}

bool ActionManager::triggerActionInternal(const QString& actionCode,
                                          const QVariantMap& params,
                                          bool isAIInvoke,
                                          const QString& invocationId,
                                          const QVariantMap& invocationMetadata) {
    const qulonglong invokeSeq = ++m_lastActionOutcomeSeq;

    // 检查是否接受新的 actions
    if (!m_acceptingActions) {
        LOG_WARN("ActionManager is not accepting new actions (shutting down). Action: {}", actionCode.toStdString());
        m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                 actionCode,
                                                 false,
                                                 false,
                                                 "ActionManager is not accepting new actions",
                                                 {},
                                                 isAIInvoke,
                                                 HandlerType::Exclusive, false, ActionErrorCode::SystemUnavailable);
        return false;
    }

    // 先获取 handler，以便检查其类型
    // Background 类型的 handler 可以在有 m_activeHandler 时执行
    auto handler = ActionHandlerRegistry::instance().getOrCreateHandler(actionCode);
    if (!handler) {
        LOG_WARN("No handler registered for action: {}", actionCode.toStdString());
        m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                 actionCode,
                                                 false,
                                                 false,
                                                 "No handler registered for action",
                                                 {},
                                                 isAIInvoke,
                                                 HandlerType::Exclusive, false, ActionErrorCode::InvalidParams);
        return false;
    }

    if (!handler->supportsEnvironment(m_currentEnvironment)) {
        const QString message = QStringLiteral("Action is unavailable in environment '%1'")
                                    .arg(m_currentEnvironment);
        LOG_WARN("Action '{}' rejected in environment '{}'",
                 actionCode.toStdString(), m_currentEnvironment.toStdString());
        m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                 actionCode,
                                                 false,
                                                 false,
                                                 message,
                                                 {},
                                                 isAIInvoke,
                                                 handler->getHandlerType(),
                                                 false,
                                                 ActionErrorCode::SystemUnavailable);
        return false;
    }

    if (isAIInvoke && !handler->getAIDescriptor(actionCode).has_value()) {
        LOG_WARN("Action '{}' is not available for AI invocation", actionCode.toStdString());
        m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                 actionCode,
                                                 false,
                                                 false,
                                                 "Action is not available for AI invocation",
                                                 {},
                                                 true,
                                                 handler->getHandlerType(), false, ActionErrorCode::InvalidParams);
        return false;
    }

    const HandlerType handlerType = handler->getHandlerType();
    const bool isBackgroundHandler = (handlerType == HandlerType::Background);
    const bool isMiddlewareHandler = (handlerType == HandlerType::Middleware);
    const bool isExclusiveHandler = (handlerType == HandlerType::Exclusive);

    LOG_DEBUG("Handler found for action: {}, type: {}, aiInvoke={}",
              actionCode.toStdString(),
              static_cast<int>(handlerType),
              isAIInvoke ? "true" : "false");

    // 仅 Exclusive action 会抢占 active slot。Middleware/Background 不应中断当前前台交互。
    if (m_activeHandler && isExclusiveHandler) {
        // 如果当前handler是persistent的，允许新action并终止旧handler
        if (m_activeHandler->isPersistent()) {
            LOG_DEBUG("Terminating persistent handler '{}' to execute new action '{}'",
                     m_activeHandler->metaObject()->className(), actionCode.toStdString());
            m_activeHandler->onExit();
            m_activeHandler = nullptr;
        } else {
            LOG_DEBUG("Cannot trigger action while another handler is active: {}", actionCode.toStdString());
            m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                     actionCode,
                                                     false,
                                                     false,
                                                     "Another handler is active",
                                                     {},
                                                     isAIInvoke,
                                                     handlerType, false, ActionErrorCode::SystemUnavailable);
            return false;
        }
    }

    if (m_activeHandler && (isBackgroundHandler || isMiddlewareHandler)) {
        LOG_DEBUG("Allowing {} handler '{}' while m_activeHandler exists: {}",
                  isBackgroundHandler ? "Background" : "Middleware",
                  actionCode.toStdString(),
                  m_activeHandler->metaObject()->className());
    }

    // suspendedHandler 存在时，仅阻止 Exclusive action；允许 Background/Middleware 并行执行。
    if (m_suspendedHandler && isExclusiveHandler) {
        LOG_WARN("🚫 Cannot trigger action while a handler is suspended. Action: {}", actionCode.toStdString());
        LOG_WARN("🚫 Returning false - action blocked (Exclusive handler)");
        m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                 actionCode,
                                                 false,
                                                 false,
                                                 "A handler is suspended",
                                                 {},
                                                 isAIInvoke,
                                                 handlerType, false, ActionErrorCode::SystemUnavailable);
        return false;
    }

    if (m_suspendedHandler && (isBackgroundHandler || isMiddlewareHandler)) {
        LOG_DEBUG("✅ Allowing {} handler '{}' while suspendedHandler exists",
                  isBackgroundHandler ? "Background" : "Middleware",
                  actionCode.toStdString());
    }

    // 连接后台处理信号
    connect(handler.get(), &IActionHandlerBase::requestBackgroundExecution,
            this, &ActionManager::onHandlerRequestBackground, Qt::UniqueConnection);
    connect(handler.get(), &IActionHandlerBase::requestResume,
            this, &ActionManager::onHandlerRequestResume, Qt::UniqueConnection);
    connect(handler.get(), &IActionHandlerBase::requestExit,
            this, &ActionManager::onHandlerRequestExit, Qt::UniqueConnection);
    connect(handler.get(), &IActionHandlerBase::invocationCompleted,
            this, &ActionManager::onHandlerInvocationCompleted,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));

    // 创建ActionContext
    auto context = std::make_shared<ActionContext>();
    context->setActionCode(actionCode);
    context->setParams(params);
    context->setInvocationId(invocationId);
    context->setInvocationMetadata(invocationMetadata);

    const HandlerPtr previousActiveHandler = m_activeHandler;

    // 只有 Exclusive handler 才占用 active slot
    if (isExclusiveHandler) {
        m_activeHandler = handler;
    }

    // 调用Handler
    try {
        emit actionTriggered(actionCode);

        if (isAIInvoke) {
            handler->onEnterForAI(context);
        } else {
            handler->onEnter(context);
        }

        // 对于同步Handler（如CreateGeometryHandler），它们会保持active状态
        // 我们需要检查Handler的类型来判断是否同步完成
        LOG_DEBUG("Checking handler type: {}", static_cast<int>(handler->getHandlerType()));

        // Background 类型的 Handler 直接添加到背景层。
        if (isBackgroundHandler) {
            LOG_DEBUG("Handler is Background type, adding to background layer");
            auto it = std::find(m_backgroundHandlers.begin(), m_backgroundHandlers.end(), handler);
            if (it == m_backgroundHandlers.end()) {
                m_backgroundHandlers.push_back(handler);
            } else {
                LOG_DEBUG("Background handler already registered in background layer");
            }
            m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                     actionCode,
                                                     true,
                                                     !context->hasError(),
                                                     context->getError(),
                                                     context->getResult(),
                                                     isAIInvoke,
                                                     handlerType,
                                                     true,
                                                     context->getErrorCode());
            emit actionCompleted(actionCode, true);
        } else if (isMiddlewareHandler) {
            auto it = std::find(m_middlewareHandlers.begin(), m_middlewareHandlers.end(), handler);
            if (it == m_middlewareHandlers.end()) {
                m_middlewareHandlers.push_back(handler);
            } else {
                LOG_DEBUG("Middleware handler already registered in middleware layer");
            }

            const bool handlerStillActive = handler->isActive();
            LOG_DEBUG("Middleware handler active status after onEnter: {}", handlerStillActive);

            if (!handlerStillActive) {
                LOG_DEBUG("Middleware handler completed synchronously, cleaning up immediately");
                handler->onExit();
                m_middlewareHandlers.erase(
                    std::remove(m_middlewareHandlers.begin(), m_middlewareHandlers.end(), handler),
                    m_middlewareHandlers.end());
                m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                         actionCode,
                                                         true,
                                                         !context->hasError(),
                                                         context->getError(),
                                                         context->getResult(),
                                                         isAIInvoke,
                                                         handlerType,
                                                         false,
                                                         context->getErrorCode());
                emit actionCompleted(actionCode, !context->hasError());
            } else if (handler->isPersistent()) {
                LOG_INFO("🔒 Middleware handler is persistent, keeping it active");
                m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                         actionCode,
                                                         true,
                                                         !context->hasError(),
                                                         context->getError(),
                                                         context->getResult(),
                                                         isAIInvoke,
                                                         handlerType,
                                                         true,
                                                         context->getErrorCode());
                emit actionCompleted(actionCode, true);
            } else {
                LOG_INFO("⏳ Middleware handler still active, scheduling cleanup");
                m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                         actionCode,
                                                         true,
                                                         !context->hasError(),
                                                         context->getError(),
                                                         context->getResult(),
                                                         isAIInvoke,
                                                         handlerType,
                                                         true,
                                                         context->getErrorCode());
                QTimer::singleShot(0, this, [this, handler, actionCode, context]() {
                    auto it = std::find(m_middlewareHandlers.begin(), m_middlewareHandlers.end(), handler);
                    if (it == m_middlewareHandlers.end()) {
                        return;
                    }
                    LOG_INFO("⏳ Middleware timer cleanup: removing handler");
                    handler->onExit();
                    m_middlewareHandlers.erase(
                        std::remove(m_middlewareHandlers.begin(), m_middlewareHandlers.end(), handler),
                        m_middlewareHandlers.end());
                    emit actionCompleted(actionCode, !context->hasError());
                });
            }
        } else if (handlerType == HandlerType::Exclusive) {
            // 对于独占型Handler，检查它是否已经不再活跃（在自己的onEnter中调用了onExit）
            bool handlerStillActive = handler->isActive();
            LOG_DEBUG("Handler active status after onEnter: {}", handlerStillActive);

            if (m_suspendedHandler == handler) {
                // A suspended importer is inactive in the foreground, but its
                // background work is still pending. Do not report completion.
                m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                         actionCode,
                                                         true,
                                                         !context->hasError(),
                                                         context->getError(),
                                                         context->getResult(),
                                                         isAIInvoke,
                                                         handlerType,
                                                         true,
                                                         context->getErrorCode());
            } else if (!handlerStillActive) {
                // Handler已经自己完成并调用了onExit，直接清理
                LOG_DEBUG("Handler completed synchronously, cleaning up immediately");
                m_activeHandler = nullptr;
                m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                         actionCode,
                                                         true,
                                                         !context->hasError(),
                                                         context->getError(),
                                                         context->getResult(),
                                                         isAIInvoke,
                                                         handlerType,
                                                         false,
                                                         context->getErrorCode());
                emit actionCompleted(actionCode, !context->hasError());
            } else if (handler->isPersistent()) {
                // 持久性Handler，不自动清理
                LOG_INFO("🔒 Handler is persistent, keeping it active");
                m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                         actionCode,
                                                         true,
                                                         !context->hasError(),
                                                         context->getError(),
                                                         context->getResult(),
                                                         isAIInvoke,
                                                         handlerType,
                                                         true,
                                                         context->getErrorCode());
                emit actionCompleted(actionCode, true);
                // 注意：m_activeHandler 保持不变，Handler持续处理事件
            } else {
                // Handler仍然活跃，使用QTimer::singleShot延迟清理
                // 这适用于需要异步执行的Handler
                LOG_INFO("⏳ Handler still active, scheduling cleanup");
                m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                         actionCode,
                                                         true,
                                                         !context->hasError(),
                                                         context->getError(),
                                                         context->getResult(),
                                                         isAIInvoke,
                                                         handlerType,
                                                         true,
                                                         context->getErrorCode());
                QTimer::singleShot(0, this, [this, handler, actionCode, context]() {
                    if (m_activeHandler == handler && handler->isActive()) {
                        // Handler同步完成，调用onExit然后清理
                        LOG_INFO("⏳ Timer cleanup: calling onExit and clearing activeHandler");
                        handler->onExit();
                        m_activeHandler = nullptr;
                        emit actionCompleted(actionCode, !context->hasError());
                    } else {
                        LOG_INFO("⏳ Timer cleanup: handler already cleaned up or different handler active");
                    }
                });
            }
        }
        // Accepted asynchronous work may complete later; synchronous errors
        // must be observable by every caller, including menus and Journal.
        return !context->hasError();
    } catch (const std::exception& e) {
        if (isExclusiveHandler && m_activeHandler == handler) {
            m_activeHandler = previousActiveHandler;
        }

        LOG_WARN("💥 Handler execution failed: {}", e.what());
        m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                 actionCode,
                                                 false,
                                                 false,
                                                 QString("Handler execution failed: %1").arg(e.what()),
                                                 {},
                                                 isAIInvoke,
                                                 handlerType,
                                                 false,
                                                 ActionErrorCode::Internal);
        emit actionCompleted(actionCode, false);
        return false;
    }
}

QVariantList ActionManager::listActionsForAI() {
    QVariantList list;
    auto actions = ActionHandlerRegistry::instance().getRegisteredActions();
    std::sort(actions.begin(), actions.end());

    for (const auto& actionCode : actions) {
        auto handler = ActionHandlerRegistry::instance().getOrCreateHandler(actionCode);
        if (!handler) {
            continue;
        }

        auto itemOpt = handler->getAIDescriptor(actionCode);
        if (!itemOpt) {
            continue;
        }
        QVariantMap item = *itemOpt;

        if (!exposeInCapabilityIndex(item) || !allowAIInvoke(item)) {
            continue;
        }
        if (!item.contains("actionCode")) {
            item.insert("actionCode", actionCode);
        }
        if (!item.contains("title")) {
            item.insert("title", actionCode);
        }
        if (!item.contains("description")) {
            item.insert("description", "No AI descriptor provided");
        }
        item.insert("handlerClass", handler->metaObject()->className());
        item.insert("handlerType", static_cast<int>(handler->getHandlerType()));
        item.insert("availableForAI", true);
        list.push_back(item);
    }

    return list;
}

QVariantList ActionManager::listActionSummariesForAI() {
    QVariantList list;
    auto actions = ActionHandlerRegistry::instance().getRegisteredActions();
    std::sort(actions.begin(), actions.end());

    for (const auto& actionCode : actions) {
        auto handler = ActionHandlerRegistry::instance().getOrCreateHandler(actionCode);
        if (!handler) {
            continue;
        }

        auto itemOpt = handler->getAIDescriptor(actionCode);
        if (!itemOpt) {
            continue;
        }
        QVariantMap item = *itemOpt;

        if (!exposeInCapabilityIndex(item) || !allowAIInvoke(item)) {
            continue;
        }
        // Keep concise, discriminative metadata for model-side action selection.
        QVariantMap summary;
        summary.insert("actionCode", item.value("actionCode", actionCode));
        summary.insert("title", item.value("title", actionCode));
        summary.insert("description", item.value("description", ""));
        summary.insert("tags", item.value("tags", QVariantList{}));
        summary.insert("sideEffectLevel", item.value("sideEffectLevel", "write"));
        list.push_back(summary);
    }
    return list;
}

QVariantMap ActionManager::describeActionForAI(const QString& actionCode) {
    QVariantMap info;
    info.insert("actionCode", actionCode);

    auto handler = ActionHandlerRegistry::instance().getOrCreateHandler(actionCode);
    if (!handler) {
        info.insert("found", false);
        info.insert("availableForAI", false);
        info.insert("message", "Action not registered");
        return info;
    }

    info.insert("found", true);
    auto descriptorOpt = handler->getAIDescriptor(actionCode);
    info.insert("availableForAI", descriptorOpt.has_value());
    info.insert("handlerClass", handler->metaObject()->className());
    info.insert("handlerType", static_cast<int>(handler->getHandlerType()));

    if (descriptorOpt) {
        QVariantMap descriptor = *descriptorOpt;
        info.insert("descriptor", descriptor);
        info.insert("exposedInCapabilityIndex", exposeInCapabilityIndex(descriptor));
        info.insert("allowAIInvoke", allowAIInvoke(descriptor));
    }

    return info;
}

bool ActionManager::triggerActionByAI(const QString& actionCode,
                                      const QVariantMap& params,
                                      const QVariantMap& meta) {
    const qulonglong invokeSeq = ++m_lastActionOutcomeSeq;
    auto handler = ActionHandlerRegistry::instance().getOrCreateHandler(actionCode);
    if (!handler) {
        LOG_WARN("No handler registered for AI action: {}", actionCode.toStdString());
        m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                 actionCode,
                                                 false,
                                                 false,
                                                 "No handler registered for AI action",
                                                 {},
                                                 true,
                                                 HandlerType::Exclusive,
                                                 false,
                                                 ActionErrorCode::InvalidParams);
        return false;
    }
    auto descriptorOpt = handler->getAIDescriptor(actionCode);
    if (!descriptorOpt) {
        LOG_WARN("AI action '{}' rejected: handler is not AI-available", actionCode.toStdString());
        m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                 actionCode,
                                                 false,
                                                 false,
                                                 "Handler is not AI-available",
                                                 {},
                                                 true,
                                                 handler->getHandlerType(), false, ActionErrorCode::InvalidParams);
        return false;
    }

    QVariantMap descriptor = *descriptorOpt;
    if (!allowAIInvoke(descriptor)) {
        LOG_WARN("AI action '{}' rejected: descriptor allowAIInvoke=false", actionCode.toStdString());
        m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                 actionCode,
                                                 false,
                                                 false,
                                                 "Action not allowed for AI invocation",
                                                 {},
                                                 true,
                                                 handler->getHandlerType(), false, ActionErrorCode::InvalidParams);
        return false;
    }

    QString validationError;
    if (!validateWithContract(actionCode, params, descriptor, validationError)) {
        LOG_WARN("AI action '{}' rejected by contract validation: {}",
                 actionCode.toStdString(),
                 validationError.toStdString());
        m_lastActionOutcome = buildActionOutcome(invokeSeq,
                                                 actionCode,
                                                 false,
                                                 false,
                                                 validationError,
                                                 {},
                                                 true,
                                                 handler->getHandlerType(), false, ActionErrorCode::ContractValidation);
        return false;
    }

    const QString invocationId = meta.value("invocationId").toString().trimmed();
    const ScopedTransactionContextMetadata txScope(actionCode, meta);
    const bool accepted = triggerActionInternal(actionCode, params, true, invocationId, meta);

    QVariantMap outcome = m_lastActionOutcome;
    if (outcome.value("actionCode").toString() != actionCode) {
        outcome = buildActionOutcome(invokeSeq,
                                     actionCode,
                                     accepted,
                                     accepted,
                                     accepted ? QString() : QString("Action rejected"),
                                     {},
                                     true,
                                     handler->getHandlerType(),
                                     false,
                                     accepted ? ActionErrorCode::None : ActionErrorCode::Internal);
    }
    outcome.insert("contractValidated", true);
    m_lastActionOutcome = outcome;

    const bool success = outcome.value("success", accepted).toBool();
    return accepted && success;
}

QVariantMap ActionManager::invokeActionByAI(const QString& actionCode,
                                            const QVariantMap& params,
                                            const QVariantMap& meta) {
    auto handler = ActionHandlerRegistry::instance().getOrCreateHandler(actionCode);
    auto descriptor = handler ? handler->getAIDescriptor(actionCode) : std::nullopt;
    const bool isAsync = descriptor && descriptor->value("async", false).toBool();

    if (!isAsync) {
        triggerActionByAI(actionCode, params, meta);
        QVariantMap receipt = m_lastActionOutcome;
        receipt.insert("status", receipt.value("success").toBool()
                                     ? QStringLiteral("completed")
                                     : QStringLiteral("failed"));
        receipt.insert("async", false);
        return receipt;
    }

    const QString invocationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap invocationMeta = meta;
    invocationMeta.insert("invocationId", invocationId);

    QVariantMap pending;
    pending.insert("actionCode", actionCode);
    pending.insert("meta", invocationMeta);
    m_pendingInvocations.insert(invocationId, pending);

    const bool accepted = triggerActionByAI(actionCode, params, invocationMeta);
    QVariantMap receipt = m_lastActionOutcome;
    receipt.insert("invocationId", invocationId);
    receipt.insert("async", true);

    if (!accepted) {
        m_pendingInvocations.remove(invocationId);
        receipt.insert("status", QStringLiteral("failed"));
        receipt.insert("pending", false);
        return receipt;
    }

    pending.insert("seq", receipt.value("seq"));
    m_pendingInvocations.insert(invocationId, pending);

    receipt.insert("success", true);
    receipt.insert("accepted", true);
    receipt.insert("status", QStringLiteral("running"));
    receipt.insert("pending", true);
    receipt.remove("result");
    m_lastActionOutcome = receipt;
    LOG_INFO("AI action accepted: action={} invocation={}",
             actionCode.toStdString(), invocationId.toStdString());
    return receipt;
}

void ActionManager::onHandlerInvocationCompleted(const QString& invocationId,
                                                 const QVariantMap& handlerOutcome) {
    auto it = m_pendingInvocations.find(invocationId);
    if (it == m_pendingInvocations.end()) {
        LOG_WARN("Ignoring completion for unknown invocation: {}", invocationId.toStdString());
        return;
    }

    const QVariantMap pending = it.value();
    m_pendingInvocations.erase(it);

    QVariantMap outcome = handlerOutcome;
    outcome.insert("invocationId", invocationId);
    outcome.insert("actionCode", pending.value("actionCode"));
    outcome.insert("seq", pending.value("seq"));
    outcome.insert("accepted", true);
    outcome.insert("async", true);
    outcome.insert("pending", false);
    if (!outcome.contains("status")) {
        outcome.insert("status", outcome.value("success").toBool()
                                     ? QStringLiteral("completed")
                                     : QStringLiteral("failed"));
    }
    m_lastActionOutcome = outcome;
    LOG_INFO("Action invocation completed: action={} invocation={} success={}",
             outcome.value("actionCode").toString().toStdString(),
             invocationId.toStdString(),
             outcome.value("success").toBool() ? "true" : "false");
    emit invocationCompleted(invocationId, outcome);
}

QVariantMap ActionManager::triggerActionSequenceByAI(const QVariantList& steps,
                                                     const QVariantMap& meta) {
    QVariantMap result;
    result.insert("success", false);
    result.insert("totalSteps", steps.size());
    result.insert("executedSteps", 0);
    QVariantList stepResults;
    result.insert("stepResults", stepResults);

    if (steps.isEmpty()) {
        result.insert("success", true);
        result.insert("message", "No steps to execute");
        return result;
    }

    for (int i = 0; i < steps.size(); ++i) {
        const QVariant stepValue = steps.at(i);
        if (!stepValue.canConvert<QVariantMap>()) {
            result.insert("error", "Invalid step type (expect object)");
            result.insert("failedStepIndex", i);
            return result;
        }

        const QVariantMap step = stepValue.toMap();
        const QString actionCode = step.value("actionCode").toString().trimmed();
        if (actionCode.isEmpty()) {
            result.insert("error", "Missing actionCode");
            result.insert("failedStepIndex", i);
            return result;
        }

        const QVariantMap params = step.value("params").toMap();
        QVariantMap stepMeta = meta;
        const QVariantMap stepMetaOverride = step.value("meta").toMap();
        for (auto it = stepMetaOverride.begin(); it != stepMetaOverride.end(); ++it) {
            stepMeta.insert(it.key(), it.value());
        }

        const bool ok = triggerActionByAI(actionCode, params, stepMeta);
        QVariantMap stepReceipt = m_lastActionOutcome;
        if (stepReceipt.value("actionCode").toString() != actionCode) {
            stepReceipt.insert("actionCode", actionCode);
            stepReceipt.insert("success", ok);
        }
        stepReceipt.insert("stepIndex", i);
        stepReceipt.insert("requestedParams", params);
        stepResults.push_back(stepReceipt);
        result.insert("stepResults", stepResults);

        if (!ok) {
            QString error = stepReceipt.value("error").toString().trimmed();
            if (error.isEmpty()) {
                error = "Step execution failed";
            }
            result.insert("error", error);
            result.insert("failedStepIndex", i);
            result.insert("failedActionCode", actionCode);
            result.insert("executedSteps", i);
            return result;
        }

        // Process zero-delay cleanup events between steps.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        result.insert("executedSteps", i + 1);
    }

    result.insert("success", true);
    return result;
}

void ActionManager::registerHandler(const QString& actionCode, HandlerPtr handler) {
    if (actionCode.isEmpty() || !handler) {
        LOG_WARN("Invalid handler registration attempt");
        return;
    }

    m_handlers[actionCode] = handler;
    LOG_INFO("Handler registered for action: {}", actionCode.toStdString());
}

void ActionManager::addBackgroundHandler(HandlerPtr handler) {
    if (handler) {
        m_backgroundHandlers.push_back(handler);
    }
}

void ActionManager::addMiddlewareHandler(HandlerPtr handler) {
    if (handler) {
        m_middlewareHandlers.push_back(handler);
    }
}

void ActionManager::setActiveHandler(HandlerPtr handler) {
    if (m_activeHandler) {
        m_activeHandler->onExit();
    }

    m_activeHandler = handler;

    if (m_activeHandler) {
        auto context = std::make_shared<ActionContext>();
        m_activeHandler->onEnter(context);
    }
}

bool ActionManager::canRunHandler(const HandlerPtr& handler) const {
    return handler && handler->isActive() &&
           handler->supportsEnvironment(m_currentEnvironment);
}

void ActionManager::setEnvironment(const QString& environment) {
    const QString target = environment.trimmed();
    if (target.isEmpty()) {
        LOG_WARN("ActionManager: ignoring empty environment");
        return;
    }
    if (target == m_currentEnvironment) {
        return;
    }

    std::vector<HandlerPtr> knownHandlers;
    std::unordered_set<IActionHandlerBase*> knownPointers;
    const auto appendUnique = [&knownHandlers, &knownPointers](const HandlerPtr& handler) {
        if (handler && knownPointers.insert(handler.get()).second) {
            knownHandlers.push_back(handler);
        }
    };

    appendUnique(m_activeHandler);
    appendUnique(m_suspendedHandler);
    for (const auto& handler : m_middlewareHandlers) {
        appendUnique(handler);
    }
    for (const auto& handler : m_backgroundHandlers) {
        appendUnique(handler);
    }
    for (const auto& [actionCode, handler] : m_handlers) {
        Q_UNUSED(actionCode)
        appendUnique(handler);
    }

    for (auto it = m_environmentSuspendedHandlers.begin();
         it != m_environmentSuspendedHandlers.end();) {
        if (knownPointers.find(*it) == knownPointers.end()) {
            it = m_environmentSuspendedHandlers.erase(it);
        } else {
            ++it;
        }
    }

    const QString previous = m_currentEnvironment;
    m_currentEnvironment = target;
    for (const auto& handler : knownHandlers) {
        const bool supported = handler->supportsEnvironment(target);
        const bool suspendedByEnvironment =
            m_environmentSuspendedHandlers.find(handler.get()) !=
            m_environmentSuspendedHandlers.end();

        if (!supported && handler->isActive() && !suspendedByEnvironment) {
            handler->onSuspend();
            if (handler->isActive()) {
                m_environmentSuspendedHandlers.insert(handler.get());
            } else if (m_activeHandler == handler) {
                m_activeHandler.reset();
            }
        } else if (supported && suspendedByEnvironment) {
            m_environmentSuspendedHandlers.erase(handler.get());
            handler->onResume();
        }
    }

    LOG_INFO("ActionManager: environment changed from '{}' to '{}'",
             previous.toStdString(), target.toStdString());
}

bool ActionManager::onMousePressEvent(QMouseEvent* event) {
    return onMousePressEvent(MouseInputEvent{0, event});
}

bool ActionManager::onMousePressEvent(const MouseInputEvent& input) {
    QMouseEvent* event = input.event;
    if (!event) {
        return false;
    }
    if (!m_acceptingActions) {
        return false;  // 关闭时不处理事件
    }

    LOG_DEBUG("🖱️ ActionManager::onMousePressEvent called at ({}, {}) button={}",
             event->pos().x(), event->pos().y(), (int)event->button());

    // 先让活跃Handler处理
    if (canRunHandler(m_activeHandler) &&
        m_activeHandler->onMousePressEvent(input)) {
        LOG_DEBUG("🖱️ Active handler {} handled mouse press",
                  m_activeHandler->metaObject()->className());
        return true;
    }

    // 然后是中间层
    for (auto& handler : m_middlewareHandlers) {
        if (canRunHandler(handler) && handler->onMousePressEvent(input)) {
            return true;
        }
    }

    // 最后是后台层
    LOG_DEBUG("🖱️ ActionManager: Checking {} background handlers", m_backgroundHandlers.size());
    for (auto& handler : m_backgroundHandlers) {
        LOG_DEBUG("🖱️ ActionManager: Trying background handler {}", handler->metaObject()->className());
        if (canRunHandler(handler) && handler->onMousePressEvent(input)) {
            LOG_DEBUG("🖱️ Background handler {} handled mouse press",
                      handler->metaObject()->className());
            return true;
        }
    }

    return false;
}

bool ActionManager::onMouseMoveEvent(QMouseEvent* event) {
    return onMouseMoveEvent(MouseInputEvent{0, event});
}

bool ActionManager::onMouseMoveEvent(const MouseInputEvent& input) {
    QMouseEvent* event = input.event;
    if (!event) {
        return false;
    }
    if (!m_acceptingActions) {
        return false;  // 关闭时不处理事件
    }

    LOG_DEBUG("ActionManager::onMouseMoveEvent - activeHandler={}, pos=({},{})",
              m_activeHandler ? "YES" : "NO", event->pos().x(), event->pos().y());

    if (canRunHandler(m_activeHandler)) {
        LOG_DEBUG("Calling activeHandler->onMouseMoveEvent");
        if (m_activeHandler->onMouseMoveEvent(input)) {
            return true;
        }
    }

    for (auto& handler : m_middlewareHandlers) {
        if (canRunHandler(handler) && handler->onMouseMoveEvent(input)) {
            return true;
        }
    }

    // 特殊处理：对于鼠标移动事件，让所有背景处理器都有机会处理
    // 这样可以确保 hover 功能在相机操作时仍然工作
    bool handled = false;
    for (auto& handler : m_backgroundHandlers) {
        if (canRunHandler(handler) && handler->onMouseMoveEvent(input)) {
            handled = true;
            // 不要立即返回，继续让其他背景处理器处理
        }
    }

    return handled;
}

bool ActionManager::onMouseReleaseEvent(QMouseEvent* event) {
    return onMouseReleaseEvent(MouseInputEvent{0, event});
}

bool ActionManager::onMouseReleaseEvent(const MouseInputEvent& input) {
    QMouseEvent* event = input.event;
    if (!event) {
        return false;
    }
    if (!m_acceptingActions) {
        return false;  // 关闭时不处理事件
    }

    if (canRunHandler(m_activeHandler) && m_activeHandler->onMouseReleaseEvent(input)) {
        return true;
    }

    for (auto& handler : m_middlewareHandlers) {
        if (canRunHandler(handler) && handler->onMouseReleaseEvent(input)) {
            return true;
        }
    }

    for (auto& handler : m_backgroundHandlers) {
        if (canRunHandler(handler) && handler->onMouseReleaseEvent(input)) {
            return true;
        }
    }

    return false;
}

bool ActionManager::onKeyPressEvent(QKeyEvent* event) {
    return onKeyPressEvent(KeyInputEvent{0, event});
}

bool ActionManager::onKeyPressEvent(const KeyInputEvent& input) {
    QKeyEvent* event = input.event;
    if (!event) {
        return false;
    }
    if (!m_acceptingActions) {
        return false;
    }

    if (canRunHandler(m_activeHandler) && m_activeHandler->onKeyPressEvent(input)) {
        return true;
    }

    for (auto& handler : m_middlewareHandlers) {
        if (canRunHandler(handler) && handler->onKeyPressEvent(input)) {
            return true;
        }
    }

    for (auto& handler : m_backgroundHandlers) {
        if (canRunHandler(handler) && handler->onKeyPressEvent(input)) {
            return true;
        }
    }

    return false;
}

bool ActionManager::onKeyReleaseEvent(QKeyEvent* event) {
    return onKeyReleaseEvent(KeyInputEvent{0, event});
}

bool ActionManager::onKeyReleaseEvent(const KeyInputEvent& input) {
    QKeyEvent* event = input.event;
    if (!event) {
        return false;
    }
    if (!m_acceptingActions) {
        return false;
    }

    if (canRunHandler(m_activeHandler) && m_activeHandler->onKeyReleaseEvent(input)) {
        return true;
    }

    for (auto& handler : m_middlewareHandlers) {
        if (canRunHandler(handler) && handler->onKeyReleaseEvent(input)) {
            return true;
        }
    }

    for (auto& handler : m_backgroundHandlers) {
        if (canRunHandler(handler) && handler->onKeyReleaseEvent(input)) {
            return true;
        }
    }

    return false;
}

void ActionManager::deactivateActiveHandler() {
    if (!m_activeHandler) {
        return;
    }

    LOG_INFO("Deactivating active handler: {}", m_activeHandler->metaObject()->className());
    if (m_activeHandler->isActive()) {
        m_activeHandler->onExit();
    }
    m_activeHandler.reset();
}

void ActionManager::onHandlerRequestExit() {
    auto* requestingHandler = qobject_cast<IActionHandlerBase*>(sender());
    if (!requestingHandler || !m_activeHandler || requestingHandler != m_activeHandler.get()) {
        LOG_DEBUG("Ignoring exit request from a non-active handler");
        return;
    }
    deactivateActiveHandler();
}

bool ActionManager::onWheelEvent(QWheelEvent* event) {
    return onWheelEvent(WheelInputEvent{0, event});
}

bool ActionManager::onWheelEvent(const WheelInputEvent& input) {
    QWheelEvent* event = input.event;
    if (!event) {
        return false;
    }
    if (!m_acceptingActions) {
        return false;  // 关闭时不处理事件
    }

    if (canRunHandler(m_activeHandler) && m_activeHandler->onWheelEvent(input)) {
        return true;
    }

    for (auto& handler : m_middlewareHandlers) {
        if (canRunHandler(handler) && handler->onWheelEvent(input)) {
            return true;
        }
    }

    for (auto& handler : m_backgroundHandlers) {
        if (canRunHandler(handler) && handler->onWheelEvent(input)) {
            return true;
        }
    }

    return false;
}

void ActionManager::onHandlerRequestBackground() {
    if (!m_activeHandler) {
        LOG_WARN("No active handler to suspend");
        return;
    }

    LOG_INFO("Handler requesting background execution: {}", m_activeHandler->metaObject()->className());

    // 调用Handler的suspend方法
    m_activeHandler->onSuspend();

    // 将Handler移到暂停状态（保持Handler实例，不删除）
    m_suspendedHandler = m_activeHandler;
    m_activeHandler = nullptr; // 清空活跃状态，但Handler实例保存在m_suspendedHandler中
}

void ActionManager::onHandlerRequestResume() {
    if (!m_suspendedHandler) {
        LOG_WARN("No suspended handler to resume");
        return;
    }

    if (m_activeHandler) {
        LOG_WARN("Cannot resume handler while another handler is active");
        return;
    }

    LOG_INFO("Handler requesting resume: {}", m_suspendedHandler->metaObject()->className());

    // 将暂停的Handler恢复为活跃状态
    m_activeHandler = m_suspendedHandler;
    m_suspendedHandler = nullptr; // 清空暂停状态，Handler实例转移到m_activeHandler

    // 调用Handler的resume方法
    m_activeHandler->onResume();

    // 使用定期检查来监测Handler是否完成
    QTimer::singleShot(100, this, [this]() {
        // 循环检查Handler是否已完成
        checkActiveHandlerCompletion();
    });
}

void ActionManager::checkActiveHandlerCompletion() {
    if (m_activeHandler) {
        // 检查Handler是否已标记为非活跃（表示完成）
        if (!m_activeHandler->isActive()) {
            QString actionCode = m_activeHandler->metaObject()->className();
            bool success = true;

            LOG_INFO("Handler completed, cleaning up: {}", actionCode.toStdString());

            // 调用Handler的onExit，但保持Handler实例在缓存中（由Registry管理）
            m_activeHandler->onExit();
            m_activeHandler = nullptr; // 清空活跃状态，Handler返回Registry缓存

            LOG_INFO("Background handler completed and cleaned up: {}", actionCode.toStdString());
            emit actionCompleted(actionCode, success);
        } else {
            // Handler仍然活跃，继续检查
            QTimer::singleShot(50, this, [this]() {
                checkActiveHandlerCompletion();
            });
        }
    }
}

void ActionManager::stopAcceptingActions() {
    // LOG_INFO("ActionManager stopping accepting actions");
    m_acceptingActions = false;
}

REGISTER_BASE_INTERACTION_QML_SINGLETON(ActionManager, "ActionManager")

ActionManager* ActionManager::getInstance()
{
    static auto* instance = new ActionManager();
    return instance;
}
