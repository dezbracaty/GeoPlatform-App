#include <UndoRedoManager.hpp>
#include "DocumentRegistration.hpp"
#include "transdb.h"
#include <DocumentManager.hpp>
#include <QQmlEngine>
#include <QJSEngine>

UndoRedoManager* UndoRedoManager::s_instance = nullptr;

UndoRedoManager* UndoRedoManager::instance() {
    if (!s_instance) {
        s_instance = new UndoRedoManager();
    }
    return s_instance;
}

UndoRedoManager::UndoRedoManager(QObject* parent)
    : QObject(parent) {
    // TransactionManager is now a singleton, no need to store it
    setupConnections();
    updateState();
}

UndoRedoManager::~UndoRedoManager() {
    // 清理
}

void UndoRedoManager::setupConnections() {
    // 设置TransactionManager的回调
    TransactionManager::instance().onUndoStackChanged = [this]() {
        updateState();
    };

    TransactionManager::instance().onRedoStackChanged = [this]() {
        updateState();
    };
}

void UndoRedoManager::updateState() {
    // 更新可撤销状态
    bool newCanUndo = TransactionManager::instance().canUndo();
    if (m_canUndo != newCanUndo) {
        m_canUndo = newCanUndo;
        emit canUndoChanged(m_canUndo);
    }

    // 更新可重做状态
    bool newCanRedo = TransactionManager::instance().canRedo();
    if (m_canRedo != newCanRedo) {
        m_canRedo = newCanRedo;
        emit canRedoChanged(m_canRedo);
    }

    // 更新撤销描述
    QString newUndoDesc = QString::fromStdString(TransactionManager::instance().getNextUndoDescription());
    if (m_undoDescription != newUndoDesc) {
        m_undoDescription = newUndoDesc;
        emit undoDescriptionChanged(m_undoDescription);
    }

    // 更新重做描述
    QString newRedoDesc = QString::fromStdString(TransactionManager::instance().getNextRedoDescription());
    if (m_redoDescription != newRedoDesc) {
        m_redoDescription = newRedoDesc;
        emit redoDescriptionChanged(m_redoDescription);
    }
}

bool UndoRedoManager::canUndo() const {
    return m_canUndo;
}

bool UndoRedoManager::canRedo() const {
    return m_canRedo;
}

QString UndoRedoManager::undoDescription() const {
    return m_undoDescription;
}

QString UndoRedoManager::redoDescription() const {
    return m_redoDescription;
}

void UndoRedoManager::undo() {
    if (!TransactionManager::instance().canUndo()) {
        return;
    }

    QString description = m_undoDescription;

    if (TransactionManager::instance().undo()) {
        // 通知DocumentManager刷新渲染
        DocumentManager::instance()->flushPendingChanges();

        emit undoExecuted(description);
        updateState();
    }
}

void UndoRedoManager::redo() {
    if (!TransactionManager::instance().canRedo()) {
        return;
    }

    QString description = m_redoDescription;

    if (TransactionManager::instance().redo()) {
        // 通知DocumentManager刷新渲染
        DocumentManager::instance()->flushPendingChanges();

        emit redoExecuted(description);
        updateState();
    }
}

void UndoRedoManager::clear() {
    TransactionManager::instance().clear();
    updateState();
}

void UndoRedoManager::testConnection() {
    // Test connection function - no longer needed for debugging
}

REGISTER_DOCUMENT_QML_SINGLETON_CUSTOM(
    UndoRedoManager,
    "UndoRedoManager",
    [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
        Q_UNUSED(engine)
        Q_UNUSED(scriptEngine)
        return DocumentManager::instance()->getUndoRedoManager();
    })
