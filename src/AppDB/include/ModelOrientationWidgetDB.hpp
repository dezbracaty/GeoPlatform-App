#pragma once

#include "WidgetDB.hpp"
#include <SystemTypes.hpp>

/**
 * @brief 模型方向控制器数据库类 - 在模型周围显示三个旋转圆环
 *
 * 功能：
 * - 在指定模型位置显示三个正交圆环
 * - 支持交互式旋转控制
 * - 自动适应模型大小
 *
 * 实现：
 * - 三个独立的圆环分别对应X/Y/Z轴旋转
 * - 圆环大小根据模型包围盒自动调整
 * - 支持高亮和拖动交互
 */
class ModelOrientationWidgetDB : public WidgetDB {
public:
    /**
     * @brief 默认构造函数
     */
    explicit ModelOrientationWidgetDB();

    /**
     * @brief 带参数的构造函数，用于创建时直接设置必要的属性
     * @param linkedActorId 关联的Actor ID
     * @param modelCenter 模型中心位置
     * @param boundsDiagonal 包围盒对角线长度
     */
    explicit ModelOrientationWidgetDB(const DBInstanceID& linkedActorId,
                                     const Vector3& modelCenter,
                                     float boundsDiagonal);

    /**
     * @brief 析构函数
     */
    virtual ~ModelOrientationWidgetDB();

    /**
     * @brief 属性初始化钩子
     */
    void initializeProperties() override;

    /**
     * @brief 初始化子Widget属性的钩子函数
     */
    void initializeSubWidgetProperties() override;

    /**
     * @brief 获取DB类型ID
     */
    TypeID getTypeID() const override {
        return TypeID::MODEL_ORIENTATION_WIDGET_DB;
    }

    // 移除了冗余的getWidgetType()，直接使用getDBType()获取TypeID::MODEL_ORIENTATION_WIDGET_DB

    // === 关联属性 ===

    // 关联的Actor ID
    FIELD_RELATION_REF(ModelOrientationWidgetDB, LinkedActorID)

    // === 尺寸控制属性 ===

    // 是否自动根据模型大小调整圆环大小
    FIELD_VALUE_SIMPLE(ModelOrientationWidgetDB, bool, AutoScale)

    // 手动缩放因子
    FIELD_VALUE_SIMPLE(ModelOrientationWidgetDB, float, ScaleFactor)

    // 圆环粗细（相对于半径的比例）
    FIELD_VALUE_SIMPLE(ModelOrientationWidgetDB, float, RingThickness)

    // 圆环分辨率（分段数）
    FIELD_VALUE_SIMPLE(ModelOrientationWidgetDB, int, RingResolution)

    // === 视觉属性 ===

    // 三个圆环的颜色
    FIELD_VALUE(ModelOrientationWidgetDB, Color, XRingColor)    // X轴圆环（YZ平面）- 红色
    FIELD_VALUE(ModelOrientationWidgetDB, Color, YRingColor)    // Y轴圆环（XZ平面）- 绿色
    FIELD_VALUE(ModelOrientationWidgetDB, Color, ZRingColor)    // Z轴圆环（XY平面）- 蓝色

    // 高亮颜色（鼠标悬停或拖动时）
    FIELD_VALUE(ModelOrientationWidgetDB, Color, HighlightColor)

    // === 模型信息（自动计算） ===

    // 模型中心点（世界坐标）
    FIELD_VALUE(ModelOrientationWidgetDB, Vector3, ModelCenter)

    // 模型包围盒
    FIELD_VALUE(ModelOrientationWidgetDB, Vector3, ModelBoundsMin)
    FIELD_VALUE(ModelOrientationWidgetDB, Vector3, ModelBoundsMax)

    // 包围盒对角线长度（用于计算圆环大小）
    FIELD_VALUE_SIMPLE(ModelOrientationWidgetDB, float, BoundsDiagonal)

    // === 交互状态 ===

    // 当前激活的圆环 (-1=无, 1=X, 2=Y, 3=Z)
    FIELD_VALUE_SIMPLE(ModelOrientationWidgetDB, int, ActiveRing)

    // 是否正在拖动
    FIELD_VALUE_SIMPLE(ModelOrientationWidgetDB, bool, IsDragging)

    // 累积旋转角度（度）
    FIELD_VALUE(ModelOrientationWidgetDB, Vector3, AccumulatedRotation)

    // === 显示控制 ===

    // 是否显示各个圆环
    FIELD_VALUE_SIMPLE(ModelOrientationWidgetDB, bool, ShowXRing)
    FIELD_VALUE_SIMPLE(ModelOrientationWidgetDB, bool, ShowYRing)
    FIELD_VALUE_SIMPLE(ModelOrientationWidgetDB, bool, ShowZRing)

    // === 便捷方法 ===

    /**
     * @brief 根据关联的Actor更新包围盒信息
     */
    void updateFromLinkedActor();

    /**
     * @brief 设置关联的Actor并自动更新
     */
    void setLinkedActor(const DBInstanceID& actorId);

    /**
     * @brief 计算圆环半径
     */
    float calculateRingRadius() const {
        return getBoundsDiagonal() * 0.6f * getScaleFactor();
    }

protected:
    /**
     * @brief 在对象完全初始化并注册到DocumentManager后调用
     */
    void onFullyInitialized() override;

private:
    // 为带参数构造函数保存初始化值
    DBInstanceID m_initLinkedActorId;
    Vector3 m_initModelCenter;
    float m_initBoundsDiagonal = 0.0f;
    bool m_hasInitialValues = false;
};
