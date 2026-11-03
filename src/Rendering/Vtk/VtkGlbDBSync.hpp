#pragma once

#include "IDBSync.hpp"
#include <GlbAssetData.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

class vtkActor;
class vtkAssembly;
class vtkGLTFImporter;
class vtkMemoryResourceStream;

class VtkGlbDBSync final : public IDBSync,
                           public std::enable_shared_from_this<VtkGlbDBSync> {
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
    void applyRenderingContext() override;

    vtkSmartPointer<vtkObject> getPrimaryVTKObject() const override;
    bool ownsProp(vtkSmartPointer<vtkProp> prop) const override;

private:
    void reloadFromDB();
    void synchronizeVisibilityFromDB();
    void releaseNativeScene();
    void applyModelState();
    void applyTransform();
    void applyPresentationContext();

    vtkSmartPointer<vtkGLTFImporter> m_importer;
    vtkSmartPointer<vtkMemoryResourceStream> m_stream;
    vtkSmartPointer<vtkAssembly> m_assembly;
    std::vector<vtkSmartPointer<vtkActor>> m_actors;
    std::vector<double> m_authoredOpacities;
    std::array<double, 3> m_sourceCenter{};
    std::shared_ptr<const GlbAssetSnapshot> m_sourceSnapshot;
    std::uint64_t m_loadedRevision{0};
};
