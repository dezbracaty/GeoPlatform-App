#pragma once

#include "IDBSync.hpp"

#include <ModelGeometryDB.hpp>

#include <vtkSmartPointer.h>

#include <memory>
#include <vector>

class vtkActor;
class vtkObject;
class vtkPolyDataMapper;

/**
 * Temporary per-Part presentation for one cut preview session.
 *
 * Geometry remains owned by ModelGeometryDB and is retained through immutable
 * ReadHandles. The preview owns only temporary VTK actors/mappers, avoiding any
 * shader or mapper mutation on the source ModelInstance presentation.
 */
class CutPreviewDBSync final
    : public IDBSync,
      public std::enable_shared_from_this<CutPreviewDBSync> {
public:
    void initialize(
        const DBInstanceID& id,
        vtkSmartPointer<vtkRenderer> renderer,
        std::shared_ptr<RenderThread> renderThread) override;
    void cleanup(const DBInstanceID& id) override;

    void update(
        const DBInstanceID& id,
        ChangeType changeType,
        const std::string& propertyName = {}) override;
    void update(
        const DBInstanceID& id,
        ChangeType changeType,
        const trans::Prop& prop) override;

    void setVisible(bool visible) override;
    vtkSmartPointer<vtkObject> getPrimaryVTKObject() const override;
    bool ownsProp(vtkSmartPointer<vtkProp>) const override { return false; }
    bool needsRenderingContext() const override { return false; }
    RenderLayer getDefaultRenderLayer() const override {
        return RenderLayer::GEOMETRY;
    }

private:
    struct PartPresentation {
        DBInstanceID partId{};
        DBInstanceID geometryId{};
        ModelGeometryDB::ReadHandle geometry;
        vtkSmartPointer<vtkActor> actor;
        vtkSmartPointer<vtkPolyDataMapper> mapper;
    };

    void refreshPresentation();
    bool rebuildParts(const DBInstanceID& sourceModelId);
    void clearParts();
    void setSourcePresentationVisible(bool visible);
    void installShader(PartPresentation& part);
    void updateShader(PartPresentation& part,
                      const class CutPlaneWidgetDB& widget);

    std::vector<PartPresentation> m_parts;
    DBInstanceID m_sourceModelId{INVALID_DB_ID};
    bool m_sourcePresentationHidden{false};
    bool m_visible{true};
};
