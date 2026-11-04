#pragma once

#include "WidgetDBSync.hpp"
#include <ModelOrientationWidgetDB.hpp>
#include <map>

// VTK components
#include <vtkActor.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkTransform.h>
#include <vtkSphereSource.h>

/**
 * @brief 模型方向控制器Widget的DBSync实现
 *
 * 三个轴向刻度盘和抓手始终显示并保持世界坐标方向；拖动某一轴时，
 * 只有当前轴的指针和圆球沿固定刻度盘旋转。
 */
class ModelOrientationWidgetDBSync : public WidgetDBSync {
public:
    ModelOrientationWidgetDBSync() = default;
    ~ModelOrientationWidgetDBSync() override = default;

    /**
     * @brief 获取默认渲染层级
     * 世界坐标交互控件使用 OVERLAY 层（共享主相机，覆盖主几何层）
     */
    RenderLayer getDefaultRenderLayer() const override {
        return RenderLayer::OVERLAY;
    }

    /**
     * @brief 清理Widget资源
     * 重写基类方法以从主渲染器中移除actors
     */
    void cleanup(const DBInstanceID& id) override;

    // === Widget识别接口重写 ===

    /**
     * @brief 标识此DBSync为Widget
     */
    bool isWidget() const override {
        return true;
    }

    /**
     * @brief 识别Widget的具体部分
     * @param actor 被pick的Actor
     * @return 部件ID，-1表示无法识别
     */
    int identifyWidgetPart(vtkSmartPointer<vtkActor> actor) const override;

    /**
     * @brief 判断某个Prop是否属于此Widget
     * @param prop 被检查的VTK Prop
     * @return true如果此Prop属于当前Widget
     */
    bool ownsProp(vtkSmartPointer<vtkProp> prop) const override;

    // === Widget部件高亮实现 ===

    /**
     * @brief 高亮Widget的特定部分
     * @param partId 部件ID（RING_X, RING_Y, RING_Z）
     * @param style 高亮样式
     * @return true如果成功高亮
     */
    bool highlightPart(int partId, const VisualStyle& style) override;

    /**
     * @brief 清除Widget特定部分的高亮
     * @param partId 部件ID
     * @return true如果成功清除
     */
    bool clearPartHighlight(int partId) override;

protected:
    /**
     * @brief 创建方向控制器Widget组件
     */
    void createWidget() override;

    /**
     * @brief 响应Widget属性变化
     */
    void onWidgetPropertyChanged(const std::string& propertyName) override;

    /**
     * @brief 应用Widget上下文设置
     */
    void applyWidgetContext() override;

private:
    // === Widget部件识别 ===

    /**
     * @brief Widget部件ID枚举
     */
    enum PartId {
        NONE = -1,
        RING_X = 1,  // X轴旋转抓手
        RING_Y = 2,  // Y轴旋转抓手
        RING_Z = 3   // Z轴旋转抓手
    };

    /**
     * @brief Actor到部件ID的映射表
     */
    mutable std::map<vtkActor*, int> m_actorToPartMap;

    // === Orca dial 组件 ===
    // 三轴外圈刻度、连接线与抓手常驻；激活轴额外显示吸附参考和指针。
    vtkSmartPointer<vtkPolyData> m_scaleGeometry[3];
    vtkSmartPointer<vtkPolyDataMapper> m_scaleMappers[3];
    vtkSmartPointer<vtkActor> m_scaleActors[3];

    vtkSmartPointer<vtkPolyData> m_snapGeometry[3];
    vtkSmartPointer<vtkPolyDataMapper> m_snapMappers[3];
    vtkSmartPointer<vtkActor> m_snapActors[3];

    vtkSmartPointer<vtkPolyData> m_referenceGeometry[3];
    vtkSmartPointer<vtkPolyDataMapper> m_referenceMappers[3];
    vtkSmartPointer<vtkActor> m_referenceActors[3];

    vtkSmartPointer<vtkPolyData> m_grabberConnectionGeometry[3];
    vtkSmartPointer<vtkPolyDataMapper> m_grabberConnectionMappers[3];
    vtkSmartPointer<vtkActor> m_grabberConnectionActors[3];

    vtkSmartPointer<vtkSphereSource> m_grabberSources[3];
    vtkSmartPointer<vtkPolyDataMapper> m_grabberMappers[3];
    vtkSmartPointer<vtkActor> m_grabberActors[3];

    // 固定世界坐标架：刻度盘、连接线和未激活抓手不跟随模型旋转。
    vtkSmartPointer<vtkTransform> m_widgetTransform;

    // 每轴独立指针变换：只有激活轴的指针和圆球应用当前拖动角度。
    vtkSmartPointer<vtkTransform> m_indicatorTransforms[3];

    // === 参数 ===

    double m_dialRadius = 3.0;

    // === 私有方法 ===

    void createDialActors(int axis);

    void updateDialGeometry(
        int axis,
        std::shared_ptr<const ModelOrientationWidgetDB> widgetDB);

    /**
     * @brief 更新Widget的位置和大小
     */
    void updateTransform();

    /**
     * @brief 更新旋转抓手和刻度的视觉属性
     */
    void updateVisualProperties();

    /**
     * @brief 更新可见性
     */
    void updateVisibility();

    /**
     * @brief 建立Actor到部件ID的映射关系
     */
    void buildActorPartMapping();

    /**
     * @brief 根据部件ID获取对应的Actor
     * @param partId 部件ID
     * @return 对应的Actor，如果未找到返回nullptr
     */
    vtkActor* getActorForPart(int partId) const;

    // === Widget部件高亮状态管理 ===

    /**
     * @brief 存储部件原始样式
     */
    std::map<int, vtkSmartPointer<vtkProperty>> m_originalPartProperties;
};
