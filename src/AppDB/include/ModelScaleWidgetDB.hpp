#pragma once

#include "WidgetDB.hpp"
#include <SystemTypes.hpp>
#include <Transform.hpp>

/**
 * @brief 模型缩放控制器数据库类 - 在模型周围显示缩放手柄
 *
 * 功能：
 * - 在模型局部包围盒上显示 Orca 风格的 10 个缩放控制点
 * - 支持交互式缩放控制
 * - 自动适应模型大小
 *
 * 实现：
 * - X/Y/Z 两端轴向控制点以及底面四角等比缩放控制点
 * - 底部 Z 控制点保留编号但不显示，防止模型缩放进平台
 * - 手柄保持固定屏幕尺寸
 * - 支持高亮和拖动交互
 */
class ModelScaleWidgetDB : public WidgetDB {
public:
    /**
     * @brief 默认构造函数
     */
    explicit ModelScaleWidgetDB();

    /**
     * @brief 带参数的构造函数，用于创建时直接设置必要的属性
     * @param linkedActorId 关联的Actor ID
     * @param modelBoundsMin 模型包围盒最小点
     * @param modelBoundsMax 模型包围盒最大点
     */
    explicit ModelScaleWidgetDB(const DBInstanceID& linkedActorId,
                               const Vector3& modelBoundsMin,
                               const Vector3& modelBoundsMax);

    /**
     * @brief 析构函数
     */
    virtual ~ModelScaleWidgetDB();

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
        return TypeID::MODEL_SCALE_WIDGET_DB;
    }

    // 移除了冗余的getWidgetType()，直接使用getDBType()获取TypeID::MODEL_SCALE_WIDGET_DB

    // === 关联属性 ===

    // 关联的Actor ID
    FIELD_RELATION_REF(ModelScaleWidgetDB, LinkedActorID)

    // === 视觉属性 ===

    // 手柄颜色
    FIELD_VALUE(ModelScaleWidgetDB, Color, XHandleColor)    // X轴手柄 - 红色
    FIELD_VALUE(ModelScaleWidgetDB, Color, YHandleColor)    // Y轴手柄 - 绿色
    FIELD_VALUE(ModelScaleWidgetDB, Color, ZHandleColor)    // Z轴手柄 - 蓝色
    FIELD_VALUE(ModelScaleWidgetDB, Color, UniformHandleColor) // 统一缩放手柄 - Orca cyan

    // === 模型信息 ===

    // 模型中心点（世界坐标）
    FIELD_VALUE(ModelScaleWidgetDB, Vector3, ModelCenter)

    // 模型包围盒
    FIELD_VALUE(ModelScaleWidgetDB, Vector3, ModelBoundsMin)
    FIELD_VALUE(ModelScaleWidgetDB, Vector3, ModelBoundsMax)

    // Orca current reference system data: local box plus transform into world coordinates.
    FIELD_VALUE(ModelScaleWidgetDB, Vector3, ModelLocalBoundsMin)
    FIELD_VALUE(ModelScaleWidgetDB, Vector3, ModelLocalBoundsMax)
    FIELD_VALUE(ModelScaleWidgetDB, Transform, ModelReferenceTransform)

    // === 交互状态 ===

    // 当前激活的手柄（-1=无，0..9 对应 Orca grabber 编号）
    FIELD_VALUE_SIMPLE(ModelScaleWidgetDB, int, ActiveHandle)

    // Ctrl 缩放时被约束的对侧手柄 (-1=无)，用于匹配 Orca GLGizmoScale 的 CONSTRAINED_COLOR。
    FIELD_VALUE_SIMPLE(ModelScaleWidgetDB, int, ConstrainedHandle)

    // 是否正在拖动
    FIELD_VALUE_SIMPLE(ModelScaleWidgetDB, bool, IsDragging)

    // === 显示控制 ===

    // 是否显示各个手柄
    FIELD_VALUE_SIMPLE(ModelScaleWidgetDB, bool, ShowXHandle)
    FIELD_VALUE_SIMPLE(ModelScaleWidgetDB, bool, ShowYHandle)
    FIELD_VALUE_SIMPLE(ModelScaleWidgetDB, bool, ShowZHandle)
    FIELD_VALUE_SIMPLE(ModelScaleWidgetDB, bool, ShowUniformHandle)

    // === 便捷方法 ===

    /**
     * @brief 根据关联的Actor更新包围盒信息
     */
    void updateFromLinkedActor();

    /**
     * @brief 设置关联的Actor并自动更新
     */
    void setLinkedActor(const DBInstanceID& actorId);

protected:
    /**
     * @brief 在对象完全初始化并注册到DocumentManager后调用
     */
    void onFullyInitialized() override;

private:
    // 为带参数构造函数保存初始化值
    DBInstanceID m_initLinkedActorId;
    Vector3 m_initModelBoundsMin;
    Vector3 m_initModelBoundsMax;
    bool m_hasInitialValues = false;
};
