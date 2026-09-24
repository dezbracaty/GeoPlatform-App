#include "ApplicationHelper.h"
#include "CoreRegistration.hpp"
#include "ActionManager.hpp"
#include <DocumentManager.hpp>
#include <UndoRedoManager.hpp>
#include <QQmlEngine>
#include <QJSEngine>
#include <QFileInfo>
#include <QTimer>
#include <utility>

ApplicationHelper* ApplicationHelper::m_instance = nullptr;

bool ApplicationHelper::scheduleRestart(const QStringList& arguments, QString* error) {
    if (arguments.isEmpty() || !m_restartArguments.isEmpty() ||
        !QFileInfo(QCoreApplication::applicationFilePath()).isExecutable()) {
        if (error) *error = QStringLiteral("Cannot schedule application restart");
        return false;
    }
    m_restartArguments = arguments;
    QTimer::singleShot(0, this, [this] { exit(0); });
    return true;
}

QStringList ApplicationHelper::takeRestartArguments() {
    return std::exchange(m_restartArguments, {});
}

void ApplicationHelper::exit(int code) {
    // 在确认关闭时执行优雅关闭
    LOG_INFO("ApplicationHelper: Starting graceful shutdown sequence");

    // 直接清理最关键的组件 - ActionManager
    // 这是最重要的，因为它持有的 Handler 缓存会导致析构顺序问题
    cleanupHandlers();

    // 清理 UndoRedoManager
    if (auto* undoManager = UndoRedoManager::instance()) {
        undoManager->clear();
        LOG_INFO("UndoRedoManager cleared");
    }

    // 其他组件会通过正常的析构顺序自动清理
    // DocumentManager、TransactionManager 等都有正确的析构函数

    LOG_INFO("ApplicationHelper: Exiting application with code {}", code);

    // 刷新日志缓冲区，确保所有日志都写入文件
    Log::flush();

    // 注意：不要关闭日志系统，让它随静态变量自然析构
    QCoreApplication::exit(code);
}

void ApplicationHelper::cleanupHandlers() {
    // 获取 ActionManager 实例并执行安全清理
    auto* actionManager = ActionManager::getInstance();
    if (actionManager) {
        // 使用无日志的清理方法，避免在关闭时崩溃
        actionManager->cleanupBeforeShutdown();
        LOG_INFO("ActionManager handlers cleared");
    }
}

REGISTER_CORE_QML_SINGLETON(ApplicationHelper, "ApplicationHelper")
