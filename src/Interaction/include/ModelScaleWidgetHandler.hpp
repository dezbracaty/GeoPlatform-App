#pragma once

#include "StandardActionHandler.hpp"
#include "PickTypes.hpp"
#include "ActorDB.hpp"
#include <QMouseEvent>
#include <QPoint>
#include <memory>

/**
 * @brief Handler for interactive scaling of models using ModelScaleWidget
 *
 * This handler manages the complete scaling interaction workflow:
 * 1. Creates ModelScaleWidget for the selected model (if not exists)
 * 2. Handles mouse drag on widget handles to scale the linked actor
 * 3. Remains active until user clicks another tool/button
 *
 * Interaction:
 * - Click an Orca-style grabber and drag to scale along an axis or uniformly.
 */
class ModelScaleWidgetHandler : public StandardActionHandler {
    Q_OBJECT
public:
    explicit ModelScaleWidgetHandler(QObject* parent = nullptr);
    virtual ~ModelScaleWidgetHandler();

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
    bool executeAIScale(std::shared_ptr<ActionContext> context);

    // Widget部件ID枚举（与ModelScaleWidgetDBSync/Orca GLGizmoScale保持一致）
    enum PartId {
        NONE = -1,
        HANDLE_X_MIN = 0,
        HANDLE_X_MAX = 1,
        HANDLE_Y_MIN = 2,
        HANDLE_Y_MAX = 3,
        HANDLE_Z_MIN = 4,
        HANDLE_Z_MAX = 5,
        HANDLE_UNIFORM_XMIN_YMIN = 6,
        HANDLE_UNIFORM_XMAX_YMIN = 7,
        HANDLE_UNIFORM_XMAX_YMAX = 8,
        HANDLE_UNIFORM_XMIN_YMAX = 9
    };

    // 拖拽状态
    struct DragState {
        bool isDragging = false;              // 是否正在拖拽
        DBInstanceID widgetId;                // 被拖拽的Widget ID
        DBInstanceID linkedActorId;           // 关联的Actor ID
        int draggedHandle = NONE;             // 被拖拽的手柄ID
        Vector3 initialScale;                 // 拖拽开始时的缩放值
        Vector3 initialPosition;              // 拖拽开始时的位置
        float initialBottomZ = 0.0f;          // Z 缩放过程中保持底面贴住起始高度
        ActorDB::BoundingBox initialLocalBounds;
        ActorDB::BoundingBox initialWorldBounds;

        // Orca GLGizmoScale::StartingData equivalent.
        Vector3 dragPosition;
        Vector3 constraintPosition;
        Vector3 planeCenter;
        Vector3 planeNormal;
        Vector3 boxSize;
        Vector3 grabbers[10];
        Vector3 referenceAxes[3];
        bool ctrlDown = false;
        float currentRatio = 1.0f;

        // 事务管理 - 一次完整的拖拽操作对应一个事务
        bool hasActiveTransaction = false;   // 标记是否有活跃的事务
    } m_dragState;

    // Widget管理
    DBInstanceID m_activeModelId;             // 当前活跃的模型ID
    DBInstanceID m_widgetId;                  // 创建的Widget ID
    DBInstanceID m_interactionViewId{INVALID_DB_ID};

    // 辅助方法

    /**
     * @brief 创建或获取ModelScaleWidget
     */
    bool createOrGetWidget();

    /**
     * @brief 删除ModelScaleWidget
     */
    void deleteModelScaleWidget();

    /**
     * @brief 开始拖拽操作
     */
    bool startDrag(const DBInstanceID& widgetId, int handleId, const QPoint& mousePos);

    /**
     * @brief 更新拖拽（计算并应用缩放）
     */
    void updateDrag(const QPoint& currentPos);

    /**
     * @brief 结束拖拽
     */
    void endDrag();

    float calculateOrcaScaleRatio(const QPoint& currentPos) const;

    bool initializeOrcaStartingData(int handleId);

    /**
     * @brief 应用缩放到关联的Actor
     */
    bool applyScaleToLinkedActor(const Vector3& scale, const Vector3& offset);

    /**
     * @brief 模型缩放后更新Widget显示（包围盒和球体位置）
     * @param newScale 新的缩放值
     */
    void updateWidgetAfterScale(const Vector3& newScale);

    bool calculateScaleDragCandidateBounds(
        const std::shared_ptr<ActorDB>& actor,
        ActorDB::BoundingBox* worldBounds) const;

    /**
     * @brief 获取当前的Pick结果
     */

    /**
     * @brief Z轴缩放后执行贴地操作，确保模型底部贴合平台
     */
    bool performSnapToGround();

};
