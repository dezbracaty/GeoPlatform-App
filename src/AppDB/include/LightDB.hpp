#pragma once

#include <AutoRegisterDB.hpp>
#include <ChangeTypes.hpp>
#include <SystemTypes.hpp>
#include <memory>

/**
 * @brief 灯光类型枚举
 *
 * 对应 VTK 的 vtkLight 类型：
 * - HEADLIGHT: 头灯，始终跟随相机，位于相机位置
 * - CAMERA_LIGHT: 相机灯光，相对相机位置偏移
 * - SCENE_LIGHT: 场景灯光，固定在世界坐标系中
 */
enum class LightType {
    HEADLIGHT = 0,      // 头灯（始终跟随相机）
    CAMERA_LIGHT = 1,   // 相机灯光（相对相机位置）
    SCENE_LIGHT = 2     // 场景灯光（固定世界坐标）
};

/**
 * @brief LightDB - 灯光数据库对象
 *
 * 管理场景中的灯光参数���与 ActorDB 不同：
 * - 灯光不是几何体，不参与 Pick
 * - 灯光没有渲染层级概念（全局生效）
 * - 灯光使用 Switch 而非 Visibility 控制开关
 *
 * 设计模式：管理模式
 * - DBSync 需要 Create/AddLight/RemoveLight/Destroy
 * - 与 CameraDBSync（引用模式）不同
 */
class LightDB : public AutoRegisterDB {
public:
    /**
     * @brief 构造函数
     */
    explicit LightDB();

    /**
     * @brief 析构函数
     */
    virtual ~LightDB();

protected:
    // === 实现 AutoRegisterDB 的钩子函数 ===

    /**
     * @brief 初始化 LightDB 特有的属性
     */
    void initializeProperties() override;

public:
    /**
     * @brief 获取DB类型ID
     */
    TypeID getTypeID() const override {
        return TypeID::LIGHT_DB;
    }

    // === 灯光属性定义 ===

    // 灯光类型（存储为 int，使用 LightType 枚举）
    FIELD_VALUE_SIMPLE(LightDB, int, LightType)

    // 颜色 (RGB, 0-1 范围)
    FIELD_VALUE(LightDB, Vector3, Color)

    // 强度 (0.0 - 2.0, 默认 1.0)
    FIELD_VALUE_SIMPLE(LightDB, float, Intensity)

    // 开关状态
    FIELD_VALUE_SIMPLE(LightDB, bool, Enabled)

    // 位置（世界坐标，仅对 CAMERA_LIGHT 和 SCENE_LIGHT 有效）
    FIELD_VALUE(LightDB, Vector3, Position)

    // 焦点/目标点（仅对 SCENE_LIGHT 有效）
    FIELD_VALUE(LightDB, Vector3, FocalPoint)

    // 灯光名称（用于标识和调试）
    FIELD_VALUE(LightDB, std::string, Name)

    // === 类型转换辅助方法 ===

    /**
     * @brief 获取灯光类型枚举
     */
    LightType getLightTypeEnum() const {
        return static_cast<LightType>(getLightType());
    }

    /**
     * @brief 设置灯光类型枚举
     */
    void setLightTypeEnum(LightType type) {
        setLightType(static_cast<int>(type));
    }

    /**
     * @brief 判断是否是头灯
     */
    bool isHeadlight() const {
        return getLightTypeEnum() == LightType::HEADLIGHT;
    }

    /**
     * @brief 判断是否是相机灯光
     */
    bool isCameraLight() const {
        return getLightTypeEnum() == LightType::CAMERA_LIGHT;
    }

    /**
     * @brief 判断是否是场景灯光
     */
    bool isSceneLight() const {
        return getLightTypeEnum() == LightType::SCENE_LIGHT;
    }

    // === 便捷设置方法 ===

    /**
     * @brief 设置为头灯
     */
    void setAsHeadlight() {
        setLightTypeEnum(LightType::HEADLIGHT);
    }

    /**
     * @brief 设置为相机灯光
     */
    void setAsCameraLight() {
        setLightTypeEnum(LightType::CAMERA_LIGHT);
    }

    /**
     * @brief 设置为场景灯光
     */
    void setAsSceneLight() {
        setLightTypeEnum(LightType::SCENE_LIGHT);
    }

    /**
     * @brief 打开灯光
     */
    void turnOn() {
        setEnabled(true);
    }

    /**
     * @brief 关闭灯光
     */
    void turnOff() {
        setEnabled(false);
    }

    /**
     * @brief 切换灯光开关
     */
    void toggle() {
        setEnabled(!getEnabled());
    }

    /**
     * @brief 设置颜色（RGB 分量形式）
     */
    void setColorRGB(float r, float g, float b) {
        setColor(Vector3(r, g, b));
    }

protected:
    /**
     * @brief 通知灯光参数变化
     */
    void notifyLightChange(const trans::Prop& prop);
};

// Qt 元对象系统支持
Q_DECLARE_METATYPE(LightType)
