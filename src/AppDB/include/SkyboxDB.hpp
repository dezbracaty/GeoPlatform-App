#pragma once

#include <AutoRegisterDB.hpp>
#include <ChangeTypes.hpp>
#include <SystemTypes.hpp>
#include <memory>

/**
 * @brief 天空盒数据库类 - 管理场景环境和 PBR 光照
 *
 * 负责管理：
 * - 天空盒类型（渐变色、HDR 纹理等）
 * - 渐变色配置（天空色、地面色）
 * - 环境光照设置（IBL）
 *
 * 设计理念：
 * - 天空盒是场景级别的环境配置，不是 Widget
 * - 提供 PBR 渲染所需的环境贴图
 * - 支持预设切换和自定义配置
 */
class SkyboxDB : public AutoRegisterDB {
public:
    /**
     * @brief 天空盒预设类型
     */
    enum class PresetType {
        GRADIENT = 0,    // 程序化渐变（当前默认）
        SOLID_COLOR = 1, // 纯色背景
        HDR_TEXTURE = 2  // 经纬图 HDR 环境纹理
    };

public:
    /**
     * @brief 构造函数
     */
    explicit SkyboxDB();

    /**
     * @brief 析构函数
     */
    virtual ~SkyboxDB();

protected:
    // === 实现 AutoRegisterDB 的钩子函数 ===

    /**
     * @brief 初始化 SkyboxDB 特有的属性
     */
    void initializeProperties() override;

    /**
     * @brief 属性初始化后的钩子
     */
    void afterPropertiesInitialized() override;

public:
    /**
     * @brief 获取 DB 类型 ID
     */
    TypeID getTypeID() const override {
        return TypeID::SKYBOX_DB;
    }

    // === 基础属性 ===

    // 预设类型
    FIELD_VALUE_SIMPLE(SkyboxDB, int, PresetType)

    // 可见性
    FIELD_VALUE_SIMPLE(SkyboxDB, bool, Visible)

    // 是否启用 IBL（基于图像的光照）
    FIELD_VALUE_SIMPLE(SkyboxDB, bool, UseImageBasedLighting)

    // === 渐变色配置（用于 GRADIENT 预设）===

    // 天空顶部颜色 (RGB, 0-255)
    FIELD_VALUE(SkyboxDB, Vector3, SkyColorTop)

    // 天空底部颜色 (RGB, 0-255)
    FIELD_VALUE(SkyboxDB, Vector3, SkyColorBottom)

    // 地面颜色 (RGB, 0-255)
    FIELD_VALUE(SkyboxDB, Vector3, GroundColor)

    // === 纯色配置（用于 SOLID_COLOR 预设）===

    // 背景色 (RGB, 0-255)
    FIELD_VALUE(SkyboxDB, Vector3, BackgroundColor)

    // === HDR 纹理配置（用于 HDR_TEXTURE 预设）===

    // HDR 纹理路径
    FIELD_VALUE(SkyboxDB, std::string, HdrTexturePath)

    // 用于 PBR 光照/反射的环境资源。为空时回退到 HdrTexturePath。
    FIELD_VALUE(SkyboxDB, std::string, LightingTexturePath)

    // 用于屏幕可见背景的环境资源。为空时回退到 HdrTexturePath。
    FIELD_VALUE(SkyboxDB, std::string, BackgroundTexturePath)

    // 环境光强度（lux）。
    FIELD_VALUE_SIMPLE(SkyboxDB, float, LightingIntensity)

    // 名称
    FIELD_VALUE(SkyboxDB, std::string, Name)

    // === 预设类型辅助方法 ===

    PresetType getPresetTypeEnum() const {
        return static_cast<PresetType>(getPresetType());
    }

    void setPresetTypeEnum(PresetType type) {
        setPresetType(static_cast<int>(type));
    }

    // === 便捷方法 ===

    /**
     * @brief 应用默认天空渐变（当前效果）
     *
     * 天蓝色渐变：
     * - 顶部：RGB(135, 206, 250) - 天蓝色
     * - 底部：RGB(176, 224, 230) - 浅蓝色
     */
    void applyDefaultGradient();

    /**
     * @brief 应用工作室环境预设
     *
     * 中性灰白色，适合产品展示
     */
    void applyStudioPreset();

    /**
     * @brief 应用简洁背景预设
     *
     * 纯色浅灰背景，适合切片预览
     */
    void applyMinimalPreset();

    /**
     * @brief 应用 3D 打印工作室预设
     *
     * 可见背景与 IBL 光照分开配置：背景提供打印语境，
     * HDR 资源保持稳定的 PBR 反射与照明质量。
     */
    void applyPrintStudioPreset();

    // === 环境预设枚举 ===

    /**
     * @brief 环境预设类型
     */
    enum class EnvironmentPreset {
        NORMAL_PROFESSIONAL = 0,  // 专业蓝灰（Normal环境）
        EDITING_WARM = 1,         // 温暖工作室（Editing环境）
        TECHNICAL_NEUTRAL = 2,    // 技术中性灰（Slicing/Preview/Support）
        PRINTING_STUDIO = 3       // 3D 打印工作室（真实渲染）
    };

    /**
     * @brief 应用环境预设
     * @param preset 环境预设类型
     */
    void applyEnvironmentPreset(EnvironmentPreset preset);

    /**
     * @brief 应用 Normal 环境预设（专业蓝灰）
     *
     * 专业冷静的工业设计氛围：
     * - 天空顶部：柔和蓝灰 RGB(190, 205, 220)
     * - 天空底部：浅灰白 RGB(230, 235, 242)
     * - 地面：中性灰 RGB(175, 180, 185)
     */
    void applyNormalEnvironment();

    /**
     * @brief 应用 Editing 环境预设（温暖工作室）
     *
     * 专注细节的温暖工作室氛围：
     * - 天空顶部：暖灰 RGB(220, 215, 205)
     * - 天空底部：暖白 RGB(240, 235, 225)
     * - 地面：暖土色 RGB(200, 190, 175)
     */
    void applyEditingEnvironment();

    /**
     * @brief 应用 Technical 环境预设（技术中性灰）
     *
     * 技术性强、清晰简洁的切片预览环境：
     * - 天空顶部：中性灰蓝 RGB(210, 210, 215)
     * - 天空底部：高亮中性灰 RGB(235, 235, 240)
     * - 地面：纯中性灰 RGB(190, 190, 195)
     */
    void applyTechnicalEnvironment();

protected:
    /**
     * @brief 通知天空盒参数变化
     * @param propertyName 变化的属性名
     */
    void notifySkyboxChange(const std::string& propertyName);
};

// Qt 元对象系统支持
Q_DECLARE_METATYPE(SkyboxDB::PresetType)
