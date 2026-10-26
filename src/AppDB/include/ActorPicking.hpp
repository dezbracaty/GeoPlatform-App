#pragma once

#include "ActorDB.hpp"
#include <functional>
#include <mutex>
#include <vtkSmartPointer.h>

class vtkPolyData;
class vtkStaticCellLocator;

namespace ActorPicking {

// Reusable surface query and cache, composed by actors without changing their
// inheritance. Each actor still explicitly implements ActorDB's pick contract.
class PolyDataRayPicker final {
public:
    using GeometryFactory = std::function<vtkSmartPointer<vtkPolyData>()>;
    PolyDataRayPicker();
    ~PolyDataRayPicker();

    std::optional<ActorDB::PickHit> intersect(
        const ActorDB& actor, const PropertyMap& properties,
        const GeometryFactory& createGeometry, const Vector3& origin,
        const Vector3& direction, float maxDistance) const;
    void invalidate();

private:
    mutable std::mutex m_pickMutex;
    mutable vtkSmartPointer<vtkStaticCellLocator> m_pickLocator;
    mutable PropertyMap m_pickProperties;
};

// For presentation-only actors with Pickable=false. Warn if a caller enables
// their flag contrary to that policy, rather than silently accepting it.
std::optional<ActorDB::PickHit> rejectNonPickable(const ActorDB& actor);

} // namespace ActorPicking
