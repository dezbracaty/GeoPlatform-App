#pragma once

#include "StandardActionHandler.hpp"
#include "PickTypes.hpp"
#include "ActorDB.hpp"
#include <ModelTranslateWidgetDB.hpp>
#include <DocumentManager.hpp>
#include <QMouseEvent>
#include <QPoint>
#include <memory>
#include <optional>

/**
 * @brief Handler for interactive translation of models using ModelTranslateWidget
 *
 * This handler manages the complete translation interaction workflow:
 * 1. Creates ModelTranslateWidget for the selected model (if not exists)
 * 2. Handles mouse drag on widget arrows to translate the linked actor
 * 3. Remains active across selection changes until the tool is explicitly exited
 *
 * Interaction modes:
 * - X axis arrow (red): Constrained translation along X axis
 * - Y axis arrow (green): Constrained translation along Y axis
 * - Z axis arrow (blue): Constrained translation along Z axis
 */
class ModelTranslateWidgetHandler : public StandardActionHandler {
    Q_OBJECT
public:
    explicit ModelTranslateWidgetHandler(QObject* parent = nullptr);
    virtual ~ModelTranslateWidgetHandler();

    // 标记为持久的Handler，不会被ActionManager自动清理
    bool isPersistent() const override { return true; }
    bool supportsEnvironment(const QString& environment) const override {
        return environment == QStringLiteral("normal") ||
               environment == QStringLiteral("editing");
    }

    // Handler生命周期
    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onEnterForAI(std::shared_ptr<ActionContext> context) override;
    void onExit() override;
    void onSuspend() override { onExit(); }

protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;

    // 事件处理
    bool onMousePressEvent(QMouseEvent* event) override;
    bool onMouseMoveEvent(QMouseEvent* event) override;
    bool onMouseReleaseEvent(QMouseEvent* event) override;

private:
    bool executeAITranslation(std::shared_ptr<ActionContext> context);

    // Widget部件ID枚举（与ModelTranslateWidgetDBSync保持一致）
    enum PartId {
        NONE = -1,
        X_AXIS = 1,        // X轴箭头（红色）
        Y_AXIS = 2,        // Y轴箭头（绿色）
        Z_AXIS = 3         // Z轴箭头（蓝色）
        // CENTER_SPHERE 已移除 - 简化设计
    };

    // 拖拽状态
    struct DragState {
        bool isDragging = false;              // 是否正在拖拽
        DBInstanceID widgetId;                // 被拖拽的Widget ID
        DBInstanceID linkedActorId;           // 关联的Actor ID
        ModelTranslateWidgetDB::TranslateMode translateMode = ModelTranslateWidgetDB::TranslateMode::None; // 当前拖拽模式
        int draggedPart = NONE;               // 被拖拽的部件ID
        Vector3 initialActorPosition;         // 拖拽开始时 Actor 的位置
        Vector3 initialBoundsMin;             // 拖拽开始时模型世界包围盒最小点
        Vector3 initialBoundsMax;             // 拖拽开始时模型世界包围盒最大点
        Vector3 startingBoxCenter;            // 拖拽开始时包围盒中心
        Vector3 startingGrabberPosition;      // 拖拽开始时 grabber 位置
        float startProjection = 0.0f;          // 鼠标按下时的轴参数基线
        Vector3 currentDisplacement;          // 当前单轴位移

        // 事务管理 - 一次完整的拖拽操作对应一个事务
        bool hasActiveTransaction = false;   // 标记是否有活跃的事务
    } m_dragState;

    // Widget管理
    DBInstanceID m_activeModelId;             // 当前活跃的模型ID
    DBInstanceID m_widgetId;                  // 创建的Widget ID
    DBInstanceID m_interactionViewId{INVALID_DB_ID};

    // Cursor 状态：只在 hover/drag 到 move cone 时覆盖鼠标样式
    bool m_translateCursorApplied = false;
    Qt::CursorShape m_translateCursorShape = Qt::ArrowCursor;

    // 辅助方法

    /**
     * @brief 创建或获取ModelTranslateWidget
     */
    bool createOrGetWidget();

    /**
     * @brief 删除ModelTranslateWidget
     */
    void deleteModelTranslateWidget();

    /**
     * @brief 开始拖拽操作
     */
    bool startDrag(const DBInstanceID& widgetId, int partId, const QPoint& mousePos);

    /**
     * @brief 更新拖拽（计算并应用位移）
     */
    void updateDrag(const QPoint& currentPos);

    /**
     * @brief 结束拖拽
     */
    void endDrag();

    /**
     * @brief 当前 Pick 是否命中本 translate widget 的 cone grabber
     */
    bool isInteractiveConePick(const PickResult& pickResult) const;

    /**
     * @brief 根据当前 hover/drag 状态更新鼠标指针
     */
    void updateHoverCursor(const PickResult& pickResult);
    void applyTranslateCursor(Qt::CursorShape shape);
    void clearTranslateCursor();

    /**
     * @brief 计算移动轴与鼠标射线最近点对应的轴参数
     */
    std::optional<float> calculateAxisParameterFromMouseRay(const QPoint& currentScreenPos) const;

    /**
     * @brief 根据当前拖拽模式把标量投影还原为 XYZ 位移
     */
    Vector3 displacementFromProjection(float projection) const;

    /**
     * @brief 应用位移到关联的Actor
     */
    void applyTranslationToLinkedActor(const Vector3& newPosition);

    /**
     * @brief 模型位移后更新Widget显示
     * @param newPosition 新的位置
     */
    void updateWidgetAfterTranslation(const Vector3& newPosition);

    /**
     * @brief 获取当前的Pick结果
     */
    PickResult getCurrentPickResult() const;

    /**
     * @brief 根据拖拽部件确定TranslateMode
     * @param partId 部件ID
     * @return 对应的TranslateMode
     */
    ModelTranslateWidgetDB::TranslateMode getTranslateModeFromPart(int partId);

    // === 数学辅助方法 ===

    /**
     * @brief 计算两个向量的点积
     */
    float dotProduct(const Vector3& a, const Vector3& b) const;

    /**
     * @brief 向量标准化
     */
    Vector3 normalize(const Vector3& v) const;

    /**
     * @brief 计算向量长度
     */
    float length(const Vector3& v) const;

};
