#pragma once

#include "BridgeBase.hpp"
#include <QObject>
#include <qqmlregistration.h>
#include <QQmlEngine>
#include <QJSEngine>
#include <memory>
#include <vector>

// 前向声明
class ActorDB;
class DebugActorDB;
namespace GPlatform {
    class ToolpathPreviewDB;
}

/**
 * @brief SlicingVisibilityBridge - QML 访问切片可见性控制的桥接类
 *
 * 提供 QML 可访问的切片可见性控制
 * 职责：
 * 1. 控制原始模型的显示/隐藏
 * 2. 控制切片模型的显示/隐藏（包括所有子组件）
 * 3. 控制调试数据的显示/隐藏
 */
class SlicingVisibilityBridge : public bridge::BridgeBase {
    Q_OBJECT

    // 可见性控制属性
    Q_PROPERTY(bool showOriginalModel READ showOriginalModel WRITE setShowOriginalModel NOTIFY showOriginalModelChanged)
    Q_PROPERTY(bool showSlicingModel READ showSlicingModel WRITE setShowSlicingModel NOTIFY showSlicingModelChanged)
    Q_PROPERTY(bool showDebugData READ showDebugData WRITE setShowDebugData NOTIFY showDebugDataChanged)

    QML_ELEMENT
    QML_SINGLETON

public:
    // 单例模式
    static SlicingVisibilityBridge* instance();

    // QML 单例提供函数
    static SlicingVisibilityBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // 可见性控制访问器
    bool showOriginalModel() const { return m_showOriginalModel; }
    void setShowOriginalModel(bool show);

    bool showSlicingModel() const { return m_showSlicingModel; }
    void setShowSlicingModel(bool show);

    bool showDebugData() const { return m_showDebugData; }
    void setShowDebugData(bool show);

    // DB 对象管理方法（由 SlicingHandler 调用）
    void setOriginalModelActor(std::shared_ptr<ActorDB> actor);
    void setToolpathPreviewDB(std::shared_ptr<GPlatform::ToolpathPreviewDB> preview);
    void setDebugActors(const std::vector<std::shared_ptr<DebugActorDB>>& debugActors);

    // 清除所有 DB 引用
    void clearAllDBReferences();

signals:
    void showOriginalModelChanged();
    void showSlicingModelChanged();
    void showDebugDataChanged();

private:
    SlicingVisibilityBridge(QObject* parent = nullptr);
    ~SlicingVisibilityBridge() = default;

    // 禁用拷贝和移动
    SlicingVisibilityBridge(const SlicingVisibilityBridge&) = delete;
    SlicingVisibilityBridge& operator=(const SlicingVisibilityBridge&) = delete;
    SlicingVisibilityBridge(SlicingVisibilityBridge&&) = delete;
    SlicingVisibilityBridge& operator=(SlicingVisibilityBridge&&) = delete;

    // 成员变量
    // 可见性控制状态
    bool m_showOriginalModel = false;  // 默认隐藏原始模型（调试模式）
    bool m_showSlicingModel = false;   // 默认隐藏切片模型（调试模式）
    bool m_showDebugData = true;       // 默认显示调试数据

    // DB 对象引用（由 SlicingHandler 设置）
    std::shared_ptr<ActorDB> m_currentOriginalModelActor;
    std::shared_ptr<GPlatform::ToolpathPreviewDB> m_currentPreview;
    std::vector<std::shared_ptr<DebugActorDB>> m_debugActors;
};
