#pragma once

#include "ActorDB.hpp"

/**
 * @brief ConeDB - 圆锥数据库类
 *
 * 对应vtkConeSource的所有属性
 */
class ConeDB : public ActorDB {
public:
    explicit ConeDB();
    virtual ~ConeDB();

    // === 几何属性（使用几何属性宏 - 自动调用markGeometryChanged） ===
    FIELD_GEOMETRY_VALUE(ConeDB, Vector3, Center)
    FIELD_GEOMETRY_VALUE_SIMPLE(ConeDB, float, Height)
    FIELD_GEOMETRY_VALUE_SIMPLE(ConeDB, float, Radius)
    FIELD_GEOMETRY_VALUE_SIMPLE(ConeDB, int, Resolution)
    FIELD_GEOMETRY_VALUE_SIMPLE(ConeDB, int, Direction) // 0=X轴, 1=Y轴, 2=Z轴
    FIELD_GEOMETRY_VALUE_SIMPLE(ConeDB, bool, Capping)

    // === 实现ActorDB接口 ===
    TypeID getTypeID() const override {
        return TypeID::CONE_DB;
    }

    BoundingBox localBounds() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

protected:
    // === 实现 ActorDB 的钩子函数 ===

    /**
     * @brief 初始化 ConeDB 特有的属性
     *
     * 由 ActorDB 的初始化流程自动调用
     */
    void initializeSubActorProperties() override;
    
    /**
     * @brief 在属性初始化完成后调用
     *
     * 用于设置 ConeDB 专属的默认颜色
     */
    void afterSubActorPropertiesInitialized() override;

    // === AutoRegisterDB接口扩展 ===
    // getProperty/setProperty 使用基类实现，不需要重写
    std::vector<std::string_view> getPropertyNames() const override;
    PropertyMap serialize() const override;
    bool deserialize(const PropertyMap& properties) override;

private:
    void markGeometryChanged(const trans::Prop& prop);
};
