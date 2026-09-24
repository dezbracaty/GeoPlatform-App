#pragma once

#include "StandardActionHandler.hpp"

/**
 * One-shot camera navigation actions.
 *
 * Continuous pointer gestures remain in PanRotateBGHandler. This handler is
 * the ActionManager adapter for fit, reset, preset-view and projection commands.
 */
class CameraNavigationActionHandler : public StandardActionHandler {
    Q_OBJECT

public:
    explicit CameraNavigationActionHandler(QObject* parent = nullptr);
    HandlerType getHandlerType() const override {
        return HandlerType::Middleware;
    }
    void onEnterForAI(std::shared_ptr<ActionContext> context) override;

protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;
    void onEnter(std::shared_ptr<ActionContext> context) override;

private:
    bool performAction(std::shared_ptr<ActionContext> context);
};
