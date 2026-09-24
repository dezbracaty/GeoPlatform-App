#pragma once

#include "ActorDB.hpp"

/**
 * @brief CylinderDB - 圆柱体数据库类
 *
 * 对应vtkCylinderSource的所有属性
 */
class CylinderDB : public ActorDB {
public:
    explicit CylinderDB();
    virtual ~CylinderDB();

    // === 几何属性（使用几何属性宏 - 自动调用markGeometryChanged） ===
    FIELD_GEOMETRY_VALUE(CylinderDB, Vector3, Center)
    FIELD_GEOMETRY_VALUE_SIMPLE(CylinderDB, float, Height)
    FIELD_GEOMETRY_VALUE_SIMPLE(CylinderDB, float, Radius)
    FIELD_GEOMETRY_VALUE_SIMPLE(CylinderDB, int, Resolution)
    FIELD_GEOMETRY_VALUE_SIMPLE(CylinderDB, bool, Capping)

    // === 实现ActorDB接口 ===
    TypeID getTypeID() const override {
        return TypeID::CYLINDER_DB;
    }

    BoundingBox localBounds() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

protected:
    // === 实现 ActorDB 的钩子函数 ===

    /**
     * @brief 初始化 CylinderDB 特有的属性
     *
     * 由 ActorDB 的初始化流程自动调用
     */
    void initializeSubActorProperties() override;
    
    /**
     * @brief 在属性初始化完成后调用
     *
     * 用于设置 CylinderDB 专属的默认颜色
     */
    void afterSubActorPropertiesInitialized() override;

    // === AutoRegisterDB接口扩展 ===
    std::vector<std::string_view> getPropertyNames() const override;
    PropertyMap serialize() const override;
    bool deserialize(const PropertyMap& properties) override;

private:
    void markGeometryChanged(const trans::Prop& prop);
};
