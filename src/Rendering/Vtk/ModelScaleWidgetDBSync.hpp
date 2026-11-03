#pragma once

#include "WidgetDBSync.hpp"
#include <ModelScaleWidgetDB.hpp>

#include <array>
#include <map>

#include <vtkActor.h>
#include <vtkCellArray.h>
#include <vtkCubeSource.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>

class ModelScaleWidgetDBSync : public WidgetDBSync {
public:
    ModelScaleWidgetDBSync() = default;
    ~ModelScaleWidgetDBSync() override = default;

    RenderLayer getDefaultRenderLayer() const override {
        return RenderLayer::OVERLAY;
    }

    void cleanup(const DBInstanceID& id) override;

    bool isWidget() const override {
        return true;
    }

    int identifyWidgetPart(vtkSmartPointer<vtkActor> actor) const override;
    bool ownsProp(vtkSmartPointer<vtkProp> prop) const override;

    bool highlightPart(int partId, const VisualStyle& style) override;
    bool clearPartHighlight(int partId) override;

protected:
    void createWidget() override;
    void onWidgetPropertyChanged(const std::string& propertyName) override;
    void applyWidgetContext() override;

private:
    enum PartId {
        NONE = -1,
        GRABBER_X_MIN = 0,
        GRABBER_X_MAX = 1,
        GRABBER_Y_MIN = 2,
        GRABBER_Y_MAX = 3,
        GRABBER_Z_MIN = 4,
        GRABBER_Z_MAX = 5,
        GRABBER_UNIFORM_XMIN_YMIN = 6,
        GRABBER_UNIFORM_XMAX_YMIN = 7,
        GRABBER_UNIFORM_XMAX_YMAX = 8,
        GRABBER_UNIFORM_XMIN_YMAX = 9,
        GRABBER_COUNT = 10
    };

    static constexpr int kConnectionCount = 4;

    mutable std::map<vtkActor*, int> m_actorToPartMap;
    std::map<int, vtkSmartPointer<vtkProperty>> m_originalPartProperties;

    std::array<vtkSmartPointer<vtkCubeSource>, GRABBER_COUNT> m_grabberSources;
    std::array<vtkSmartPointer<vtkPolyDataMapper>, GRABBER_COUNT> m_grabberMappers;
    std::array<vtkSmartPointer<vtkActor>, GRABBER_COUNT> m_grabberActors;

    std::array<vtkSmartPointer<vtkPolyData>, kConnectionCount> m_connectionGeometry;
    std::array<vtkSmartPointer<vtkPolyDataMapper>, kConnectionCount> m_connectionMappers;
    std::array<vtkSmartPointer<vtkActor>, kConnectionCount> m_connectionActors;

    bool m_actorsAddedToRenderer = false;

    void createGrabberGeometry(int part);
    void createConnectionGeometry(int edge);
    void createAllParts(std::shared_ptr<const ModelScaleWidgetDB> widgetDB);
    void updateComponentPositions(std::shared_ptr<const ModelScaleWidgetDB> widgetDB);
    void updateVisualProperties();
    void updateVisibility();
    void updateTransformAndAddToRenderer();
    void buildActorPartMapping();

    static bool isEnabledPart(int partId, const ModelScaleWidgetDB& widgetDB);
    static int axisForPart(int partId);
    static Vector3 grabberPosition(int partId, const ModelScaleWidgetDB& widgetDB);
    static Color colorForPart(int partId, const ModelScaleWidgetDB& widgetDB, bool active);
    static Color hoverColorForPart(int partId, const ModelScaleWidgetDB& widgetDB);
    static double fixedGrabberWorldSize(const Vector3& position, vtkRenderer* renderer, const ModelScaleWidgetDB& widgetDB);
    vtkActor* getActorForPart(int partId) const;
};
