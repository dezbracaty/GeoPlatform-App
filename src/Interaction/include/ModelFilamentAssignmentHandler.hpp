#pragma once

#include "StandardActionHandler.hpp"

class ModelFilamentAssignmentHandler final : public StandardActionHandler {
    Q_OBJECT

public:
    explicit ModelFilamentAssignmentHandler(QObject* parent = nullptr);

    HandlerType getHandlerType() const override {
        return HandlerType::Middleware;
    }
    void onEnter(std::shared_ptr<ActionContext> context) override;

protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;

private:
    bool apply(const std::shared_ptr<ActionContext>& context);
};
