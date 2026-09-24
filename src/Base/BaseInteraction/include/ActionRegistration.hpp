#pragma once

#include "ActionHandlerRegistry.hpp"
#include "ModuleRegistration.hpp"

#include <initializer_list>
#include <memory>

template <typename HandlerType>
void registerSharedActionHandler(std::initializer_list<const char*> actionCodes) {
    static std::weak_ptr<HandlerType> weakInstance;
    const auto sharedFactory = []() -> std::shared_ptr<IActionHandlerBase> {
        auto instance = weakInstance.lock();
        if (!instance) {
            instance = std::make_shared<HandlerType>();
            weakInstance = instance;
        }
        return instance;
    };

    auto& registry = ActionHandlerRegistry::instance();
    for (const auto* actionCode : actionCodes) {
        registry.registerFactory(QString::fromUtf8(actionCode), sharedFactory);
    }
}

template <typename HandlerType>
void registerPersistentActionHandler(std::initializer_list<const char*> actionCodes) {
    const auto sharedInstance = std::make_shared<HandlerType>();
    auto& registry = ActionHandlerRegistry::instance();
    for (const auto* actionCode : actionCodes) {
        registry.registerFactory(
            QString::fromUtf8(actionCode),
            [sharedInstance]() -> std::shared_ptr<IActionHandlerBase> {
                return sharedInstance;
            });
    }
}

#define GPLATFORM_ACTION_REGISTRATION(ModuleAccessor, HandlerClass, ...)         \
    GPLATFORM_ADD_MODULE_REGISTRATION(                                          \
        ModuleAccessor,                                                         \
        registerSharedActionHandler<HandlerClass>({__VA_ARGS__}))

#define GPLATFORM_PERSISTENT_ACTION_REGISTRATION(                               \
    ModuleAccessor, HandlerClass, ...)                                          \
    GPLATFORM_ADD_MODULE_REGISTRATION(                                          \
        ModuleAccessor,                                                         \
        registerPersistentActionHandler<HandlerClass>({__VA_ARGS__}))
