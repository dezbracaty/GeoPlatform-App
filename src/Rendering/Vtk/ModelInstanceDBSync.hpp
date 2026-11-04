#pragma once

#include "IDBSync.hpp"
#include <ModelGeometryDB.hpp>
#include <SurfaceColorRenderProjection.hpp>

#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class vtkActor;
class vtkAssembly;
class vtkGLTFImporter;
class vtkMemoryResourceStream;
class vtkPolyDataMapper;
class vtkPolyData;
class vtkLookupTable;

namespace GPlatform {
class SlicingConfigDB;
}

class ModelInstanceDBSync final
    : public IDBSync,
      public std::enable_shared_from_this<ModelInstanceDBSync> {
public:
    void initialize(const DBInstanceID& id,
                    vtkSmartPointer<vtkRenderer> renderer,
                    std::shared_ptr<RenderThread> renderThread) override;
    void cleanup(const DBInstanceID& id) override;
    void update(const DBInstanceID& id, ChangeType changeType,
                const std::string& propertyName = {}) override;
    void update(const DBInstanceID& id, ChangeType changeType,
                const trans::Prop& prop) override;
    void setVisible(bool visible) override;
    vtkSmartPointer<vtkObject> getPrimaryVTKObject() const override;
    bool ownsProp(vtkSmartPointer<vtkProp> prop) const override;
    DBInstanceID partIdForProp(
        vtkSmartPointer<vtkProp> prop) const override;
    void applyRenderingContext() override;

private:
    std::vector<std::shared_ptr<const class ModelPartDB>> partsForInstance(
        const class ModelInstanceDB& instance) const;
    std::shared_ptr<const class ModelObjectDB> objectForInstance(
        const class ModelInstanceDB& instance) const;
    std::shared_ptr<const ModelGeometryDB> geometryForPart(
        DBInstanceID partId) const;
    std::shared_ptr<const class MaterialDB> materialForPart(
        DBInstanceID partId) const;

    struct PartPresentation {
        DBInstanceID partId{};
        DBInstanceID geometryId{};
        DBInstanceID materialId{};
        DBInstanceID surfaceColorId{};
        ModelGeometryDB::ReadHandle geometry;
        vtkSmartPointer<vtkActor> actor;
        vtkSmartPointer<vtkPolyDataMapper> mapper;
        vtkSmartPointer<vtkPolyData> surfaceColorPolyData;
        vtkSmartPointer<vtkLookupTable> surfaceColorLookupTable;
        std::shared_ptr<const SurfaceColorRenderProjectionResult>
            surfaceColorProjection;
        std::vector<Color> surfaceColorPalette;
        int surfaceColorRevision{-1};
        bool surfaceColorPaletteInitialized{false};
        bool surfaceColorProjectionPending{false};
        DBInstanceID pendingSurfaceColorId{};
        int pendingSurfaceColorRevision{-1};
        std::uint64_t projectionToken{0};
    };

    void rebuildParts();
    bool buildNativeGlb(
        const class ModelInstanceDB& instance,
        std::shared_ptr<const struct GlbAssetSnapshot> snapshot);
    void releaseNativeGlb();
    void updateNativeGlbTransform(const class ModelInstanceDB& instance);
    void updateNativeGlbStyle(
        const class ModelInstanceDB& instance,
        bool updateVisibility = true);
    void updateInstanceTransform();
    void updatePartTransform(DBInstanceID partId);
    void updatePartGeometry(DBInstanceID partId);
    void updatePartMaterial(DBInstanceID partId);
    void updateInstanceStyle(bool updateVisibility = true);
    PartPresentation* findPartPresentation(DBInstanceID partId);
    const PartPresentation* findPartPresentation(DBInstanceID partId) const;
    std::shared_ptr<const class ModelSurfaceColorDB> findSurfaceColors(
        DBInstanceID partId) const;
    std::vector<Color> activeFilamentPalette() const;
    std::shared_ptr<const GPlatform::SlicingConfigDB>
    activeSlicingConfig() const;
    void scheduleSurfaceColorProjection(DBInstanceID partId);
    void applySurfaceColorProjection(
        DBInstanceID partId,
        std::uint64_t token,
        std::shared_ptr<const SurfaceColorRenderProjectionResult> projection,
        const std::vector<Color>& palette,
        DBInstanceID surfaceColorId,
        int revision);
    void applySurfaceColorPalette(
        DBInstanceID partId, const std::vector<Color>& palette,
        bool force = false);
    void clearSurfaceColorPresentation(DBInstanceID partId);

    std::vector<PartPresentation> m_parts;
    vtkSmartPointer<vtkGLTFImporter> m_nativeGlbImporter;
    vtkSmartPointer<vtkMemoryResourceStream> m_nativeGlbStream;
    vtkSmartPointer<vtkAssembly> m_nativeGlbAssembly;
    std::vector<vtkSmartPointer<vtkActor>> m_nativeGlbActors;
    std::vector<double> m_nativeGlbAuthoredOpacities;
    std::array<double, 3> m_nativeGlbSourceCenter{};
    std::shared_ptr<const struct GlbAssetSnapshot> m_nativeGlbSnapshot;
    DBInstanceID m_nativeGlbPartId{};
    std::atomic_uint64_t m_nextProjectionToken{1};
};
