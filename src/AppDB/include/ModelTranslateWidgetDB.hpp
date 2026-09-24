#pragma once

#include "WidgetDB.hpp"
#include <SystemTypes.hpp>

/**
 * @brief 模型位移控制器数据库类 - 在模型周围显示位移控制器
 *
 * 功能：
 * - 在指定模型包围盒外显示三轴向 Orca 风格 grabber
 * - 支持沿X/Y/Z轴向的精确位移操作
 * - 自动适应模型大小
 *
 * 实现：
 * - 三个轴向 grabber 分别对应 X/Y/Z 轴位移
 * - 从包围盒中心到 grabber 显示轴向连接线
 * - grabber 位置根据模型包围盒自动调整
 * - 支持高亮和拖动交互
 * - 提供实时视觉反馈
 */
class ModelTranslateWidgetDB : public WidgetDB {
public:
    /**
     * @brief 位移模式枚举
     */
    enum class TranslateMode : int {
        None = 0,           // 无活动模式
        XAxis = 1,          // X轴位移
        YAxis = 2,          // Y轴位移
        ZAxis = 3           // Z轴位移
    };

    /**
     * @brief 默认构造函数
     */
    explicit ModelTranslateWidgetDB();

    /**
     * @brief 带参数的构造函数，用于创建时直接设置必要的属性
     * @param linkedActorId 关联的Actor ID
     * @param actorPosition Actor位置
     */
    explicit ModelTranslateWidgetDB(const DBInstanceID& linkedActorId,
                                   const Vector3& actorPosition);

    /**
     * @brief 析构函数
     */
    virtual ~ModelTranslateWidgetDB();

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
        return TypeID::MODEL_TRANSLATE_WIDGET_DB;
    }

    // 移除了冗余的getWidgetType()，直接使用getDBType()获取TypeID::MODEL_TRANSLATE_WIDGET_DB

    // === 关联属性 ===

    // 关联的Actor ID
    FIELD_RELATION_REF(ModelTranslateWidgetDB, LinkedActorID)

    // === 位移控制属性 ===

    // Widget中心位置（世界坐标）
    FIELD_VALUE(ModelTranslateWidgetDB, Vector3, WidgetCenter)

    // === 颜色属性 ===

    // 轴向箭头颜色
    FIELD_VALUE(ModelTranslateWidgetDB, Color, XAxisColor)    // X轴箭头 - 红色
    FIELD_VALUE(ModelTranslateWidgetDB, Color, YAxisColor)    // Y轴箭头 - 绿色
    FIELD_VALUE(ModelTranslateWidgetDB, Color, ZAxisColor)    // Z轴箭头 - 蓝色

    // === 组件位置信息 ===

    // 三个轴向箭头的世界坐标位置
    FIELD_VALUE(ModelTranslateWidgetDB, Vector3, XArrowPosition)   // X轴箭头位置
    FIELD_VALUE(ModelTranslateWidgetDB, Vector3, YArrowPosition)   // Y轴箭头位置
    FIELD_VALUE(ModelTranslateWidgetDB, Vector3, ZArrowPosition)   // Z轴箭头位置

    // Orca move gizmo 使用选择包围盒来放置轴向 grabber。
    FIELD_VALUE(ModelTranslateWidgetDB, Vector3, ModelBoundsMin)
    FIELD_VALUE(ModelTranslateWidgetDB, Vector3, ModelBoundsMax)

    // === 显示控制 ===

    // 是否显示各个组件
    FIELD_VALUE_SIMPLE(ModelTranslateWidgetDB, bool, ShowXArrow)
    FIELD_VALUE_SIMPLE(ModelTranslateWidgetDB, bool, ShowYArrow)
    FIELD_VALUE_SIMPLE(ModelTranslateWidgetDB, bool, ShowZArrow)

    // === 便捷方法 ===

    /**
     * @brief 根据关联的Actor更新Widget中心位置
     */
    void updateFromLinkedActor();

    /**
     * @brief 设置关联的Actor并自动更新
     */
    void setLinkedActor(const DBInstanceID& actorId);

    /**
     * @brief 更新所有手柄的位置
     */
    void updateHandlePositions();

protected:
    /**
     * @brief 在对象完全初始化并注册到DocumentManager后调用
     */
    void onFullyInitialized() override;

private:
    // 为带参数构造函数保存初始化值
    DBInstanceID m_initLinkedActorId;
    Vector3 m_initWidgetCenter;
    bool m_hasInitialValues = false;
};
