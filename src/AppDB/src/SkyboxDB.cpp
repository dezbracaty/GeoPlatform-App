#include "SkyboxDB.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"

SkyboxDB::SkyboxDB()
    : AutoRegisterDB() {
    // 构造函数保持简单，所有初始化移到 onCreated() 中
}

SkyboxDB::~SkyboxDB() = default;

// === 实现 AutoRegisterDB 的钩子函数 ===

void SkyboxDB::initializeProperties() {
    // 初始化所有天空盒属性的默认值

    // 预设类型：默认使用渐变
    setPresetType(static_cast<int>(PresetType::GRADIENT));

    // 可见性
    setVisible(true);

    // 启用 IBL
    setUseImageBasedLighting(true);

    // 🌤️ 真实天空渐变 - 全蓝色系,模拟真实晴朗天空
    // 参考真实天空的颜色：顶部深蓝 → 地平线浅蓝白色 → 底面也是蓝色
    // 就像抬头看到的蓝天白云那样自然清爽,整个环境都是蓝色调
    //
    // 顶部：深蓝天空 RGB(135, 206, 235) - 标准天蓝色(Sky Blue)
    // 地平线：浅蓝白色 RGB(210, 230, 255) - 地平线处的淡蓝白色
    // 地面：明亮浅蓝 RGB(150, 200, 235) - 底面也是蓝色,与天空呼应
    setSkyColorTop(Vector3(135.0f, 206.0f, 235.0f));
    setSkyColorBottom(Vector3(210.0f, 230.0f, 255.0f));
    setGroundColor(Vector3(150.0f, 200.0f, 235.0f));

    // 纯色背景（用于 SOLID_COLOR 预设）
    setBackgroundColor(Vector3(200.0f, 200.0f, 200.0f));

    // HDR 纹理路径（默认未配置）
    setHdrTexturePath("");
    setLightingTexturePath("");
    setBackgroundTexturePath("");
    setLightingIntensity(30000.0f);

    // 名称
    setName("Skybox");
}

void SkyboxDB::afterPropertiesInitialized() {
    // 设置属性变化回调
    std::weak_ptr<SkyboxDB> weakThis =
        std::dynamic_pointer_cast<SkyboxDB>(shared_from_this());

    auto notifyChange = [weakThis](const std::string& propertyName) {
        if (auto self = weakThis.lock()) {
            if (auto* docManager = DocumentManager::instance()) {
                docManager->notifyChange(self.get(), ChangeType::PROPERTY_CHANGED, propertyName);
            }
        }
    };

    // 注册属性变化回调
    onPresetTypeChanged = [notifyChange](int) { notifyChange("PresetType"); };
    onVisibleChanged = [notifyChange](bool) { notifyChange("Visible"); };
    onUseImageBasedLightingChanged = [notifyChange](bool) { notifyChange("UseImageBasedLighting"); };
    onSkyColorTopChanged = [notifyChange](const Vector3&) { notifyChange("SkyColorTop"); };
    onSkyColorBottomChanged = [notifyChange](const Vector3&) { notifyChange("SkyColorBottom"); };
    onGroundColorChanged = [notifyChange](const Vector3&) { notifyChange("GroundColor"); };
    onBackgroundColorChanged = [notifyChange](const Vector3&) { notifyChange("BackgroundColor"); };
    onHdrTexturePathChanged = [notifyChange](const std::string&) { notifyChange("HdrTexturePath"); };
    onLightingTexturePathChanged = [notifyChange](const std::string&) { notifyChange("LightingTexturePath"); };
    onBackgroundTexturePathChanged = [notifyChange](const std::string&) { notifyChange("BackgroundTexturePath"); };
    onLightingIntensityChanged = [notifyChange](float) { notifyChange("LightingIntensity"); };

    LOG_DEBUG("SkyboxDB initialized with default gradient preset");
}

// === 便捷方法 ===

void SkyboxDB::applyDefaultGradient() {
    setPresetTypeEnum(PresetType::GRADIENT);
    // 柔和蓝灰渐变（适合 3D 打印/工业设计软件）
    setSkyColorTop(Vector3(200.0f, 210.0f, 220.0f));
    setSkyColorBottom(Vector3(235.0f, 238.0f, 242.0f));
    setGroundColor(Vector3(180.0f, 175.0f, 170.0f));
    setUseImageBasedLighting(true);

    LOG_DEBUG("SkyboxDB: Applied default gradient preset");
}

void SkyboxDB::applyStudioPreset() {
    setPresetTypeEnum(PresetType::GRADIENT);
    // 中性灰白色工作室环境
    setSkyColorTop(Vector3(240.0f, 240.0f, 245.0f));
    setSkyColorBottom(Vector3(220.0f, 220.0f, 225.0f));
    setGroundColor(Vector3(200.0f, 200.0f, 205.0f));
    setUseImageBasedLighting(true);

    LOG_DEBUG("SkyboxDB: Applied studio preset");
}

void SkyboxDB::applyMinimalPreset() {
    setPresetTypeEnum(PresetType::SOLID_COLOR);
    // 纯色浅灰背景
    setBackgroundColor(Vector3(230.0f, 230.0f, 230.0f));
    setUseImageBasedLighting(false);

    LOG_DEBUG("SkyboxDB: Applied minimal preset");
}

void SkyboxDB::applyPrintStudioPreset() {
    constexpr auto pillars = "builtin://gplatform/showcase/pillars-2k";
    constexpr auto printStudio =
        "builtin://gplatform/environment/print-studio";
    setPresetTypeEnum(PresetType::HDR_TEXTURE);
    // Legacy/single-texture backends continue to receive a valid HDR map.
    setHdrTexturePath(pillars);
    setLightingTexturePath(pillars);
    setBackgroundTexturePath(printStudio);
    setLightingIntensity(30000.0f);
    setVisible(true);
    setUseImageBasedLighting(true);

    LOG_INFO("SkyboxDB: Applied 3D printing studio environment preset");
}

// === 环境预设实现 ===

void SkyboxDB::applyEnvironmentPreset(EnvironmentPreset preset) {
    switch (preset) {
        case EnvironmentPreset::NORMAL_PROFESSIONAL:
            applyNormalEnvironment();
            break;
        case EnvironmentPreset::EDITING_WARM:
            applyEditingEnvironment();
            break;
        case EnvironmentPreset::TECHNICAL_NEUTRAL:
            applyTechnicalEnvironment();
            break;
        case EnvironmentPreset::PRINTING_STUDIO:
            applyPrintStudioPreset();
            break;
    }
}

void SkyboxDB::applyNormalEnvironment() {
    setPresetTypeEnum(PresetType::GRADIENT);

    // 🌤️ 真实天空渐变 - 全蓝色系,模拟真实晴朗天空
    // 就像抬头看到的蓝天白云那样自然清爽,整个环境都是蓝色调
    setSkyColorTop(Vector3(135.0f, 206.0f, 235.0f));     // 深蓝天空 - 标准天蓝色
    setSkyColorBottom(Vector3(210.0f, 230.0f, 255.0f));  // 浅蓝白色 - 地平线淡蓝
    setGroundColor(Vector3(150.0f, 200.0f, 235.0f));     // 明亮浅蓝 - 底面蓝色
    setUseImageBasedLighting(true);

    LOG_INFO("SkyboxDB: Applied Normal environment preset (Natural Sky)");
}

void SkyboxDB::applyEditingEnvironment() {
    setPresetTypeEnum(PresetType::GRADIENT);

    // 温暖工作室渐变 - 专注舒适氛围
    setSkyColorTop(Vector3(220.0f, 215.0f, 205.0f));     // 暖灰
    setSkyColorBottom(Vector3(240.0f, 235.0f, 225.0f));  // 暖白
    setGroundColor(Vector3(200.0f, 190.0f, 175.0f));     // 暖土色
    setUseImageBasedLighting(true);

    LOG_INFO("SkyboxDB: Applied Editing environment preset (Warm Studio)");
}

void SkyboxDB::applyTechnicalEnvironment() {
    setPresetTypeEnum(PresetType::GRADIENT);

    // 技术中性灰渐变 - 清晰简洁氛围
    setSkyColorTop(Vector3(210.0f, 210.0f, 215.0f));     // 中性灰蓝
    setSkyColorBottom(Vector3(235.0f, 235.0f, 240.0f));  // 高亮中性灰
    setGroundColor(Vector3(190.0f, 190.0f, 195.0f));     // 纯中性灰
    setUseImageBasedLighting(true);

    LOG_INFO("SkyboxDB: Applied Technical environment preset (Neutral Gray)");
}

void SkyboxDB::notifySkyboxChange(const std::string& propertyName) {
    if (auto* docManager = DocumentManager::instance()) {
        docManager->notifyChange(this, ChangeType::PROPERTY_CHANGED, propertyName);
    }
}
