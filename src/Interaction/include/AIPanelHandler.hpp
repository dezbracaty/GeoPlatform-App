#pragma once

#include "StandardActionHandler.hpp"

class AIPanelHandler : public StandardActionHandler {
    Q_OBJECT
public:
    explicit AIPanelHandler(QObject* parent = nullptr);
    ~AIPanelHandler() override = default;

    void onEnter(std::shared_ptr<ActionContext> context) override;
};
