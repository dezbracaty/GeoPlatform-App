#pragma once

#include "WidgetDBSync.hpp"
#include <ModelTranslateWidgetDB.hpp>
#include <map>

// VTK components
#include <vtkActor.h>
#include <vtkPolyData.h>
#include <vtkConeSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>

/**
 * @brief 模型位移控制器Widget的DBSync实现
 *
 * Orca 风格设计：三个轴向 grabber（虚线连接 + 末端锥体）
 * - X轴 grabber - 红色，沿X轴方向拖拽
 * - Y轴 grabber - 绿色，沿Y轴方向拖拽
 * - Z轴 grabber - 蓝色，沿Z轴方向拖拽
 *
 * 设计原则：
 * - 连接线从模型包围盒中心出发
 * - grabber 位于包围盒 max 边界外侧
 * - 仅 grabber 参与 pick，与 Orca 的 use_grabbers 行为一致
 */
class ModelTranslateWidgetDBSync : public WidgetDBSync {
public:
    ModelTranslateWidgetDBSync() = default;
    ~ModelTranslateWidgetDBSync() override = default;

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
     * @param partId 部件ID（X_AXIS, Y_AXIS, Z_AXIS）
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
     * @brief 创建位移控制器Widget组件
     */
    void createWidget() override;

    /**
     * @brief 响应Widget属性变化
     */
    void onWidgetPropertyChanged(
                                 const std::string& propertyName) override;

    /**
     * @brief 应用Widget上下文设置
     */
    void applyWidgetContext() override;

private:
    // === Widget部件识别 ===

    /**
     * @brief Widget部件ID枚举（对应TranslateMode）
     */
    enum PartId {
        NONE = -1,
        X_AXIS = 1,        // X轴箭头 - 红色
        Y_AXIS = 2,        // Y轴箭头 - 绿色
        Z_AXIS = 3         // Z轴箭头 - 蓝色
        // CENTER_SPHERE 已移除
    };

    /**
     * @brief Actor到部件ID的映射表
     */
    mutable std::map<vtkActor*, int> m_actorToPartMap;

    // === Orca move 组件 (3个轴向) ===

    vtkSmartPointer<vtkPolyData> m_connectionGeometry[3];
    vtkSmartPointer<vtkPolyDataMapper> m_connectionMappers[3];
    vtkSmartPointer<vtkActor> m_connectionActors[3];

    vtkSmartPointer<vtkConeSource> m_grabberSources[3];
    vtkSmartPointer<vtkPolyDataMapper> m_grabberMappers[3];
    vtkSmartPointer<vtkActor> m_grabberActors[3];

    // === 参数 ===

    double m_grabberHeight = 8.0;        // 末端锥体高度
    double m_grabberRadius = 3.0;        // 末端锥体底面半径
    double m_connectionLineWidth = 2.0;  // 轴向虚线宽度
    int m_resolution = 16;               // 几何分辨率

    // === 标记状态 ===

    /**
     * @brief 标记Actor是否已添加到渲染器
     */
    bool m_actorsAddedToRenderer = false;

    // === 私有方法 ===

    /**
     * @brief 创建 Orca 风格轴向 grabber 几何
     */
    void createGrabberGeometry(int axis);

    /**
     * @brief 创建中心到 grabber 的虚线连接线
     */
    void createConnectionGeometry(int axis);

    /**
     * @brief 创建所有轴向组件
     * @param widgetDB ModelTranslateWidgetDB实例
     */
    void createAllAxes(std::shared_ptr<const ModelTranslateWidgetDB> widgetDB);

    /**
     * @brief 更新所有组件的位置
     * @param widgetDB ModelTranslateWidgetDB实例
     */
    void updateComponentPositions(std::shared_ptr<const ModelTranslateWidgetDB> widgetDB);

    /**
     * @brief 更新 grabber 和虚线位置
     * @param widgetDB ModelTranslateWidgetDB实例
     */
    void updateAxisPositions(std::shared_ptr<const ModelTranslateWidgetDB> widgetDB);

    /**
     * @brief 更新Widget变换并添加到渲染器（创建时使用）
     */
    void updateTransformAndAddToRenderer(std::shared_ptr<const ModelTranslateWidgetDB> widgetDB);

    /**
     * @brief 更新所有组件的视觉属性
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
