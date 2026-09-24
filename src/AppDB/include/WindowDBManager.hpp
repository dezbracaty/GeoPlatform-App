#pragma once

#include <memory>
#include <vector>
#include <BaseID.hpp>
#include <SystemTypes.hpp>

// 前向声明
class WindowDB;
class DocumentManager;

/**
 * @brief WindowDB 特定管理器
 *
 * 这个类处理 WindowDB 特定的操作，避免 BaseDB 依赖 AppDB
 */
class WindowDBManager {
public:
    /**
     * @brief 构造函数
     * @param docManager DocumentManager 的引用
     */
    explicit WindowDBManager(DocumentManager* docManager);

    /**
     * @brief 注册 WindowDB 实例
     * @param windowDB WindowDB 实例
     * @return 实例 ID
     */
    DBInstanceID registerWindowDB(std::shared_ptr<WindowDB> windowDB);

    /**
     * @brief 获取所有 WindowDB 实例
     * @return WindowDB 实例列表
     */
    std::vector<std::shared_ptr<WindowDB>> getAllWindowDBs() const;

    /**
     * @brief 获取活动窗口 DB（当前正在使用的窗口）
     * @return 活动窗口 DB，如果没有则返回 nullptr
     */
    std::shared_ptr<WindowDB> getActiveWindowDB() const;

    /**
     * @brief 设置活动窗口
     * @param windowId 窗口 DB 的实例 ID
     * @return 是否成功设置
     */
    bool setActiveWindow(const DBInstanceID& windowId);

private:
    DocumentManager* m_docManager;
};