#pragma once

#include "StandardActionHandler.hpp"

class SceneQueryHandler : public StandardActionHandler {
    Q_OBJECT

public:
    explicit SceneQueryHandler(QObject* parent = nullptr);
    ~SceneQueryHandler() override = default;

    HandlerType getHandlerType() const override {
        return HandlerType::Exclusive;
    }

    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onEnterForAI(std::shared_ptr<ActionContext> context) override;

protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;

private:
    bool executeQuery(std::shared_ptr<ActionContext> context) const;
};

