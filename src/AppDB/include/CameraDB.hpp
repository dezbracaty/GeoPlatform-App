#pragma once

#include <AutoRegisterDB.hpp>
#include <ChangeTypes.hpp>
#include <SystemTypes.hpp>
#include <chrono>
#include <memory>
#include <cmath>
#include <cstdint>
#include <mutex>

/**
 * @brief 相机数据库类 - 管理3D相机的所有参数
 *
 * 负责管理：
 * - 相机位置、目标点、上向量
 * - 投影参数（透视/正交、视野、近远裁剪面）
 * - 视口参数
 * - 轨道相机模式（围绕目标点旋转）
 *
 * 设计理念：
 * - 所有属性使用 FIELD_VALUE 宏管理，支持事务和撤销/重做
 * - 支持透视和正交两种投影模式
 * - 提供轨道相机接口（围绕目标点旋转）
 * - 自动计算相机方向向量
 * - 集成到 GPlatform 的变更通知系统
 */
class CameraDB : public AutoRegisterDB {
public:
    /**
     * @brief 投影类型
     */
    enum class ProjectionType {
        PERSPECTIVE = 0, // 透视投影
        ORTHOGRAPHIC = 1 // 正交投影
    };

    /**
     * @brief 相机视图预设
     */
    enum class ViewPreset {
        CUSTOM = 0,
        FRONT,    // 前视图
        BACK,     // 后视图
        LEFT,     // 左视图
        RIGHT,    // 右视图
        TOP,      // 顶视图
        BOTTOM,   // 底视图
        ISOMETRIC // 等轴测视图
    };

    /**
     * @brief 轨道相机参数结构
     */
    struct OrbitParameters {
        Vector3 center{0, 0, 0};      // 轨道中心点
        float horizontalAngle = 0.0f; // 水平角度（弧度）
        float verticalAngle = 0.0f;   // 垂直角度（弧度）
        float radius = 10.0f;         // 距离中心的半径

        bool operator!=(const OrbitParameters& other) const {
            return center != other.center ||
                   horizontalAngle != other.horizontalAngle ||
                   verticalAngle != other.verticalAngle ||
                   radius != other.radius;
        }
    };

    // Immutable-by-copy state published to renderer threads after a complete
    // camera update. Render code must use this snapshot instead of reading the
    // FIELD_VALUE properties individually while the GUI thread is dragging.
    struct RenderState {
        Vector3 position{0, 0, 0};
        Vector3 target{0, 0, 0};
        Vector3 upVector{0, 0, 1};
        bool orbitMode = true;
        OrbitParameters orbit;
        int projectionType = static_cast<int>(ProjectionType::PERSPECTIVE);
        float fieldOfView = 45.0f;
        float orthographicSize = 10.0f;
        float nearClippingPlane = 0.1f;
        float farClippingPlane = 10000.0f;
        std::uint64_t revision = 0;
    };

public:
    /**
     * @brief 构造函数
     */
    explicit CameraDB();

    /**
     * @brief 析构函数
     */
    virtual ~CameraDB();

protected:
    // === 实现 AutoRegisterDB 的钩子函数 ===

    /**
     * @brief 初始化 CameraDB 特有的属性
     *
     * 由 AutoRegisterDB 的初始化流程自动调用
     */
    void initializeProperties() override;

    /**
     * @brief 属性初始化后的钩子
     *
     * 用于根据轨道参数更新相机位置等二次初始化
     */
    void afterPropertiesInitialized() override;

public:
    /**
     * @brief 获取DB类型ID
     */
    TypeID getTypeID() const override {
        return TypeID::CAMERA_DB;
    }

    // === 基本相机属性（使用新的无默认值宏） ===

    // 相机位置和方向
    FIELD_VALUE(CameraDB, Vector3, Position) // 相机位置
    FIELD_VALUE(CameraDB, Vector3, Target)   // 目标点
    FIELD_VALUE(CameraDB, Vector3, UpVector) // 上向量

    // 投影参数
    FIELD_VALUE_SIMPLE(CameraDB, int, ProjectionType)      // 存储为int，通过辅助方法转换
    FIELD_VALUE_SIMPLE(CameraDB, float, FieldOfView)       // 透视投影视野角度（度）
    FIELD_VALUE_SIMPLE(CameraDB, float, OrthographicSize)  // 正交投影大小
    FIELD_VALUE_SIMPLE(CameraDB, float, AspectRatio)       // 宽高比
    FIELD_VALUE_SIMPLE(CameraDB, float, NearClippingPlane) // 近裁剪面
    FIELD_VALUE_SIMPLE(CameraDB, float, FarClippingPlane)  // 远裁剪面

    // 轨道相机模式
    FIELD_VALUE_SIMPLE(CameraDB, bool, OrbitMode)        // 是否启用轨道模式
    FIELD_VALUE(CameraDB, Vector3, OrbitCenter)          // 轨道中心
    FIELD_VALUE_SIMPLE(CameraDB, float, OrbitRadius)     // 轨道半径
    FIELD_VALUE_SIMPLE(CameraDB, float, HorizontalAngle) // 水平角度（弧度）
    FIELD_VALUE_SIMPLE(CameraDB, float, VerticalAngle)   // 垂直角度（弧度）

    // 相机状态
    FIELD_VALUE_SIMPLE(CameraDB, bool, Enabled) // 是否启用
    FIELD_VALUE(CameraDB, std::string, Name)    // 相机名称

    // === 投影类型辅助方法 ===

    ProjectionType getProjectionTypeEnum() const {
        return static_cast<ProjectionType>(getProjectionType());
    }

    void setProjectionTypeEnum(ProjectionType type) {
        setProjectionType(static_cast<int>(type));
    }

    bool isPerspective() const {
        return getProjectionTypeEnum() == ProjectionType::PERSPECTIVE;
    }

    bool isOrthographic() const {
        return getProjectionTypeEnum() == ProjectionType::ORTHOGRAPHIC;
    }

    // === 轨道相机接口 ===

    /**
     * @brief 围绕目标点旋转
     * @param deltaHorizontal 水平角度增量（弧度）
     * @param deltaVertical 垂直角度增量（弧度）
     */
    void rotate(float deltaHorizontal, float deltaVertical);

    /**
     * @brief 缩放（改变轨道半径）
     * @param scaleFactor 缩放因子（>1放大，<1缩小）
     */
    void zoom(float scaleFactor);

    /**
     * @brief 平移轨道中心
     * @param delta 平移向量
     */
    void pan(const Vector3& delta);

    /**
     * @brief 从当前位置同步轨道参数
     * 根据当前的 position 和 target 反向计算轨道参数
     */
    void syncOrbitParametersFromPosition();

    /**
     * @brief 更新轨道参数（批量更新）
     */
    void updateOrbitParameters(const Vector3& center,
                               float horizontalAngle,
                               float verticalAngle,
                               float radius);

    /**
     * @brief 获取当前轨道参数
     */
    OrbitParameters getCurrentOrbitParameters() const;

    /**
     * @brief Return one coherent camera state for cross-thread rendering.
     */
    RenderState getRenderStateSnapshot() const;

    /**
     * @brief 从轨道参数更新相机位置
     */
    void updatePositionFromOrbit();

    // === 相机计算方法 ===

    /**
     * @brief 获取相机前向量
     */
    Vector3 getForwardVector() const;

    /**
     * @brief 获取相机右向量
     */
    Vector3 getRightVector() const;

    /**
     * @brief 计算视图矩阵（4x4，行主序）
     */
    std::vector<float> calculateViewMatrix() const;

    /**
     * @brief 计算投影矩阵（4x4，行主序）
     */
    std::vector<float> calculateProjectionMatrix() const;
    std::vector<float> calculateProjectionMatrix(float aspectRatio) const;

    // === 便捷访问方法 ===

    /**
     * @brief 获取当前半径（用于轨道模式）
     */
    float getCurrentRadius() const;

    /**
     * @brief 计算到目标的距离
     */
    float getDistanceToTarget() const;

    /**
     * @brief 重置到默认状态
     */
    void resetToDefault();

    // === 动态裁剪面计算 ===

    /**
     * @brief 根据场景包围盒和相机距离更新裁剪面
     *
     * Near Plane: 分段固定策略，避免深度精度跳变
     * Far Plane: 动态计算，基于场景包围盒和相机距离
     *
     * @param sceneBBoxDiagonal 场景包围盒对角线长度
     */
    void updateClippingPlanesFromScene(float sceneBBoxDiagonal);

    /**
     * @brief 根据相机距离计算 Near Plane（分段策略）
     * @param cameraDistance 相机到目标的距离
     * @return 计算得到的 Near Plane 值
     */
    static float calculateNearPlane(float cameraDistance);

    /**
     * @brief 根据场景和相机距离计算 Far Plane（动态策略）
     * @param sceneBBoxDiagonal 场景包围盒对角线
     * @param cameraDistance 相机到目标的距离
     * @return 计算得到的 Far Plane 值
     */
    static float calculateFarPlane(float sceneBBoxDiagonal, float cameraDistance);

protected:
    /**
     * @brief 通知相机参数变化
     * @param propertyName 变化的属性名
     */
    void notifyCameraChange(const std::string& propertyName);

    /**
     * @brief 通知轨道参数变化
     */
    void notifyOrbitChange(const std::string& propertyName);

private:
    void beginCameraBatch();
    void endCameraBatch();
    void handlePropertyChanged(const std::string& propertyName, bool updateClippingPlanes);
    void updateClippingPlanesFromCurrentScene(bool force = false);
    void notifyCameraStateChanged();
    void publishRenderState();

    /**
     * @brief 归一化向量
     */
    Vector3 normalize(const Vector3& v) const;

    /**
     * @brief 向量叉积
     */
    Vector3 cross(const Vector3& a, const Vector3& b) const;

    /**
     * @brief 向量点积
     */
    float dot(const Vector3& a, const Vector3& b) const;

    /**
     * @brief 向量长度
     */
    float length(const Vector3& v) const;

    /**
     * @brief 角度转弧度
     */
    float degreesToRadians(float degrees) const {
        return degrees * M_PI / 180.0f;
    }

    /**
     * @brief 弧度转角度
     */
    float radiansToDegrees(float radians) const {
        return radians * 180.0f / M_PI;
    }

    int m_cameraBatchDepth = 0;
    bool m_cameraBatchDirty = false;
    bool m_cameraBatchNeedsClipping = false;
    std::chrono::steady_clock::time_point m_lastClippingUpdate{};
    mutable std::mutex m_renderStateMutex;
    RenderState m_renderState;
};

// Qt元对象系统支持
Q_DECLARE_METATYPE(CameraDB::ProjectionType)
Q_DECLARE_METATYPE(CameraDB::ViewPreset)
Q_DECLARE_METATYPE(CameraDB::OrbitParameters)
