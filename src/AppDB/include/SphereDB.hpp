#pragma once

#include "ActorDB.hpp"
#include <atomic>

/**
 * @brief SphereDB - 球体数据库类
 *
 * 对应vtkSphereSource的所有属性
 */
class SphereDB : public ActorDB {
public:
    /**
     * @brief 构造函数
     */
    explicit SphereDB();

    /**
     * @brief 析构函数
     */
    virtual ~SphereDB();

    // === 使用几何属性宏 - 自动调用markGeometryChanged ===

    // 几何属性 - 修改时自动触发markGeometryChanged
    FIELD_GEOMETRY_VALUE(SphereDB, Vector3, Center)      // 球心
    FIELD_GEOMETRY_VALUE_SIMPLE(SphereDB, float, Radius) // 半径

    // 细分属性
    FIELD_GEOMETRY_VALUE_SIMPLE(SphereDB, int, ThetaResolution) // 经度细分
    FIELD_GEOMETRY_VALUE_SIMPLE(SphereDB, int, PhiResolution)   // 纬度细分

    // 部分球体属性
    FIELD_GEOMETRY_VALUE_SIMPLE(SphereDB, float, StartTheta) // 起始经度角
    FIELD_GEOMETRY_VALUE_SIMPLE(SphereDB, float, EndTheta)   // 结束经度角
    FIELD_GEOMETRY_VALUE_SIMPLE(SphereDB, float, StartPhi)   // 起始纬度角
    FIELD_GEOMETRY_VALUE_SIMPLE(SphereDB, float, EndPhi)     // 结束纬度角

    // 细分模式
    FIELD_GEOMETRY_VALUE_SIMPLE(SphereDB, bool, LatLongTessellation) // 经纬度细分模式

    // === 便捷方法 ===

    /**
     * @brief 快速设置球体
     */
    void setup(const Vector3& center, float radius, int resolution = 16) {
        setCenter(center);
        setRadius(radius);
        setThetaResolution(resolution);
        setPhiResolution(resolution);
    }

    /**
     * @brief 创建半球
     */
    void createHemisphere(bool upper = true) {
        setStartPhi(upper ? 0.0f : 90.0f);
        setEndPhi(upper ? 90.0f : 180.0f);
    }

    // === 实现ActorDB接口 ===

    TypeID getTypeID() const override {
        return TypeID::SPHERE_DB;
    }

    BoundingBox localBounds() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

protected:
    // === 实现 ActorDB 的钩子函数 ===

    /**
     * @brief 初始化 SphereDB 特有的属性
     *
     * 由 ActorDB 的初始化流程自动调用
     */
    void initializeSubActorProperties() override;
    
    /**
     * @brief 在属性初始化完成后调用
     *
     * 用于设置 SphereDB 专属的默认颜色
     */
    void afterSubActorPropertiesInitialized() override;

    // === AutoRegisterDB接口扩展 ===

    std::vector<std::string_view> getPropertyNames() const override;
    PropertyMap serialize() const override;
    bool deserialize(const PropertyMap& properties) override;

private:
    /**
     * @brief 标记几何体变化
     */
    void markGeometryChanged(const trans::Prop& prop);
};
