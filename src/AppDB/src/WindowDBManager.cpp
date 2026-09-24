#include "../include/WindowDBManager.hpp"
#include "../include/WindowDB.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"

WindowDBManager::WindowDBManager(DocumentManager* docManager)
    : m_docManager(docManager) {
}

DBInstanceID WindowDBManager::registerWindowDB(std::shared_ptr<WindowDB> windowDB) {
    if (!windowDB) {
        LOG_ERROR("WindowDBManager::registerWindowDB - null WindowDB instance");
        return INVALID_DB_ID;
    }

    // 使用通用的注册方法
    DBInstanceID id = m_docManager->registerDBInstance(windowDB, TypeID::WINDOW_DB);

    // 如果这是第一个窗口，设置为活动窗口
    if (m_docManager->getActiveWindowId() == INVALID_DB_ID) {
        m_docManager->setActiveWindow(id);
        LOG_DEBUG("WindowDBManager: Set first WindowDB as active window, ID: {}", id.getValue());
    }

    LOG_DEBUG("WindowDBManager: Registered WindowDB with ID: {}", id.getValue());
    return id;
}

std::vector<std::shared_ptr<WindowDB>> WindowDBManager::getAllWindowDBs() const {
    auto instances = m_docManager->getDBInstancesByType(TypeID::WINDOW_DB);
    std::vector<std::shared_ptr<WindowDB>> windowDBs;

    for (const auto& dbInstance : instances) {
        auto windowDB = std::dynamic_pointer_cast<WindowDB>(dbInstance);
        if (windowDB) {
            windowDBs.push_back(windowDB);
        }
    }

    LOG_DEBUG("WindowDBManager: Found {} WindowDB instances", windowDBs.size());
    return windowDBs;
}

std::shared_ptr<WindowDB> WindowDBManager::getActiveWindowDB() const {
    // 获取活动窗口 ID
    DBInstanceID activeId = m_docManager->getActiveWindowId();
    if (activeId == INVALID_DB_ID) {
        return nullptr;
    }

    // 获取活动窗口实例
    auto dbInstance = m_docManager->getDBInstance(activeId);
    if (!dbInstance || dbInstance->getTypeID() != TypeID::WINDOW_DB) {
        return nullptr;
    }

    return std::dynamic_pointer_cast<WindowDB>(dbInstance);
}

bool WindowDBManager::setActiveWindow(const DBInstanceID& windowId) {
    // 验证窗口存在且是 WindowDB 类型
    auto dbInstance = m_docManager->getDBInstance(windowId);
    if (!dbInstance) {
        LOG_ERROR("WindowDBManager::setActiveWindow - Window ID {} not found", windowId.getValue());
        return false;
    }

    if (dbInstance->getTypeID() != TypeID::WINDOW_DB) {
        LOG_ERROR("WindowDBManager::setActiveWindow - ID {} is not a WindowDB", windowId.getValue());
        return false;
    }

    // 设置活动窗口
    return m_docManager->setActiveWindow(windowId);
}