#include "../include/PrintBedDB.hpp"
#include "../include/PrintBedTemplateDB.hpp"
#include <DocumentManager.hpp>
#include "Foundation/Log.h"
#include <TransactionManager.hpp>
#include <cmath>

PrintBedDB::PrintBedDB()
    : ActorDB() {
    // 构造函数保持简单，所有初始化移到 onCreated() 中
}

PrintBedDB::~PrintBedDB() {
}

// === 实现 ActorDB 的钩子函数 ===

void PrintBedDB::initializeSubActorProperties() {
    setTemplateDBId(DBInstanceID());

    // 网格显示属性
    setShowGrid(true);
    setGridSpacing(PrintBedViewDefaults::DEFAULT_GRID_SPACING); // 10mm网格间距
    setShowBounds(false);    // 默认关闭可打印区域光墙（已完成调试）

    // 外观属性
    // 注意：Opacity 现在由 Material 管理，不在此处设置
    // 默认 Opacity 在 Material 初始化时设置
    setGridColor(Vector3(PrintBedViewDefaults::GRID_COLOR_R,
                         PrintBedViewDefaults::GRID_COLOR_G,
                         PrintBedViewDefaults::GRID_COLOR_B));
    setBoundColor(Vector3(PrintBedViewDefaults::GRID_COLOR_R,
                          PrintBedViewDefaults::GRID_COLOR_G,
                          PrintBedViewDefaults::GRID_COLOR_B));

    setCollisionMask(CollisionNone);

    // 禁用pick功能 - 打印平台不应该被选中
    setPickable(false);
}

void PrintBedDB::afterSubActorPropertiesInitialized() {
    // OrcaSlicer dark bed: Bed3D::DEFAULT_MODEL_COLOR_DARK * 0.8.
    auto material = getMaterial();
    if (!material) return;

    TransactionGuard guard("Init PrintBed Default Material (OrcaSlicer style)");

    material->setRenderingMode(static_cast<int>(MaterialDB::RenderingMode::PHONG));
    material->setDiffuseColor(Vector3(PrintBedViewDefaults::BED_COLOR_R,
                                      PrintBedViewDefaults::BED_COLOR_G,
                                      PrintBedViewDefaults::BED_COLOR_B));
    material->setColor(material->getDiffuseColor());
    material->setLighting(false);
    material->setAmbient(1.0f);
    material->setDiffuse(0.0f);
    material->setSpecular(0.0f);
    material->setSpecularPower(1.0f);

    // 清零 PBR 参数：UI 切到 PBR 时再让用户重新选预设
    material->setMetallic(0.0f);
    material->setRoughness(1.0f);

    material->setOpacity(1.0f);
    material->setEdgeVisibility(false);
    material->setLineWidth(PrintBedViewDefaults::GRID_LINE_WIDTH);
}

std::shared_ptr<PrintBedTemplateDB> PrintBedDB::getTemplateDB() {
    auto* document = DocumentManager::instance();
    if (!document || !getTemplateDBId().isValid()) return {};
    return std::dynamic_pointer_cast<PrintBedTemplateDB>(
        document->getDBInstance(getTemplateDBId()));
}

std::shared_ptr<const PrintBedTemplateDB> PrintBedDB::getTemplateDB() const {
    auto* document = DocumentManager::instance();
    if (!document || !getTemplateDBId().isValid()) return {};
    return std::dynamic_pointer_cast<const PrintBedTemplateDB>(
        document->getDBInstance(getTemplateDBId()));
}

float PrintBedDB::getWidth() const {
    const auto value = getTemplateDB();
    return value ? value->getWidth() : PrintBedDefaults::DEFAULT_WIDTH;
}

float PrintBedDB::getHeight() const {
    const auto value = getTemplateDB();
    return value ? value->getHeight() : PrintBedDefaults::DEFAULT_HEIGHT;
}

float PrintBedDB::getThickness() const {
    const auto value = getTemplateDB();
    return value ? value->getThickness() : PrintBedDefaults::PLATFORM_THICKNESS;
}

float PrintBedDB::getPrintHeight() const {
    const auto value = getTemplateDB();
    return value ? value->getPrintHeight() : PrintBedDefaults::DEFAULT_PRINT_HEIGHT;
}

Polygon2 PrintBedDB::getPrintableArea() const {
    const auto value = getTemplateDB();
    return value ? value->getPrintableArea() : Polygon2{};
}

Vector3 PrintBedDB::getOrigin() const {
    const auto value = getTemplateDB();
    return value ? value->getOrigin() : Vector3{};
}

Vector3 PrintBedDB::getCenter() const {
    const Vector3 origin = getOrigin();
    return Vector3(origin.x + getWidth() * 0.5f,
                   origin.y + getHeight() * 0.5f,
                   origin.z - getThickness() * 0.5f);
}

std::string PrintBedDB::getBedModelPath() const {
    const auto value = getTemplateDB();
    return value ? value->getBedModelPath() : std::string{};
}

std::string PrintBedDB::getBedTexturePath() const {
    const auto value = getTemplateDB();
    return value ? value->getBedTexturePath() : std::string{};
}

std::string PrintBedDB::getBedVendor() const {
    const auto value = getTemplateDB();
    return value ? value->getVendor() : std::string{};
}

std::string PrintBedDB::getBedPrinterModel() const {
    const auto value = getTemplateDB();
    return value ? value->getPrinterModel() : std::string{};
}

float PrintBedDB::getMaxTemperature() const {
    const auto value = getTemplateDB();
    return value ? value->getMaxTemperature() : 100.0f;
}

bool PrintBedDB::getHeated() const {
    const auto value = getTemplateDB();
    return value ? value->getHeated() : true;
}

void PrintBedDB::setWidth(float value) { if (auto db = getTemplateDB()) db->setWidth(value); }
void PrintBedDB::setHeight(float value) { if (auto db = getTemplateDB()) db->setHeight(value); }
void PrintBedDB::setThickness(float value) { if (auto db = getTemplateDB()) db->setThickness(value); }
void PrintBedDB::setPrintHeight(float value) { if (auto db = getTemplateDB()) db->setPrintHeight(value); }
void PrintBedDB::setPrintableArea(const Polygon2& value) { if (auto db = getTemplateDB()) db->setPrintableArea(value); }
void PrintBedDB::setOrigin(const Vector3& value) { if (auto db = getTemplateDB()) db->setOrigin(value); }
void PrintBedDB::setBedModelPath(const std::string& value) { if (auto db = getTemplateDB()) db->setBedModelPath(value); }
void PrintBedDB::setBedTexturePath(const std::string& value) { if (auto db = getTemplateDB()) db->setBedTexturePath(value); }
void PrintBedDB::setBedVendor(const std::string& value) { if (auto db = getTemplateDB()) db->setVendor(value); }
void PrintBedDB::setBedPrinterModel(const std::string& value) { if (auto db = getTemplateDB()) db->setPrinterModel(value); }
void PrintBedDB::setMaxTemperature(float value) { if (auto db = getTemplateDB()) db->setMaxTemperature(value); }
void PrintBedDB::setHeated(bool value) { if (auto db = getTemplateDB()) db->setHeated(value); }

void PrintBedDB::notifyTemplateChanged(const std::string& propertyName) {
    notifyGeometryChange(propertyName);
}

void PrintBedDB::applyMaterialPreset(MaterialPreset preset) {
    auto material = getMaterial();
    if (!material) return;

    // 使用事务包装材质修改，确保渲染层收到更新通知
    TransactionGuard guard("Switch PrintBed Material Preset");

    // 默认使用青灰色 RGB(171, 197, 184) - 优雅的金属质感
    material->setDiffuseColor(Vector3(171.0f/255.0f, 197.0f/255.0f, 184.0f/255.0f));
    material->setOpacity(getOpacity());

    switch (preset) {
        case MaterialPreset::MIRROR_METAL:
            // 镜面金属（略带粗糙度避免过度高光）
            material->setMetallic(1.0f);
            material->setRoughness(0.1f);
            material->setDiffuse(1.0f);  // VTK PBR 使用 Diffuse 参数
            LOG_INFO("🔄 PrintBedDB: Switched to MIRROR_METAL preset (Metallic: 1.0, Roughness: 0.1, Diffuse: 1.0) for ID: {}",
                     getDBInstanceID().toString());
            break;

        case MaterialPreset::SEMI_METAL:
            // 半金属（中球效果）
            material->setMetallic(0.5f);
            material->setRoughness(0.3f);
            material->setDiffuse(1.0f);  // VTK PBR 使用 Diffuse 参数
            LOG_INFO("🔄 PrintBedDB: Switched to SEMI_METAL preset (Metallic: 0.5, Roughness: 0.3, Diffuse: 1.0) for ID: {}",
                     getDBInstanceID().toString());
            break;

        case MaterialPreset::PLASTIC:
            // 塑料（右球效果）
            material->setMetallic(0.0f);
            material->setRoughness(0.5f);
            material->setDiffuse(1.0f);  // VTK PBR 使用 Diffuse 参数
            LOG_INFO("🔄 PrintBedDB: Switched to PLASTIC preset (Metallic: 0.0, Roughness: 0.5, Diffuse: 1.0) for ID: {}",
                     getDBInstanceID().toString());
            break;
    }
}

void PrintBedDB::cycleToNextMaterialPreset() {
    auto material = getMaterial();
    if (!material) return;

    // 根据当前材质状态判断预设类型
    float metallic = material->getMetallic();
    float roughness = material->getRoughness();

    MaterialPreset currentPreset;
    if (std::abs(metallic - 1.0f) < 0.01f && std::abs(roughness - 0.0f) < 0.01f) {
        currentPreset = MaterialPreset::MIRROR_METAL;
    } else if (std::abs(metallic - 0.5f) < 0.01f && std::abs(roughness - 0.3f) < 0.01f) {
        currentPreset = MaterialPreset::SEMI_METAL;
    } else {
        currentPreset = MaterialPreset::PLASTIC;
    }

    // 切换到下一个预设
    MaterialPreset nextPreset = static_cast<MaterialPreset>((static_cast<int>(currentPreset) + 1) % 3);
    applyMaterialPreset(nextPreset);
}

// === ActorDB接口实现 ===

ActorDB::BoundingBox PrintBedDB::localBounds() const {
    BoundingBox bounds;

    // 计算打印床的边界框
    Vector3 center = getCenter();         // getCenter() 已经是线程安全的
    float halfWidth = getWidth() / 2.0f;  // getWidth() 已经是线程安全的
    float halfHeight = getHeight() / 2.0f; // getHeight() 已经是线程安全的
    float halfThickness = getThickness() / 2.0f; // getThickness() 已经是线程安全的

    bounds.min = Vector3(
        center.x - halfWidth,
        center.y - halfHeight,
        center.z - halfThickness
    );

    bounds.max = Vector3(
        center.x + halfWidth,
        center.y + halfHeight,
        center.z + halfThickness
    );

    bounds.valid = true;

    return bounds;
}

std::shared_ptr<AutoRegisterDB> PrintBedDB::clone() const {
    auto lock = getSharedLock();

    // 使用工厂方法创建新实例
    auto cloned = trans::TransDB::create<PrintBedDB>();

    cloned->setShowGrid(getShowGrid());
    cloned->setGridSpacing(getGridSpacing());
    cloned->setShowBounds(getShowBounds());

    // 注意：Opacity 由 Material 管理，会在复制 Material 时自动复制
    cloned->setGridColor(getGridColor());
    cloned->setBoundColor(getBoundColor());

    // 复制 Material 的 Phong 参数（从 Material 读取）
    auto material = getMaterial();
    if (material) {
        auto clonedMaterial = cloned->getMaterial();
        if (clonedMaterial) {
            clonedMaterial->setAmbient(material->getAmbient());
            clonedMaterial->setDiffuse(material->getDiffuse());
            clonedMaterial->setSpecular(material->getSpecular());
            clonedMaterial->setSpecularPower(material->getSpecularPower());
        }
    }

    // 复制基类属性
    cloned->copyTransformFrom(*this);
    cloned->setVisible(isVisible());
    auto srcMaterial = getMaterial();
    auto dstMaterial = cloned->getMaterial();
    if (srcMaterial && dstMaterial) {
        dstMaterial->deserialize(srcMaterial->serialize());
    }
    cloned->setPickable(isPickable());
    cloned->setDragable(isDragable());

    return cloned;
}

std::vector<DBRelationRef> PrintBedDB::reportRelations() const {
    if (!getTemplateDBId().isValid()) return {};
    return {{"Template", getTemplateDBId()}};
}

bool PrintBedDB::replaceRelations(
    const std::vector<DBRelationReplacement>& replacements) {
    if (replacements.empty()) return true;
    if (replacements.size() != 1 || replacements.front().relationName != "Template") {
        return false;
    }
    setTemplateDBId(replacements.front().newTargetId);
    return getTemplateDBId() == replacements.front().newTargetId;
}

// === AutoRegisterDB接口扩展 ===

std::vector<std::string_view> PrintBedDB::getPropertyNames() const {
    // All properties are handled by the base class property system
    return ActorDB::getPropertyNames();
}

PropertyMap PrintBedDB::serialize() const {
    // 直接使用基类的序列化
    // 所有属性都已经在属性字典中
    return ActorDB::serialize();
}

bool PrintBedDB::deserialize(const PropertyMap& properties) {
    // 直接使用基类的反序列化
    // 基类会处理所有在属性字典中的属性
    return ActorDB::deserialize(properties);
}
