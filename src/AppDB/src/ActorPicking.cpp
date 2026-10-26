#include "ActorPicking.hpp"
#include "Foundation/Log.h"
#include <unordered_set>
#include <Eigen/LU>
#include <algorithm>
#include <cmath>
#include <vtkGenericCell.h>
#include <vtkPolyData.h>
#include <vtkStaticCellLocator.h>

namespace ActorPicking {

PolyDataRayPicker::PolyDataRayPicker() = default;
PolyDataRayPicker::~PolyDataRayPicker() = default;

void PolyDataRayPicker::invalidate() {
    std::lock_guard<std::mutex> lock(m_pickMutex);
    m_pickLocator = nullptr;
}

std::optional<ActorDB::PickHit> PolyDataRayPicker::intersect(
    const ActorDB& actor, const PropertyMap& properties,
    const GeometryFactory& createGeometry, const Vector3& origin,
    const Vector3& direction, float maxDistance) const {
    if (!actor.isPickable()) return std::nullopt;
    const Eigen::Vector3d worldOrigin(origin.x, origin.y, origin.z);
    Eigen::Vector3d worldDirection(direction.x, direction.y, direction.z);
    if (!worldOrigin.allFinite() || !worldDirection.allFinite() ||
        worldDirection.squaredNorm() == 0.0 || !(maxDistance > 0.0f))
        return std::nullopt;
    worldDirection.normalize();
    const Eigen::Matrix4d matrix = actor.getTransformMatrix().cast<double>();
    if (!matrix.allFinite() || matrix.block<3, 3>(0, 0).determinant() == 0.0)
        return std::nullopt;
    const Eigen::Matrix4d inverse = matrix.inverse();
    const Eigen::Vector3d localOrigin =
        (inverse * worldOrigin.homogeneous()).head<3>();
    // Keep the ray parameter in world units under nonuniform/mirrored scales.
    const Eigen::Vector3d localDirection =
        inverse.block<3, 3>(0, 0) * worldDirection;
    if (!localOrigin.allFinite() || !localDirection.allFinite()) return std::nullopt;

    vtkSmartPointer<vtkStaticCellLocator> locator;
    // Undo, deserialization and generic property writes can bypass typed setters.
    {
        std::lock_guard<std::mutex> lock(m_pickMutex);
        if (!m_pickLocator || m_pickProperties != properties) {
            const auto geometry = createGeometry();
            if (!geometry || geometry->GetNumberOfCells() == 0) return std::nullopt;
            m_pickLocator = vtkSmartPointer<vtkStaticCellLocator>::New();
            m_pickLocator->SetDataSet(geometry);
            m_pickLocator->BuildLocator();
            m_pickProperties = properties;
        }
        locator = m_pickLocator;
    }
    // Bound an infinite query by the farthest possible point in the actor.
    double bounds[6];
    locator->GetDataSet()->GetBounds(bounds);
    double reach = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const Eigen::Vector4d local(bounds[(corner & 1) ? 1 : 0],
                                    bounds[(corner & 2) ? 3 : 2],
                                    bounds[(corner & 4) ? 5 : 4], 1);
        const Eigen::Vector3d world = (matrix * local).head<3>();
        reach = std::max(reach, (world - worldOrigin).norm());
    }
    reach += 1.0;
    const double limit = std::min(double(maxDistance), reach);
    const Eigen::Vector3d end = localOrigin + localDirection * limit;
    if (!std::isfinite(limit) || !end.allFinite()) return std::nullopt;
    double first[3]{localOrigin.x(), localOrigin.y(), localOrigin.z()};
    double last[3]{end.x(), end.y(), end.z()};
    double fraction = 0, position[3], coordinates[3];
    int subId = 0;
    vtkIdType cellId = -1;
    const auto cell = vtkSmartPointer<vtkGenericCell>::New();
    if (!locator->IntersectWithLine(first, last, 1.e-7, fraction, position,
                                   coordinates, subId, cellId, cell)) return std::nullopt;
    const float distance = float(fraction * limit);
    if (!std::isfinite(distance) || distance < 0 || distance > maxDistance)
        return std::nullopt;
    const Eigen::Vector3d world = worldOrigin + worldDirection * distance;
    return ActorDB::PickHit{distance, {float(world.x()), float(world.y()), float(world.z())},
                   {float(position[0]), float(position[1]), float(position[2])},
                   std::nullopt, std::nullopt};
}

std::optional<ActorDB::PickHit> rejectNonPickable(const ActorDB& actor) {
    if (actor.isPickable()) {
        static std::mutex mutex;
        static std::unordered_set<TypeID> warnedTypes;
        bool first;
        {
            std::lock_guard<std::mutex> lock(mutex);
            first = warnedTypes.insert(actor.getTypeID()).second;
        }
        if (first) {
            LOG_WARN("Presentation-only actor type={} id={} has Pickable=true; "
                     "this type does not participate in actor surface picking",
                     static_cast<unsigned>(actor.getTypeID()), actor.getDBInstanceID().toString());
        }
    }
    return std::nullopt;
}

} // namespace ActorPicking
