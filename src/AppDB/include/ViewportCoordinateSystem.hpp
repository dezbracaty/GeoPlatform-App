#pragma once

#include <SystemTypes.hpp>
#include <memory>
#include <vector>
#include <QPointF>

// 前向声明
class CameraDB;

struct WorldRay {
    Vector3 origin;
    Vector3 direction;
};

/**
 * @brief 一次交互计算使用的不可变视口投影状态
 *
 * 相机矩阵、物理视口和 DPR 在创建快照时一次性确定。公开方法与 Qt
 * 一致，统一接收和返回 viewport-local 逻辑坐标；物理像素只是内部
 * 矩阵运算的实现细节。
 */
class ViewportProjectionSnapshot {
public:
    bool isValid() const noexcept;
    Vector4 clipFromWorld(const Vector3& worldPos) const;
    Vector2 logicalSize() const;

    Vector3 worldFromScreen(const QPointF& logicalScreenPos, float depth) const;
    Vector3 screenFromWorld(const Vector3& worldPos) const;
    WorldRay rayFromScreen(const QPointF& logicalScreenPos) const;
    Vector3 worldDeltaFromScreenDrag(const QPointF& startScreen,
                                     const QPointF& currentScreen,
                                     const Vector3& anchorWorld) const;

private:
    friend class ViewportCoordinateSystem;

    std::vector<float> m_viewMatrix;
    std::vector<float> m_projectionMatrix;
    Vector2 m_physicalViewportSize;
    qreal m_devicePixelRatio{1.0};
};

/**
 * @brief 视口坐标系统管理类
 *
 * 职责：
 * - 管理视口的逻辑尺寸和物理尺寸（DPR）
 * - 提供统一的坐标转换接口
 * - 封装 CoordinateTransform 的复杂性
 * - 自动管理相机矩阵的获取
 *
 * 设计理念：
 * - 每个 WindowDB 持有一个 ViewportCoordinateSystem
 * - 支持多窗口场景，每个窗口独立的坐标系统
 * - Handler 不需要关心 DPR、物理像素和矩阵计算细节
 * - Handler 中的 QMouseEvent::pos() 统一视为 viewport-local 逻辑坐标
 */
class ViewportCoordinateSystem {
public:
    /**
     * @brief 构造函数
     */
    ViewportCoordinateSystem();

    /**
     * @brief 析构函数
     */
    ~ViewportCoordinateSystem();

    // ===== 基础属性管理 =====

    /**
     * @brief 设置逻辑视口尺寸（由 ThreadRendererQmlItem 更新）
     * @param size 逻辑像素尺寸
     */
    void setLogicalViewportSize(const Vector2& size);

    /**
     * @brief 设置设备像素比率（由 ThreadRendererQmlItem 更新）
     * @param dpr 设备像素比率（如 Retina 显示器为 2.0）
     */
    void setDevicePixelRatio(qreal dpr);

    /**
     * @brief 设置活动相机（用于世界坐标转换）
     * @param camera 相机的弱引用
     */
    void setActiveCamera(std::weak_ptr<CameraDB> camera);

    // ===== 尺寸查询 =====

    /**
     * @brief 获取逻辑视口尺寸
     * @return 逻辑像素尺寸
     */
    Vector2 getLogicalViewportSize() const;

    /**
     * @brief 捕获当前视口的不可变投影状态
     *
     * 返回的值对象不再访问 CameraDB，可安全地随交互计算请求传递。
     */
    ViewportProjectionSnapshot projectionSnapshot() const;

    // ===== 语义坐标转换（公开输入输出均为逻辑坐标）=====

    /**
     * @brief 从 viewport-local 逻辑坐标生成世界射线
     * @param screenPos Handler 收到的 event->pos()
     * @return 世界射线
     */
    WorldRay rayFromScreen(const QPointF& screenPos) const;

    /**
     * @brief viewport-local 逻辑坐标转世界坐标
     * @param screenPos Handler 收到的 event->pos()
     * @param depth 深度值 [0,1]，0 表示近裁剪面，1 表示远裁剪面
     * @return 世界坐标
     */
    Vector3 worldFromScreen(const QPointF& screenPos, float depth) const;

    /**
     * @brief 世界坐标转 viewport-local 逻辑坐标
     * @param worldPos 世界坐标
     * @return viewport-local 逻辑坐标，z 为深度值
     */
    Vector3 screenFromWorld(const Vector3& worldPos) const;

    /**
     * @brief viewport-local 逻辑坐标拖拽转世界增量
     */
    Vector3 worldDeltaFromScreenDrag(const QPointF& startScreen,
                                     const QPointF& currentScreen,
                                     const Vector3& anchorWorld) const;

    /**
     * @brief 计算输入手势 delta
     *
     * 输入本身已是 Qt 逻辑坐标，手势差值不受 DPR 影响。
     */
    QPointF gestureDelta(const QPointF& startScreen,
                         const QPointF& currentScreen) const;

    // ===== 兼容接口（迁移期保留；新 Handler 代码优先使用上面的语义接口）=====

    /**
     * @brief 屏幕坐标转世界坐标
     * @param screenPos viewport-local 逻辑坐标
     * @param depth 深度值 [0,1]
     * @return 世界坐标
     */
    Vector3 screenToWorld(const QPointF& screenPos, float depth) const;

    /**
     * @brief 屏幕坐标生成射线（用于 Pick）
     * @param screenPos viewport-local 逻辑坐标
     * @param rayOrigin 输出：射线起点（世界坐标）
     * @param rayDirection 输出：射线方向（归一化）
     */
    void screenToRay(const QPointF& screenPos,
                     Vector3& rayOrigin,
                     Vector3& rayDirection) const;

    /**
     * @brief 世界坐标转屏幕坐标
     * @param worldPos 世界坐标
     * @return viewport-local 逻辑坐标（包含深度 z）
     */
    Vector3 worldToScreen(const Vector3& worldPos) const;

    /**
     * @brief 屏幕增量转世界增量（用于 TranslateHandler）
     * @param startScreen 起始 viewport-local 逻辑坐标
     * @param currentScreen 当前 viewport-local 逻辑坐标
     * @param objectWorldPos 对象的世界坐标位置
     * @return 世界空间的位移向量
     */
    Vector3 screenDeltaToWorldDelta(const QPointF& startScreen,
                                   const QPointF& currentScreen,
                                   const Vector3& objectWorldPos) const;

private:
    Vector2 physicalViewportSize() const;
    QPointF toPhysical(const QPointF& logicalPoint) const;

    /**
     * @brief 获取相机的 View 和 Projection 矩阵
     * @param viewMatrix 输出：View 矩阵
     * @param projectionMatrix 输出：Projection 矩阵
     * @return 是否成功获取
     */
    bool getCameraMatrices(std::vector<float>& viewMatrix,
                          std::vector<float>& projectionMatrix) const;

private:
    Vector2 m_logicalViewportSize;           // 逻辑视口尺寸
    qreal m_devicePixelRatio = 1.0;          // 设备像素比率
    std::weak_ptr<CameraDB> m_activeCamera;  // 活动相机的弱引用
};
