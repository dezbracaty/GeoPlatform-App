#pragma once

#include <AutoRegisterDB.hpp>
#include <SystemTypes.hpp>
#include <string>

/**
 * @brief 材质数据库类
 *
 * 正式文档 DB。归属关系只存在于 Document relation graph 中。
 */
class MaterialDB : public AutoRegisterDB {
public:
    /**
     * @brief 材质预设类型枚举
     */
    enum class MaterialPreset {
        MATTE_PLASTIC = 0,      // 哑光塑料（默认导入模型）
        SEMI_GLOSS_PLASTIC = 1, // 半光泽塑料（编辑环境）
        SLICE_LAYER = 2,        // 切片层材质（预览）
        SUPPORT_STRUCTURE = 3   // 支撑结构材质
    };

    /**
     * @brief 渲染模式枚举
     * 控制材质的光照计算模式
     */
    enum class RenderingMode {
        PHONG = 0,  // Phong 光照模型（传统光照）
        PBR = 1     // PBR 光照模型（基于物理的渲染）
    };

    /**
     * @brief 构造函数
     */
    MaterialDB() = default;
    virtual ~MaterialDB() = default;

    TypeID getTypeID() const override { return TypeID::MATERIAL_DB; }
    bool needsVTKSync() const override { return false; }
    bool isValid() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

    // ======= 材质预设方法 =======

    /**
     * @brief 应用材质预设
     * @param preset 预设类型
     * @param environmentAdjustment 环境色温调整（可选）：0.0=冷色调，0.5=中性，1.0=暖色调
     */
    void applyMaterialPreset(MaterialPreset preset, float environmentAdjustment = 0.5f);

    /**
     * @brief 应用哑光塑料材质（Normal环境默认）
     * @param environmentAdjustment 环境色温调整
     */
    void applyMattePlastic(float environmentAdjustment = 0.5f);

    /**
     * @brief 应用半光泽塑料材质（Editing环境）
     * @param environmentAdjustment 环境色温调整
     */
    void applySemiGlossPlastic(float environmentAdjustment = 0.5f);

    void copyValuesFrom(const MaterialDB& source);

    /** Type-safe convenience wrapper; AutoRegisterDB publishes the change. */
    template <typename T>
    void setProperty(const std::string& propertyName, const T& value) {
        trans::TransDB::setProperty(propertyName, value);
    }

protected:
    void initializeProperties() override;
    virtual void initializeSubMaterialProperties() {}

public:
    // 使用新的无默认值宏定义所有属性
    // 基础颜色属性
    FIELD_VALUE(MaterialDB, Vector3, Color)
    FIELD_VALUE(MaterialDB, Vector3, AmbientColor)
    // Base material properties always use MaterialDB as their property owner.
    // Using typeid(*this) here made the same property acquire a different key
    // on PBRMaterialDB and also turned a null call into std::bad_typeid.
    Vector3 getDiffuseColor() const {
        return trans::TransDB::getProperty<Vector3>(PROP_DiffuseColor());
    }
    void setDiffuseColor(const Vector3& value) {
        trans::TransDB::setProperty(PROP_DiffuseColor(), value);
    }
    FIELD_VALUE(MaterialDB, Vector3, SpecularColor)

    // 光照系数 - 使用自定义 getter/setter 以支持通知
    float getAmbient() const {
        return trans::TransDB::getProperty<float>(PROP_Ambient());
    }
    void setAmbient(float value) {
        trans::TransDB::setProperty(PROP_Ambient(), value);
    }

    float getDiffuse() const {
        return trans::TransDB::getProperty<float>(PROP_Diffuse());
    }
    void setDiffuse(float value) {
        trans::TransDB::setProperty(PROP_Diffuse(), value);
    }

    float getSpecular() const {
        return trans::TransDB::getProperty<float>(PROP_Specular());
    }
    void setSpecular(float value) {
        trans::TransDB::setProperty(PROP_Specular(), value);
    }

    float getSpecularPower() const {
        return trans::TransDB::getProperty<float>(PROP_SpecularPower());
    }
    void setSpecularPower(float value) {
        trans::TransDB::setProperty(PROP_SpecularPower(), value);
    }

    // PBR 属性 - 使用自定义 getter/setter 以支持通知
    float getMetallic() const {
        return trans::TransDB::getProperty<float>(PROP_Metallic());
    }
    void setMetallic(float value) {
        trans::TransDB::setProperty(PROP_Metallic(), value);
    }

    float getRoughness() const {
        return trans::TransDB::getProperty<float>(PROP_Roughness());
    }
    void setRoughness(float value) {
        trans::TransDB::setProperty(PROP_Roughness(), value);
    }
    // Opacity - 使用自定义getter/setter以支持通知
    float getOpacity() const {
        return trans::TransDB::getProperty<float>(PROP_Opacity());
    }
    void setOpacity(float value) {
        trans::TransDB::setProperty(PROP_Opacity(), value);
    }

    // 渲染属性
    FIELD_VALUE_SIMPLE(MaterialDB, bool, Lighting)
    FIELD_VALUE_SIMPLE(MaterialDB, bool, Interpolation)
    FIELD_VALUE_SIMPLE(MaterialDB, bool, EdgeVisibility)
    FIELD_VALUE(MaterialDB, Vector3, EdgeColor)
    FIELD_VALUE_SIMPLE(MaterialDB, float, LineWidth)
    FIELD_VALUE_SIMPLE(MaterialDB, int, Representation) // 0=点，1=线框，2=表面

    // 渲染模式 - 使用自定义 getter/setter 以支持通知
    int getRenderingMode() const {
        return trans::TransDB::getProperty<int>(PROP_RenderingMode());
    }
    void setRenderingMode(int value) {
        trans::TransDB::setProperty(PROP_RenderingMode(), value);
    }

    std::vector<std::string_view> getPropertyNames() const override {
        return AutoRegisterDB::getPropertyNames();
    }

private:
#define GPLATFORM_MATERIAL_PROP(name)                                \
    static const trans::Prop& PROP_##name() {                        \
        static const trans::Prop prop(typeid(MaterialDB), #name);    \
        return prop;                                                  \
    }
    GPLATFORM_MATERIAL_PROP(DiffuseColor)
    GPLATFORM_MATERIAL_PROP(Ambient)
    GPLATFORM_MATERIAL_PROP(Diffuse)
    GPLATFORM_MATERIAL_PROP(Specular)
    GPLATFORM_MATERIAL_PROP(SpecularPower)
    GPLATFORM_MATERIAL_PROP(Metallic)
    GPLATFORM_MATERIAL_PROP(Roughness)
    GPLATFORM_MATERIAL_PROP(Opacity)
    GPLATFORM_MATERIAL_PROP(RenderingMode)
#undef GPLATFORM_MATERIAL_PROP
};
