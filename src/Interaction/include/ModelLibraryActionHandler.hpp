#pragma once

#include "StandardActionHandler.hpp"

class ModelLibraryActionHandler final : public StandardActionHandler {
    Q_OBJECT
    Q_DISABLE_COPY(ModelLibraryActionHandler)

public:
    explicit ModelLibraryActionHandler(QObject* parent = nullptr);
    HandlerType getHandlerType() const override { return HandlerType::Middleware; }
    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onExit() override;

protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;
};
