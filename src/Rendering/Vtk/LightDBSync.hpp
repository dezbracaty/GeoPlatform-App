#pragma once

#include "IDBSync.hpp"
#include <LightDB.hpp>
#include <vtkLight.h>
#include <vtkSmartPointer.h>
#include "Foundation/Log.h"

/**
 * @brief LightDBSync - 灯光数据库同步类
 *
 * 同步 LightDB 到 VTK vtkLight 对象。
 *
 * 设计模式：管理模式（与 CameraDBSync 的引用模式不同）
 * - initialize: 创建 vtkLight 并 AddLight 到渲染器
 * - cleanup: RemoveLight 从渲染器并销毁
 * - 灯光生命周期由 DBSync 管理
 *
 * 关键区别：
 * - 灯光没有渲染层级概念（全局生效）
 * - 灯光不参与 Pick（不需要渲染上下文）
 * - 使用 Switch 而非 Visibility 控制开关
 */
class LightDBSync : public IDBSync, public std::enable_shared_from_this<LightDBSync> {
public:
    LightDBSync() = default;
    virtual ~LightDBSync() = default;

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

    vtkSmartPointer<vtkObject> getPrimaryVTKObject() const override {
        return m_light;
    }

    // === LightDBSync 特殊设计 ===

    /**
     * @brief 灯光不需要渲染层级
     *
     * 灯光全局生效，不属于任何渲染层级。
     * 返回 GEOMETRY 只是为了满足接口要求。
     */
    RenderLayer getDefaultRenderLayer() const override {
        return RenderLayer::GEOMETRY;
    }

    /**
     * @brief 灯光不需要渲染上下文
     *
     * 灯光不参与 Pick，不需要材质等渲染上下文。
     */
    bool needsRenderingContext() const override {
        return false;
    }

private:
    // === VTK 对象 ===
    vtkSmartPointer<vtkLight> m_light;

    // === LightDB 属性同步方法 ===

    /**
     * @brief 同步灯光类型
     *
     * HEADLIGHT: 设置为头灯模式
     * CAMERA_LIGHT: 设置为相机灯光模式
     * SCENE_LIGHT: 设置为场景灯光模式
     */
    void syncLightType();

    /**
     * @brief 同步灯光颜色
     */
    void syncColor();

    /**
     * @brief 同步灯光强度
     */
    void syncIntensity();

    /**
     * @brief 同步灯光开关状态
     */
    void syncEnabled();

    /**
     * @brief 同步灯光位置
     *
     * 仅对 CAMERA_LIGHT 和 SCENE_LIGHT 有效
     */
    void syncPosition();

    /**
     * @brief 同步灯光焦点
     *
     * 仅对 SCENE_LIGHT 有效
     */
    void syncFocalPoint();

    /**
     * @brief 同步所有属性
     */
    void syncAllProperties();

    /**
     * @brief 线程安全的 VTK 操作
     */
    void dispatchVTKOperation(std::function<void()> operation);
};
