#pragma once

#include "StandardActionHandler.hpp"
#include <memory>

class UndoRedoHandler : public StandardActionHandler {
    Q_OBJECT

public:
    explicit UndoRedoHandler(QObject* parent = nullptr);
    virtual ~UndoRedoHandler() = default;
    HandlerType getHandlerType() const override {
        return HandlerType::Middleware;
    }
    void onEnterForAI(std::shared_ptr<ActionContext> context) override;

protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;

protected:
    void onEnter(std::shared_ptr<ActionContext> context) override;

private:
    void performUndo();
    void performRedo();
    void clearHistory();
};
