#pragma once

#include "WidgetDBSync.hpp"
#include <SelectionBoxWidgetDB.hpp>
#include <vtkActor.h>
#include <vtkPolyDataMapper.h>

/**
 * @brief 在 OVERLAY 层渲染选中模型的白色角点包围框。
 */
class SelectionBoxWidgetDBSync : public WidgetDBSync {
public:
    RenderLayer getDefaultRenderLayer() const override {
        return RenderLayer::OVERLAY;
    }

    bool isWidget() const override {
        return true;
    }

protected:
    void createWidget() override;
    void onWidgetPropertyChanged(const std::string& propertyName) override;
    void onVisibilityChanged(bool visible) override;

private:
    void rebuildGeometry(const std::shared_ptr<const SelectionBoxWidgetDB>& widgetDB);
    void updateVisibility(const std::shared_ptr<const SelectionBoxWidgetDB>& widgetDB);

    vtkSmartPointer<vtkActor> m_boxActor;
    vtkSmartPointer<vtkPolyDataMapper> m_boxMapper;
};
