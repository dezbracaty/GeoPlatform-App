#pragma once

#include "ActorDB.hpp"

/**
 * @brief CubeDB - 立方体数据库类
 *
 * 对应vtkCubeSource的所有属性
 */
class CubeDB : public ActorDB {
public:
    /**
     * @brief 构造函数
     */
    explicit CubeDB();

    /**
     * @brief 析构函数
     */
    virtual ~CubeDB();

    // === 使用几何属性宏 - 自动调用markGeometryChanged ===

    // 几何属性 - 修改时自动触发markGeometryChanged
    FIELD_GEOMETRY_VALUE(CubeDB, Vector3, Center)       // 中心点
    FIELD_GEOMETRY_VALUE_SIMPLE(CubeDB, float, XLength) // X方向长度
    FIELD_GEOMETRY_VALUE_SIMPLE(CubeDB, float, YLength) // Y方向长度
    FIELD_GEOMETRY_VALUE_SIMPLE(CubeDB, float, ZLength) // Z方向长度

    // === 便捷方法 ===

    /**
     * @brief 快速设置立方体
     */
    void setup(const Vector3& center, float xLen, float yLen, float zLen) {
        setCenter(center);
        setXLength(xLen);
        setYLength(yLen);
        setZLength(zLen);
    }

    /**
     * @brief 设置统一尺寸
     */
    void setUniformSize(float size) {
        setXLength(size);
        setYLength(size);
        setZLength(size);
    }

    /**
     * @brief 设置尺寸向量
     */
    void setSize(const Vector3& size) {
        setXLength(size.x);
        setYLength(size.y);
        setZLength(size.z);
    }

    Vector3 getSize() const {
        return Vector3(getXLength(), getYLength(), getZLength());
    }

    // === 实现ActorDB接口 ===

    TypeID getTypeID() const override {
        return TypeID::CUBE_DB;
    }

    BoundingBox localBounds() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

    // === AutoRegisterDB接口扩展 ===

    std::vector<std::string_view> getPropertyNames() const override;
    PropertyMap serialize() const override;
    bool deserialize(const PropertyMap& properties) override;

protected:
    // === 实现 ActorDB 的钩子函数 ===

    /**
     * @brief 初始化 CubeDB 特有的属性
     *
     * 由 ActorDB 的初始化流程自动调用
     */
    void initializeSubActorProperties() override;
    
    /**
     * @brief 在属性初始化完成后调用
     *
     * 用于设置 CubeDB 专属的默认颜色
     */
    void afterSubActorPropertiesInitialized() override;

private:
    /**
     * @brief 标记几何体变化
     * @param prop 变化的属性
     */
    void markGeometryChanged(const trans::Prop& prop);
};
