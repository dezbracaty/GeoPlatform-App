#pragma once

#include "StandardActionHandler.hpp"
#include "PickTypes.hpp"
#include "Transform.hpp"
#include <QMouseEvent>
#include <QPoint>
#include <memory>

/**
 * @brief Handler for interactive rotation of models using ModelOrientationWidget
 *
 * This handler manages the complete rotation interaction workflow:
 * 1. Creates ModelOrientationWidget for the selected model (if not exists)
 * 2. Handles mouse drag on axis grabbers to rotate the linked actor
 * 3. Remains active until user clicks another tool/button
 *
 * Interaction:
 * - Click an axis grabber and drag to rotate around that axis
 * - X grabber (red): Rotate around X axis
 * - Y grabber (green): Rotate around Y axis
 * - Z grabber (blue): Rotate around Z axis
 */
class ModelOrientationWidgetHandler : public StandardActionHandler {
    Q_OBJECT
public:
    explicit ModelOrientationWidgetHandler(QObject* parent = nullptr);
    virtual ~ModelOrientationWidgetHandler();

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
    bool executeAIRotate(std::shared_ptr<ActionContext> context);

    // Widget部件ID枚举（与ModelOrientationWidgetDBSync保持一致）
    enum PartId {
        NONE = -1,
        RING_X = 1,  // X轴旋转抓手
        RING_Y = 2,  // Y轴旋转抓手
        RING_Z = 3   // Z轴旋转抓手
    };

    struct RotationSample {
        float angleDeg = 0.0f;
        float mouseRadius = 0.0f;
        bool inCoarseSnapRing = false;
        bool inFineSnapRing = false;
    };

    // 拖拽状态
    struct DragState {
        bool isDragging = false;              // 是否正在拖拽
        DBInstanceID widgetId;                // 被拖拽的Widget ID
        DBInstanceID linkedActorId;           // 关联的Actor ID
        int draggedPart = NONE;               // 被拖拽的部件ID
        Transform::Matrix4 initialTransform = Transform::Matrix4::Identity();
        Vector3 pivot;
        Vector3 worldAxis;
        float fallbackAngleRad = 0.0f;
        float currentAngleDeg = 0.0f;
        float radius = 1.0f;

        // 事务管理 - 一次完整的拖拽操作对应一个事务
        bool hasActiveTransaction = false;  // 标记是否有活跃的事务
    } m_dragState;

    // Widget管理
    DBInstanceID m_activeModelId;             // 当前活跃的模型ID
    DBInstanceID m_widgetId;                  // 创建的Widget ID
    DBInstanceID m_interactionViewId{INVALID_DB_ID};

    // 辅助方法

    /**
     * @brief 创建或获取ModelOrientationWidget
     */
    bool createOrGetWidget();

    /**
     * @brief 删除ModelOrientationWidget
     */
    void deleteModelOrientationWidget();

    /**
     * @brief 开始拖拽操作
     */
    bool startDrag(const DBInstanceID& widgetId, int partId, const QPoint& mousePos);

    /**
     * @brief 更新拖拽（计算并应用旋转）
     */
    void updateDrag(const QPoint& currentPos);

    /**
     * @brief 结束拖拽
     */
    void endDrag();

    /**
     * @brief 执行贴地操作
     */
    bool performSnapToGround();

    /**
     * @brief 计算旋转角度
     */
    RotationSample calculateRotationSample(const QPoint& currentPos) const;

    /**
     * @brief 将屏幕坐标投影到3D平面
     */
    float mouseAngleInRotationPlane(const QPoint& screenPos, float* radiusOut = nullptr) const;

    /**
     * @brief 基于拖拽开始时的变换应用绝对旋转，避免逐帧增量误差
     */
    bool applyRotationToLinkedActor(float angleDeg);

    /**
     * @brief 同步旋转控件的交互状态
     */
    void updateWidgetAfterRotation(float angleDeg);

    /**
     * @brief 获取当前的Pick结果
     */
};
