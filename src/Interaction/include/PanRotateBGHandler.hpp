#pragma once

#include "IActionHandlerBase.hpp"
#include <QMouseEvent>
#include <memory>

// 前向声明
class CameraDB;
class ViewportCoordinateSystem;

/**
 * @brief 平移旋转后台处理器
 *
 * 负责处理相机的实时交互：
 * - 鼠标拖拽旋转相机（轨道模式）
 * - 中键或右键拖拽平移相机
 * - 滚轮缩放
 *
 * 特点：
 * - 后台 Handler，始终活跃
 * - 直接操作 CameraDB，通过 DocumentManager 获取实例
 * - 支持轨道相机模式
 * - 可配置的灵敏度和约束
 */
class PanRotateBGHandler : public IActionHandlerBase {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     */
    explicit PanRotateBGHandler(QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~PanRotateBGHandler() override;

    // === IActionHandlerBase 接口实现 ===

    /**
     * @brief Handler类型 - 后台Handler，始终活跃
     */
    HandlerType getHandlerType() const override {
        return HandlerType::Background;
    }

    /**
     * @brief 进入Handler
     */
    void onEnter(std::shared_ptr<ActionContext> context) override;

    /**
     * @brief 退出Handler
     */
    void onExit() override;

    // === 事件处理方法 ===

    /**
     * @brief 鼠标按下事件
     * 左键：开始轨道旋转
     * 中键或右键：开始平移
     */
    bool onMousePressEvent(QMouseEvent* event) override;

    /**
     * @brief 鼠标移动事件
     * 处理相机的旋转、平移、缩放操作
     */
    bool onMouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief 鼠标释放事件
     * 结束当前操作
     */
    bool onMouseReleaseEvent(QMouseEvent* event) override;

    /**
     * @brief 滚轮事件
     * 缩放相机
     */
    bool onWheelEvent(QWheelEvent* event) override;

    // === 配置方法 ===

    /**
     * @brief 设置旋转灵敏度
     */
    void setRotationSensitivity(float sensitivity) {
        m_rotationSensitivity = sensitivity;
    }

    /**
     * @brief 设置缩放灵敏度
     */
    void setZoomSensitivity(float sensitivity) {
        m_zoomSensitivity = sensitivity;
    }

    /**
     * @brief 设置平移灵敏度
     */
    void setPanSensitivity(float sensitivity) {
        m_panSensitivity = sensitivity;
    }

private:
    // === 操作状态 ===
    enum class InteractionMode {
        NONE,
        ROTATING, // 轨道旋转
        PANNING   // 平移
    };

    InteractionMode m_currentMode = InteractionMode::NONE;

    // === 鼠标状态 ===
    bool m_mousePressed = false; // 是否有鼠标键按下
    Qt::MouseButton m_gestureButton = Qt::NoButton;
    Qt::MouseButton m_pendingButton = Qt::NoButton; // 等待拖拽的按键

    // === 配置参数 ===
    float m_rotationSensitivity = 1.0f; // 视口归一化旋转倍率
    float m_zoomSensitivity = 1.0f;     // 指数缩放倍率
    float m_panSensitivity = 1.0f;      // 投影平移倍率
    std::shared_ptr<CameraDB> m_inputCamera;
    std::uint64_t m_cameraViewId{0};
    struct GestureState;
    std::unique_ptr<GestureState> m_gesture;

    // === 辅助方法 ===

    /**
     * @brief 开始旋转操作
     */
    void startRotation();

    /**
     * @brief 更新旋转
     */
    void updateRotation(const QPointF& currentPos);

    /**
     * @brief 开始平移操作
     */
    void startPanning();

    /**
     * @brief 更新平移
     */
    void updatePanning(const QPointF& currentPos);

    /**
     * @brief 结束当前操作
     */
    void endCurrentOperation();
};
