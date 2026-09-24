#pragma once

#include "StandardActionHandler.hpp"
#include <memory>

// Forward declarations
struct Vector3;

/**
 * @brief 场景初始化和管理 Handler
 *
 * 负责场景级别的操作：
 * - scene.init: 初始化完整场景（Skybox + PrintBed）
 * - scene.skybox.preset: 切换天空盒预设
 * - scene.skybox.colors: 修改天空盒颜色
 *
 * 设计原则：
 * - 场景初始化是幂等的，多次调用不会重复创建
 * - Skybox 先于 PrintBed 创建（IBL 需要先设置好）
 */
class SceneHandler : public StandardActionHandler {
    Q_OBJECT

public:
    explicit SceneHandler(QObject* parent = nullptr);
    virtual ~SceneHandler() = default;

    void onEnter(std::shared_ptr<ActionContext> context) override;

private:
    // === 场景初始化 ===

    /**
     * @brief 初始化完整场景
     * @param params 包含 skybox 和 printBed 的配置参数
     */
    void initScene(const QVariantMap& params);

    /**
     * @brief 创建天空盒
     * @param params skybox 配置参数（preset, useIBL, colors 等）
     * @return 是否成功创建（如果已存在返回 true）
     */
    bool createSkybox(const QVariantMap& params);

    /**
     * @brief 创建打印平台
     * @param params printBed 配置参数（width, height, thickness 等）
     * @return 是否成功创建（如果已存在返回 true）
     */
    bool createPrintBed(const QVariantMap& params);

    /** Synchronize the existing platform from the active libslicer machine profile. */
    bool syncPrintBed(const QVariantMap& params, bool fitCamera);
    void syncPrintBedFromActiveMachine();

    /**
     * @brief 创建打印机模型（可选）
     * @param params printerModel 配置参数（filePath, color 等）
     * @return 是否成功创建
     */
    bool createPrinterModel(const QVariantMap& params);

    /**
     * @brief 创建默认灯光
     * @return 是否成功创建
     *
     * 创建经典三点光照配置：
     * - 主光源 (Key Light): 主要照明，45度角
     * - 填充光 (Fill Light): 补充阴影区域
     */
    bool createDefaultLights();

    // === 天空盒操作 ===

    /**
     * @brief 切换天空盒预设
     * @param params 包含 preset 名称（gradient, studio, minimal）
     */
    void setSkyboxPreset(const QVariantMap& params);

    /**
     * @brief 修改天空盒颜色
     * @param params 包含 skyColorTop, skyColorBottom, groundColor
     */
    void setSkyboxColors(const QVariantMap& params);

    // === 辅助方法 ===

    /**
     * @brief 检查场景中是否已存在指定类型的对象
     * @param typeId 对象类型 ID
     * @return 是否存在
     */
    bool hasObjectOfType(int typeId) const;

    /**
     * @brief 从参数中提取 Vector3
     */
    Vector3 extractVector3(const QVariantMap& params, const QString& key, const Vector3& defaultValue);
};
