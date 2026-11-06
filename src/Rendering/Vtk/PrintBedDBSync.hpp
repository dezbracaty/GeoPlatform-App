#pragma once

#include "ActorDBSync.hpp"
#include <PrintBedDB.hpp>
#include <PrintBedViewPolicy.hpp>
#include <vtkCubeSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkPlaneSource.h>
#include <vtkLineSource.h>
#include <vtkAppendPolyData.h>
#include <vtkSTLReader.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTexture.h>
#include <vtkTextureMapToPlane.h>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

/**
 * @brief 打印床的DBSync实现 - 使用新的模板方法模式
 *
 * 继承自ActorDBSync，实现模板方法模式:
 * - createActor(): 创建打印床的 VTK 对象（Bed3D 实体床板/坐标轴/打印体积）
 * - updateGeometry(): 响应几何属性变化
 * - 钩子函数: onMaterialChanged(), onRenderingContextApplied()
 *
 * 打印床由以下组件组成：
 * - Bed3D 实体床板 STL；没有模型资源时回退为薄平面
 * - Bed3D 坐标轴
 * - 可打印区域：半透明光墙效果的可打印区域边界
 *
 * 网格、轮廓和床面贴图是同一个真实打印床的渲染投影，由本同步器
 * 统一维护打印床本体、网格、贴图和碰撞边界。
 */
class PrintBedDBSync : public ActorDBSync {
public:
    PrintBedDBSync() = default;
    ~PrintBedDBSync() override = default;
    void synchronizeView() override;

    RenderLayer getDefaultRenderLayer() const override {
        // PrintBed 是场景几何体，需要光照渲染，应在 GEOMETRY 层
        return RenderLayer::GEOMETRY;
    }

protected:
    // === 纯虚函数实现（ActorDBSync要求） ===
    /**
     * @brief 创建打印床Actor和Source
     */
    void createActor() override;

    /**
     * @brief 初始化，确保可打印区域Actor也被添加到渲染器
     */
    void initialize(const DBInstanceID& id,
                    vtkSmartPointer<vtkRenderer> renderer,
                    std::shared_ptr<RenderThread> renderThread) override;

    /**
     * @brief 清理，确保可打印区域Actor也被移除
     */
    void cleanup(const DBInstanceID& id) override;

    /**
     * @brief 更新打印床几何属性
     */
    void updateGeometry( const std::string& prop) override;

    // === 钩子函数重写（可选扩展） ===
    /**
     * @brief 响应材质变化的钩子
     */
    void onMaterialChanged( const std::string& prop) override;
    void onTransformChanged(const std::string& prop) override;

    /**
     * @brief 响应渲染上下文变化的钩子
     */
    void onRenderingContextApplied(const RenderingContext& context) override;

    /**
     * @brief 响应可见性变化的钩子，确保附属 actor 同步显隐
     */
    void onVisibilityChanged(bool visible) override;

    /**
     * @brief 处理其他变化类型（如 PROPERTY_CHANGED）
     */
    void onOtherChangeType(ChangeType changeType, const std::string& prop) override;

    bool shouldApplyMaterial() const override { return false; }
    bool isGeometryProperty(std::string_view prop) const override;

private:
    GPlatform::Rendering::PrintBedViewPolicy m_viewPolicy;
    /**
     * @brief 更新类型枚举 - 用于选择性几何更新优化
     */
    enum class UpdateType {
        PLATFORM_ONLY,      // 仅更新平台（尺寸、厚度、位置变化）
        PRINT_VOLUME_ONLY,  // 仅更新可打印区域（显示状态、高度变化）
        COLLISION_ONLY,     // 仅更新碰撞墙面（碰撞状态、强度变化）
        BED_RESOURCE_RELOAD,// 重新加载机型专属床板 STL
        FULL_REBUILD        // 完全重建（多个组件同时变化）
    };

    /**
     * @brief 根据属性变化确定更新类型
     */
    UpdateType determineUpdateType(const std::string& prop);

    /**
     * @brief 更新打印床的几何尺寸和属性（优化版本）
     */
    void updatePrintBedGeometry();

    /** Recreate the primary bed actor when the selected machine changes STL. */
    void reloadBedVisualResources();

    /**
     * @brief 选择性更新平台几何体
     */
    void updatePlatformGeometry();

    /**
     * @brief 选择性更新可打印区域几何体
     */
    void updatePrintVolumeGeometry();

    /**
     * @brief 创建打印床平台几何体
     */
    vtkSmartPointer<vtkPolyData> createPlatformGeometry();

    /**
     * @brief 尝试创建 FlashForge/Orca 风格实体床板模型
     */
    bool createFlashForgeBedModel(const PrintBedDB& printBedDB);

    /**
     * @brief 根据 DB 尺寸更新床板模型的位置
     */
    void updateBedVisualGeometry(const PrintBedDB& printBedDB);

    std::vector<vtkSmartPointer<vtkActor>> createAxisActors();

    /**
     * @brief 创建可打印区域盒子几何体（半透明，在Z=0以上）
     */
    vtkSmartPointer<vtkPolyData> createPrintVolumeGeometry();

    vtkSmartPointer<vtkPolyData> createGridGeometry(bool majorOnly) const;
    bool syncBedTextureActor(const PrintBedDB& printBedDB);
    void createBedTextureActor(
        const PrintBedDB& printBedDB,
        const std::filesystem::path& texturePath);
    void updateBedTextureGeometry(const PrintBedDB& printBedDB);
    void rebuildBedDecorations(const PrintBedDB& printBedDB);
    void clearBedDecorations();
    void syncAuxiliaryTransforms();
    void configureGridActor(vtkActor* actor, bool major) const;

    /**
     * @brief 验证几何体有效性
     */
    bool validateGeometry(vtkPolyData* geometry, const std::string& component);

    // === 碰撞墙面渲染方法 ===

    /**
     * @brief 更新碰撞墙面效果（根据碰撞状态）
     */
    void updateCollisionWalls();

    /**
     * @brief 创建单个碰撞墙面几何体
     * @param wallIndex 墙面索引 (0=LEFT, 1=RIGHT, 2=FRONT, 3=BACK, 4=TOP, 5=BOTTOM)
     */
    vtkSmartPointer<vtkPolyData> createCollisionWallGeometry(int wallIndex);

    vtkSmartPointer<vtkPolyData> createCollisionWallBorderGeometry(
        vtkPolyData* wallGeometry);

    /**
     * @brief 应用碰撞墙面视觉效果
     * @param actor 墙面Actor
     */
    void applyCollisionWallEffect(vtkSmartPointer<vtkActor> actor);

    void applyCollisionWallBorderEffect(vtkSmartPointer<vtkActor> actor);

    /**
     * @brief 清理所有碰撞墙面
     */
    void clearCollisionWalls();

private:
    // === 打印床主要组件 ===
    vtkSmartPointer<vtkPlaneSource> m_platformSource;     // 回退平面平台（基类 m_actor 使用）
    vtkSmartPointer<vtkSTLReader> m_bedModelReader;       // FlashForge 实体床板 STL
    vtkSmartPointer<vtkTransform> m_bedModelTransform;    // STL 尺寸适配变换
    vtkSmartPointer<vtkTransformPolyDataFilter> m_bedModelTransformFilter;
    std::vector<vtkSmartPointer<vtkActor>> m_axisActors;  // Orca-style XYZ 小坐标轴
    vtkSmartPointer<vtkActor> m_printVolumeActor;         // 可打印区域盒子Actor，光墙效果
    vtkSmartPointer<vtkActor> m_gridActor;
    vtkSmartPointer<vtkActor> m_majorGridActor;
    vtkSmartPointer<vtkActor> m_bedTextureActor;
    vtkSmartPointer<vtkPlaneSource> m_bedTexturePlaneSource;
    vtkSmartPointer<vtkTextureMapToPlane> m_bedTextureMap;
    vtkSmartPointer<vtkTexture> m_bedTexture;
    std::string m_bedTexturePath;

    // === 碰撞墙面渲染组件 ===
    std::array<vtkSmartPointer<vtkActor>, 6> m_collisionWallActors;  // 6个墙面的Actor
    std::array<vtkSmartPointer<vtkActor>, 6> m_collisionWallBorderActors;
};
