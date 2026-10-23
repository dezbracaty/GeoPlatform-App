#include "ApplicationHelper.h"
#include "CoreRegistration.hpp"
#include "ActionManager.hpp"
#include <SlicingPreviewBridge.hpp>
#include <DocumentManager.hpp>
#include <UndoRedoManager.hpp>
#include <QQmlEngine>
#include <QJSEngine>
#include <QFileInfo>
#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <utility>

ApplicationHelper* ApplicationHelper::m_instance = nullptr;

void ApplicationHelper::releaseQmlWindows(QQmlEngine& engine) {
    QList<QPointer<QQuickWindow>> windows;
    for (auto* candidate : QGuiApplication::allWindows()) {
        auto* window = qobject_cast<QQuickWindow*>(candidate);
        if (window && qmlEngine(window) == &engine &&
            QQmlEngine::objectOwnership(window) == QQmlEngine::JavaScriptOwnership &&
            !window->QObject::parent()) {
            windows.append(window);
        }
    }
    // Deleting a window can destroy another candidate through QML ownership.
    // Snapshot every candidate first and observe cascading deletion safely.
    for (const auto& window : windows) {
        if (window) delete window.data();
    }
    // The application event loop has stopped. Window owners such as KDD Group
    // queue visual deletion during destruction; finish it before QML singletons.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

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

void ApplicationHelper::prepareForExit() {
    if (m_shutdownPrepared) return;
    m_shutdownPrepared = true;
    auto* preview = SlicingPreviewBridge::instance();
    preview->beginShutdown();
    cleanupHandlers();
    preview->clearAllDBReferences();
    if (auto* undoManager = UndoRedoManager::instance()) undoManager->clear();
}
void ApplicationHelper::exit(int code) {
    prepareForExit();
    Log::flush();
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
