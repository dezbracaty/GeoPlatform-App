#pragma once

#include "WidgetDBSync.hpp"

#include <CutPlaneWidgetDB.hpp>

#include <map>
#include <vector>

class vtkActor;
class vtkConeSource;
class vtkLineSource;
class vtkPlaneSource;
class vtkRegularPolygonSource;
class vtkSphereSource;

class CutPlaneWidgetDBSync final : public WidgetDBSync {
public:
    RenderLayer getDefaultRenderLayer() const override {
        return RenderLayer::OVERLAY;
    }

    bool isWidget() const override { return true; }
    bool ownsProp(vtkSmartPointer<vtkProp> prop) const override;
    int identifyWidgetPart(vtkSmartPointer<vtkActor> actor) const override;
    bool highlightPart(int partId, const VisualStyle& style) override;
    bool clearPartHighlight(int partId) override;

protected:
    void createWidget() override;
    void onWidgetPropertyChanged(const std::string& propertyName) override;
    void applyWidgetContext() override;

private:
    void createActors();
    void updateGeometryAndTransform();
    void updateStyle();
    void addActor(vtkSmartPointer<vtkActor> actor);
    void registerPickActor(vtkSmartPointer<vtkActor> actor,
                           CutPlaneWidgetDB::Part part);
    void registerVisualActor(vtkSmartPointer<vtkActor> actor,
                             CutPlaneWidgetDB::Part part);

    vtkSmartPointer<vtkPlaneSource> m_planeSource;
    vtkSmartPointer<vtkLineSource> m_normalLineSource;
    vtkSmartPointer<vtkConeSource> m_normalHandleSource;
    vtkSmartPointer<vtkRegularPolygonSource> m_xRingSource;
    vtkSmartPointer<vtkRegularPolygonSource> m_yRingSource;
    vtkSmartPointer<vtkSphereSource> m_xHandleSource;
    vtkSmartPointer<vtkSphereSource> m_yHandleSource;

    vtkSmartPointer<vtkActor> m_planeActor;
    vtkSmartPointer<vtkActor> m_normalLineActor;
    vtkSmartPointer<vtkActor> m_normalHandleActor;
    vtkSmartPointer<vtkActor> m_xRingActor;
    vtkSmartPointer<vtkActor> m_yRingActor;
    vtkSmartPointer<vtkActor> m_xHandleActor;
    vtkSmartPointer<vtkActor> m_yHandleActor;

    std::map<vtkActor*, int> m_pickParts;
    std::map<int, std::vector<vtkActor*>> m_visualParts;
};
