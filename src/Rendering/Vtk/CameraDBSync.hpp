#pragma once

#include "IDBSync.hpp"
#include <CameraDB.hpp>
#include <vtkCamera.h>
#include <vtkSmartPointer.h>
#include "Foundation/Log.h"

/**
 * @brief Camera专用DBSync - 同步CameraDB到VTK相机
 * 
 * 设计理念：
 * - 专门处理 CameraDB 到 vtkCamera 的属性同步
 * - 解决VTK相机固定不变的问题
 * - 使用RenderThread确保线程安全的VTK操作
 * - 支持所有相机属性：位置、目标、上向量、投影参数等
 * 
 * 核心功能：
 * - Position、Target、UpVector 实时同步到VTK相机
 * - 投影参数同步：FOV、近远裁剪面、投影模式
 * - 轨道相机参数的正确应用
 */
class CameraDBSync : public IDBSync, public std::enable_shared_from_this<CameraDBSync> {
public:
    CameraDBSync() = default;
    virtual ~CameraDBSync() = default;

    // === 实现IDBSync接口 ===
    
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
    
    vtkSmartPointer<vtkObject> getPrimaryVTKObject() const override {
        return m_vtkCamera;
    }
    
    RenderLayer getDefaultRenderLayer() const override {
        return RenderLayer::BACKGROUND;  // Camera在背景层，不直接渲染
    }
    
    bool needsRenderingContext() const override {
        return false;  // Camera不需要材质等渲染上下文
    }

private:
    // === VTK对象 ===
    vtkSmartPointer<vtkCamera> m_vtkCamera;
    
    // === CameraDB属性同步方法 ===
    void syncCameraPosition();
    void syncCameraTarget();
    void syncCameraUpVector();
    void syncProjectionParameters();
    void syncClippingPlanes();
    
    // === 轨道相机同步 ===
    void syncOrbitParameters();

    // === 动态裁剪面更新 ===
    void updateClippingPlanesFromScene();

    // === 辅助方法 ===
    void syncAllCameraProperties();
    void logCameraState(const std::string& context) const;
    
    // === 线程安全的VTK相机操作 ===
    void updateVTKCameraThreadSafe(std::function<void(vtkCamera*)> operation);
};
