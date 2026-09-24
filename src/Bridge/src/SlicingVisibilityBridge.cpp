#include "SlicingVisibilityBridge.hpp"
#include "BridgeRegistration.hpp"
#include "ActorDB.hpp"
#include "ToolpathPreviewDB.hpp"
#include "DebugActorDB.hpp"
#include "TransactionManager.hpp"
#include "Foundation/Log.h"

SlicingVisibilityBridge::SlicingVisibilityBridge(QObject* parent)
    : bridge::BridgeBase(parent) {
    LOG_INFO("SlicingVisibilityBridge created");
}

SlicingVisibilityBridge* SlicingVisibilityBridge::instance() {
    static SlicingVisibilityBridge* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new SlicingVisibilityBridge();
    }
    return s_instance;
}

SlicingVisibilityBridge* SlicingVisibilityBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)

    SlicingVisibilityBridge* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}

void SlicingVisibilityBridge::setShowOriginalModel(bool show) {
    if (m_showOriginalModel == show) return;

    LOG_INFO("原始模型可见性: {}", show ? "显示" : "隐藏");

    TransactionGuard guard(tr("Toggle original model visibility").toStdString());
    if (m_currentOriginalModelActor) {
        m_currentOriginalModelActor->setVisible(show);
    }

    m_showOriginalModel = show;
    emit showOriginalModelChanged();
}

void SlicingVisibilityBridge::setShowSlicingModel(bool show) {
    if (m_showSlicingModel == show) return;

    LOG_INFO("切片模型可见性: {}", show ? "显示" : "隐藏");

    TransactionGuard guard(tr("Toggle sliced model visibility").toStdString());
    if (m_currentPreview) {
        m_currentPreview->setVisible(show);
        LOG_INFO("  toolpath preview visibility synchronized");
    }

    m_showSlicingModel = show;
    emit showSlicingModelChanged();
}

void SlicingVisibilityBridge::setShowDebugData(bool show) {
    if (m_showDebugData == show) return;

    LOG_INFO("调试数据可见性: {}", show ? "显示" : "隐藏");

    TransactionGuard guard(tr("Toggle debug data visibility").toStdString());
    for (auto& debugActor : m_debugActors) {
        if (debugActor) {
            debugActor->setVisible(show);
        }
    }

    m_showDebugData = show;
    emit showDebugDataChanged();
}

void SlicingVisibilityBridge::setOriginalModelActor(std::shared_ptr<ActorDB> actor) {
    m_currentOriginalModelActor = actor;
    if (m_currentOriginalModelActor) {
        TransactionGuard guard(tr("Synchronize original model visibility").toStdString());
        m_currentOriginalModelActor->setVisible(m_showOriginalModel);
    }
}

void SlicingVisibilityBridge::setToolpathPreviewDB(std::shared_ptr<GPlatform::ToolpathPreviewDB> preview) {
    m_currentPreview = preview;
}

void SlicingVisibilityBridge::setDebugActors(
    const std::vector<std::shared_ptr<DebugActorDB>>& debugActors) {
    m_debugActors = debugActors;
}

void SlicingVisibilityBridge::clearAllDBReferences() {
    m_currentOriginalModelActor.reset();
    m_currentPreview.reset();
    m_debugActors.clear();
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    SlicingVisibilityBridge,
    "SlicingVisibilityBridge",
    &SlicingVisibilityBridge::create)
