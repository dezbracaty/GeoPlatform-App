#pragma once

#include <AutoRegisterDB.hpp>
#include <ChangeTypes.hpp>
#include <SystemTypes.hpp>
#include <memory>

/**
 * @brief Widget数据库基类 - 管理UI辅助显示元素
 *
 * Widget是特殊的可视化元素，包括：
 * - 方向指示器（坐标轴）
 * - 测量工具
 * - 标注
 * - 网格
 *
 * 设计理念：
 * - Widget本质是特殊的Actor组合
 * - 不依赖VTK Widget系统
 * - 所有状态通过DB管理，支持撤销/重做
 * - DBRenderAdapter负责转换为VTK对象
 */
class WidgetDB : public AutoRegisterDB {
public:
    // 移除了冗余的WidgetType枚举，直接使用SystemTypes.hpp中的TypeID

    /**
     * @brief 构造函数
     */
    explicit WidgetDB();

    /**
     * @brief 析构函数
     */
    virtual ~WidgetDB();

    /**
     * @brief 初始化属性钩子函数
     */
    void initializeProperties() override;

    /**
     * @brief 属性初始化完成后的钩子函数
     */
    void afterPropertiesInitialized() override;

protected:
    // === 子类钩子函数 - 子类可以重写这些方法 ===

    /**
     * @brief 初始化子Widget属性的钩子函数
     */
    virtual void initializeSubWidgetProperties() {
    }

    /**
     * @brief 子Widget属性初始化完成后的钩子函数
     */
    virtual void afterSubWidgetPropertiesInitialized() {
    }

    /**
     * @brief 获取DB类型ID
     */
    TypeID getTypeID() const override {
        return TypeID::WIDGET_DB;
    }

    // 移除了冗余的getWidgetType()方法，直接使用getDBType()获取类型

    /**
     * @brief 重写setPropertyImpl以防止Widget属性变化被记录到事务系统
     *
     * Widget是临时的UI元素，其属性变化不应该被记录到事务系统中，
     * 因为这会导致在undo/redo时尝试操作已经不存在的Widget对象。
     */
    void setPropertyImpl(const trans::Prop& prop, const std::any& value) override;

    // === 基础属性（使用新的无默认值宏） ===

    // 可见性
    FIELD_VALUE_SIMPLE(WidgetDB, bool, Visible)

    // 渲染层级（0=主场景，1+=overlay层）
    FIELD_VALUE_SIMPLE(WidgetDB, int, RenderLayer)

    // 不透明度
    FIELD_VALUE_SIMPLE(WidgetDB, float, Opacity)

    // 交互性
    FIELD_VALUE_SIMPLE(WidgetDB, bool, Interactive)

    // 优先级（用于事件处理顺序）
    FIELD_VALUE_SIMPLE(WidgetDB, float, Priority)

    // 名称
    FIELD_VALUE(WidgetDB, std::string, Name)
    FIELD_VALUE_SIMPLE(WidgetDB, int, HoveredPart)

protected:
    /**
     * @brief 通知Widget变化
     * @param propertyName 变化的属性名
     */
    void notifyWidgetChange(const std::string& propertyName);
};

// 移除了Qt元对象系统对WidgetType的支持，因为已删除WidgetType枚举
// 如需要类型信息，直接使用TypeID枚举
