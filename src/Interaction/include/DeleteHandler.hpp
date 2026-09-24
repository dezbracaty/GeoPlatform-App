#pragma once

#include "StandardActionHandler.hpp"
#include <memory>

/**
 * @brief DeleteHandler - 处理删除选中模型的操作
 *
 * 当用户按下Delete键时，删除当前选中的所有ActorDB对象
 */
class DeleteHandler : public StandardActionHandler {
    Q_OBJECT

public:
    explicit DeleteHandler(QObject* parent = nullptr);
    virtual ~DeleteHandler() = default;

protected:
    void onEnter(std::shared_ptr<ActionContext> context) override;

private:
    void deleteSelectedModels();
};