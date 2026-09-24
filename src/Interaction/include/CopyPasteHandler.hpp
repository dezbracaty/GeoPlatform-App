#pragma once

#include "StandardActionHandler.hpp"
#include "SystemTypes.hpp"
#include "BaseID.hpp"
#include <vector>

// Forward declarations
class ActorDB;

/**
 * @brief 复制粘贴操作处理器
 *
 * 处理 "edit.copy" 和 "edit.paste" 两种操作
 * 使用最简单的内部剪贴板存储 DBInstanceID
 */
class CopyPasteHandler : public StandardActionHandler {
    Q_OBJECT

public:
    explicit CopyPasteHandler(QObject* parent = nullptr);
    ~CopyPasteHandler() override = default;

protected:
    void onEnter(std::shared_ptr<ActionContext> context) override;

private:
    // 核心操作方法
    void copySelectedModels();
    void pasteModels();

    // 辅助方法
    Vector3 getMouseWorldPosition() const;

    // 超简单的内部剪贴板 - 只存储对象ID
    static std::vector<DBInstanceID> s_copiedObjectIds;
};