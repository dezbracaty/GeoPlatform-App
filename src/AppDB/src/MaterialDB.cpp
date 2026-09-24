#include "../include/MaterialDB.hpp"
#include "../include/ActorDB.hpp"
#include "../include/ModelPartDB.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"
#include <algorithm>

void MaterialDB::initializeProperties() {
    // 初始化所有材质属性的默认值
    // 不依赖静态默认值机制，而是在这里显式设置

    // OrcaSlicer GLVolume::NEUTRAL_COLOR.
    setColor(Vector3(0.8f, 0.8f, 0.8f));
    setAmbientColor(Vector3(1.0f, 1.0f, 1.0f));  // 环境光颜色
    setDiffuseColor(Vector3(0.8f, 0.8f, 0.8f));
    setSpecularColor(Vector3(1.0f, 1.0f, 1.0f)); // 镜面反射颜色

    // OrcaSlicer gouraud_light approximation:
    // ambient=0.3, diffuse top+front≈0.66, specular=0.075, shininess=20.
    setAmbient(0.30f);
    setDiffuse(0.66f);
    setSpecular(0.075f);
    setSpecularPower(20.0f);

    // PBR 属性
    setMetallic(0.0f);  // 金属度（0=非金属，默认不启用PBR）
    setRoughness(1.0f); // 粗糙度（1.0=完全粗糙，默认不启用PBR）
    setOpacity(1.0f);   // 不透明度（1=完全不透明）

    // 渲染属性
    setLighting(true);                       // 启用光照
    setInterpolation(true);                  // 启用插值（平滑着色）
    setEdgeVisibility(false);                // 不显示边缘
    setEdgeColor(Vector3(0.0f, 0.0f, 0.0f)); // 边缘颜色（黑色）
    setLineWidth(1.0f);                      // 线宽
    setRepresentation(2);                    // 渲染模式（2=表面）

    // 渲染模式 - 默认为 Phong（传统光照模型）
    setRenderingMode(static_cast<int>(RenderingMode::PHONG));

    initializeSubMaterialProperties();
}

bool MaterialDB::isValid() const {
    if (!AutoRegisterDB::isValid()) return false;
    auto* document = DocumentManager::instance();
    if (!document) return false;
    const DBInstanceID ownerId = document->getOwner(getDBInstanceID());
    if (!ownerId.isValid()) return false;
    if (const auto actor = document->getDB<ActorDB>(ownerId)) {
        return actor->getMaterial() &&
            actor->getMaterial()->getDBInstanceID() == getDBInstanceID();
    }
    const auto part = document->getDB<ModelPartDB>(ownerId);
    return part && part->getMaterial() &&
        part->getMaterial()->getDBInstanceID() == getDBInstanceID();
}

void MaterialDB::copyValuesFrom(const MaterialDB& source) {
    setColor(source.getColor());
    setAmbientColor(source.getAmbientColor());
    setDiffuseColor(source.getDiffuseColor());
    setSpecularColor(source.getSpecularColor());
    setAmbient(source.getAmbient());
    setDiffuse(source.getDiffuse());
    setSpecular(source.getSpecular());
    setSpecularPower(source.getSpecularPower());
    setMetallic(source.getMetallic());
    setRoughness(source.getRoughness());
    setOpacity(source.getOpacity());
    setLighting(source.getLighting());
    setInterpolation(source.getInterpolation());
    setEdgeVisibility(source.getEdgeVisibility());
    setEdgeColor(source.getEdgeColor());
    setLineWidth(source.getLineWidth());
    setRepresentation(source.getRepresentation());
    setRenderingMode(source.getRenderingMode());
}

std::shared_ptr<AutoRegisterDB> MaterialDB::clone() const {
    auto copy = trans::TransDB::create<MaterialDB>();
    copy->setDisplayName(getDisplayName());
    copy->copyValuesFrom(*this);
    return copy;
}

// ======= 材质预设实现 =======

void MaterialDB::applyMaterialPreset(MaterialPreset preset, float environmentAdjustment) {
    switch (preset) {
        case MaterialPreset::MATTE_PLASTIC:
            applyMattePlastic(environmentAdjustment);
            break;
        case MaterialPreset::SEMI_GLOSS_PLASTIC:
            applySemiGlossPlastic(environmentAdjustment);
            break;
        case MaterialPreset::SLICE_LAYER:
            // TODO: 实现切片层材质（第二阶段）
            LOG_WARN("MaterialDB: SLICE_LAYER preset not yet implemented");
            break;
        case MaterialPreset::SUPPORT_STRUCTURE:
            // TODO: 实现支撑结构材质（第二阶段）
            LOG_WARN("MaterialDB: SUPPORT_STRUCTURE preset not yet implemented");
            break;
        default:
            LOG_ERROR("MaterialDB: Unknown material preset: {}", static_cast<int>(preset));
            break;
    }
}

void MaterialDB::applyMattePlastic(float environmentAdjustment) {
    // 夹紧环境调整参数到 [0.0, 1.0]
    environmentAdjustment = std::clamp(environmentAdjustment, 0.0f, 1.0f);

    // 基础颜色：中性灰色 (0.85, 0.85, 0.90)
    Vector3 baseColor(0.85f, 0.85f, 0.90f);

    // 根据环境调整色温
    // environmentAdjustment: 0.0 = 冷色调（蓝灰），0.5 = 中性，1.0 = 暖色调（米黄）
    if (environmentAdjustment < 0.5f) {
        // 冷色调：增加蓝色分量，减少红色分量
        float coldFactor = (0.5f - environmentAdjustment) * 2.0f; // 0.0 到 1.0
        baseColor.x -= coldFactor * 0.05f; // 红色减少
        baseColor.z += coldFactor * 0.05f; // 蓝色增加
    } else if (environmentAdjustment > 0.5f) {
        // 暖色调：增加红色分量，减少蓝色分量
        float warmFactor = (environmentAdjustment - 0.5f) * 2.0f; // 0.0 到 1.0
        baseColor.x += warmFactor * 0.05f; // 红色增加
        baseColor.z -= warmFactor * 0.05f; // 蓝色减少
    }

    // PBR 参数 - 哑光塑料
    setMetallic(0.0f);     // 完全非金属
    setRoughness(0.6f);    // 较高粗糙度（哑光效果）
    setDiffuseColor(baseColor);
    setOpacity(1.0f);      // 完全不透明

    // 光照系数
    setAmbient(0.3f);
    setDiffuse(1.0f);
    setSpecular(0.15f);    // 低镜面反射（哑光）
    setSpecularPower(10.0f);

    // 渲染属性
    setLighting(true);
    setInterpolation(true);
    setRepresentation(2);  // 表面渲染

    LOG_INFO("MaterialDB: Applied Matte Plastic preset (environmentAdjustment: {:.2f})", environmentAdjustment);
}

void MaterialDB::applySemiGlossPlastic(float environmentAdjustment) {
    // 夹紧环境调整参数到 [0.0, 1.0]
    environmentAdjustment = std::clamp(environmentAdjustment, 0.0f, 1.0f);

    // 基础颜色：稍微偏暖的灰色 (0.88, 0.86, 0.84)
    Vector3 baseColor(0.88f, 0.86f, 0.84f);

    // 根据环境调整色温（Editing环境通常偏暖）
    if (environmentAdjustment < 0.5f) {
        // 冷色调
        float coldFactor = (0.5f - environmentAdjustment) * 2.0f;
        baseColor.x -= coldFactor * 0.04f;
        baseColor.z += coldFactor * 0.04f;
    } else if (environmentAdjustment > 0.5f) {
        // 暖色调
        float warmFactor = (environmentAdjustment - 0.5f) * 2.0f;
        baseColor.x += warmFactor * 0.04f;
        baseColor.z -= warmFactor * 0.04f;
    }

    // PBR 参数 - 半光泽塑料
    setMetallic(0.0f);     // 完全非金属
    setRoughness(0.4f);    // 中等粗糙度（半光泽效果）
    setDiffuseColor(baseColor);
    setOpacity(1.0f);      // 完全不透明

    // 光照系数
    setAmbient(0.35f);
    setDiffuse(1.0f);
    setSpecular(0.25f);    // 中等镜面反射（半光泽）
    setSpecularPower(20.0f);

    // 渲染属性
    setLighting(true);
    setInterpolation(true);
    setRepresentation(2);  // 表面渲染

    LOG_INFO("MaterialDB: Applied Semi-Gloss Plastic preset (environmentAdjustment: {:.2f})", environmentAdjustment);
}
