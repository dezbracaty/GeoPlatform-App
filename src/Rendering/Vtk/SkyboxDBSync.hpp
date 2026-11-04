#pragma once

#include "IDBSync.hpp"
#include <SkyboxDB.hpp>
#include <vtkSkybox.h>
#include <vtkTexture.h>
#include <vtkImageData.h>
#include <vtkSmartPointer.h>
#include "Foundation/Log.h"

#include <atomic>
#include <optional>
#include <unordered_set>

/**
 * @brief Skybox 专用 DBSync - 同步 SkyboxDB 到 VTK 天空盒
 *
 * 设计理念：
 * - 管理场景的天空盒和环境光照
 * - 支持程序化渐变、纯色和经纬图 HDR 纹理
 * - 提供 PBR 渲染所需的 IBL（基于图像的光照）
 *
 * 核心功能：
 * - 创建和管理 vtkSkybox
 * - 生成 CubeMap 纹理
 * - 配置渲染器的环境贴图
 */
class SkyboxDBSync : public IDBSync, public std::enable_shared_from_this<SkyboxDBSync> {
public:
    SkyboxDBSync() = default;
    virtual ~SkyboxDBSync() = default;

    // === 实现 IDBSync 接口 ===

    void initialize(
        const DBInstanceID& id,
        vtkSmartPointer<vtkRenderer> renderer,
        std::shared_ptr<RenderThread> renderThread) override;

    void cleanup(const DBInstanceID& id) override;

    void update(
        const DBInstanceID& id,
        ChangeType changeType,
        const std::string& propertyName = "") override;

    void update(
        const DBInstanceID& id,
        ChangeType changeType,
        const trans::Prop& prop) override;

    void setVisible(bool visible) override;
    void synchronizeView() override;

    vtkSmartPointer<vtkObject> getPrimaryVTKObject() const override {
        return m_skybox;
    }

    RenderLayer getDefaultRenderLayer() const override {
        return RenderLayer::BACKGROUND;  // 天空盒在背景层
    }

    bool needsRenderingContext() const override {
        return false;  // 天空盒不需要材质等渲染上下文
    }

private:
    // === VTK 对象 ===
    vtkSmartPointer<vtkSkybox> m_skybox;
    vtkSmartPointer<vtkTexture> m_environmentTexture;
    vtkSmartPointer<vtkImageData> m_hdrImage;
    vtkSmartPointer<vtkTexture> m_backgroundTexture;
    vtkSmartPointer<vtkImageData> m_backgroundImage;
    std::atomic_bool m_rebuildPending{false};
    DBInstanceID m_themeId;
    bool m_themeDiscovered{false};
    std::optional<std::string> m_appliedBackgroundKey;
    std::unordered_set<std::string> m_failedBackgroundKeys;

    // === 天空盒创建和更新方法 ===

    /**
     * @brief 创建渐变天空盒
     */
    void createGradientSkybox();

    /**
     * @brief 创建纯色背景
     */
    void createSolidColorBackground();

    /**
     * @brief 从 SkyboxDB 的稳定资源 URI 创建球形 HDR 天空盒
     */
    bool createHdrSkybox();
    bool darkTheme();
    void updateHdrBackground();
    void scheduleBackgroundUpdate();

    /**
     * @brief 按当前 DB 快照完整重建天空盒和 IBL 投影
     */
    void rebuildSkybox();

    /**
     * @brief 合并同一批 DB 属性通知，只执行一次昂贵的 HDR 重建
     */
    void scheduleSkyboxRebuild();

    /**
     * @brief 更新天空盒颜色
     */
    void updateSkyboxColors();

    /**
     * @brief 更新 IBL 设置
     */
    void updateIBLSettings();

    // === 辅助方法 ===

    /**
     * @brief 创建渐变 CubeMap 面（简单垂直渐变）
     * @param r1, g1, b1 顶部颜色 (0-255)
     * @param r2, g2, b2 底部颜色 (0-255)
     */
    vtkSmartPointer<vtkImageData> createGradientCubeFace(
        unsigned char r1, unsigned char g1, unsigned char b1,
        unsigned char r2, unsigned char g2, unsigned char b2);

    /**
     * @brief 创建纯色 CubeMap 面
     * @param r, g, b 颜色 (0-255)
     */
    vtkSmartPointer<vtkImageData> createSolidCubeFace(
        unsigned char r, unsigned char g, unsigned char b);

    /**
     * @brief 创建径向渐变 CubeMap 面（从中心向外渐变）
     * @param centerR, centerG, centerB 中心颜色 (0-255)
     * @param edgeR, edgeG, edgeB 边缘颜色 (0-255)
     */
    vtkSmartPointer<vtkImageData> createRadialGradientCubeFace(
        unsigned char centerR, unsigned char centerG, unsigned char centerB,
        unsigned char edgeR, unsigned char edgeG, unsigned char edgeB);

    /**
     * @brief 创建侧面渐变（考虑Z-up坐标系）
     * @param topColor 顶部颜色（天空）
     * @param bottomColor 底部颜色（地平线）
     * @param groundColor 地面颜色
     * @param horizonRatio 地平线位置比例 (0-1)
     */
    vtkSmartPointer<vtkImageData> createSideFaceGradient(
        const Vector3& topColor,
        const Vector3& bottomColor,
        const Vector3& groundColor,
        float horizonRatio = 0.5f);

    /**
     * @brief 线程安全的 VTK 操作
     */
    void dispatchVTKOperation(std::function<void()> operation);
};
