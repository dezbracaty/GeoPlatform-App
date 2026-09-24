#pragma once

#include <QString>
#include "Foundation/Log.h"
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <QVariantMap>

// 前向声明
class IActionHandlerBase;
class ActionManager;

// Handler注册表（单例）
class ActionHandlerRegistry {
public:
    using HandlerPtr = std::shared_ptr<IActionHandlerBase>;
    using HandlerFactory = std::function<HandlerPtr()>;

    // 获取单例实例
    static ActionHandlerRegistry& instance() {
        static ActionHandlerRegistry registry;
        return registry;
    }

    // 注册Handler工厂
    bool registerFactory(const QString& actionCode, HandlerFactory factory) {
        if (actionCode.isEmpty() || !factory) {
            LOG_ERROR("Cannot register an empty action code or factory");
            return false;
        }
        const auto [_, inserted] =
            m_factories.emplace(actionCode, std::move(factory));
        if (!inserted) {
            LOG_ERROR("Duplicate action registration rejected: {}",
                      actionCode.toStdString());
        }
        return inserted;
    }

    // 注册多个ActionCode到同一个工厂
    bool registerFactory(const std::vector<QString>& actionCodes, HandlerFactory factory) {
        bool registered = true;
        for (const auto& code : actionCodes) {
            registered = registerFactory(code, factory) && registered;
        }
        return registered;
    }

    // 获取或创建Handler实例（缓存单例）
    HandlerPtr getOrCreateHandler(const QString& actionCode) {
        // 先检查缓存
        auto cacheIt = m_handlerCache.find(actionCode);
        if (cacheIt != m_handlerCache.end()) {
            return cacheIt->second;
        }

        // 缓存中没有，使用工厂创建
        auto factoryIt = m_factories.find(actionCode);
        if (factoryIt != m_factories.end()) {
            auto handler = factoryIt->second();
            if (handler) {
                LOG_DEBUG("Created handler for action: {}", actionCode.toStdString());
                m_handlerCache[actionCode] = handler; // 缓存Handler
                return handler;
            }
        }

        LOG_WARN("No factory registered for action: {}", actionCode.toStdString());
        return nullptr;
    }

    // 获取已存在的Handler（不创建新的）
    HandlerPtr getHandler(const QString& actionCode) {
        auto it = m_handlerCache.find(actionCode);
        return (it != m_handlerCache.end()) ? it->second : nullptr;
    }

    // 获取所有已注册的action codes
    std::vector<QString> getRegisteredActions() const {
        std::vector<QString> actions;
        for (const auto& pair : m_factories) {
            actions.push_back(pair.first);
        }
        return actions;
    }

    // 清空Handler缓存 - 用于程序关闭时确保Handler对象被正确销毁
    void clearCache() {
        m_handlerCache.clear();
    }

private:
    ActionHandlerRegistry() = default;
    ~ActionHandlerRegistry() = default;

    // 禁止拷贝
    ActionHandlerRegistry(const ActionHandlerRegistry&) = delete;
    ActionHandlerRegistry& operator=(const ActionHandlerRegistry&) = delete;

    std::unordered_map<QString, HandlerFactory> m_factories;
    std::unordered_map<QString, HandlerPtr> m_handlerCache; // Handler缓存
};
