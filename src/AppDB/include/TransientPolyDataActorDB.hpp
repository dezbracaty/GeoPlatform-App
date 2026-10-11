#pragma once

#include "ActorDB.hpp"
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkStaticCellLocator.h>
#include <vtkGenericCell.h>
#include <Eigen/LU>
#include <algorithm>
#include <any>
#include <cmath>

/**
 * Runtime-only poly-data actor shared by interaction previews, derived
 * overlays and diagnostic visualization. It carries no domain state; its
 * geometry may always be discarded and rebuilt by its owner.
 */
class TransientPolyDataActorDB : public ActorDB {
public:
    TypeID getTypeID() const override {
        return TypeID::TRANSIENT_POLY_DATA_ACTOR_DB;
    }

    BoundingBox localBounds() const override {
        BoundingBox box;
        if (!m_geometry || m_geometry->GetNumberOfPoints() == 0) return box;
        double bounds[6];
        m_geometry->GetBounds(bounds);
        box.min = {static_cast<float>(bounds[0]), static_cast<float>(bounds[2]),
                   static_cast<float>(bounds[4])};
        box.max = {static_cast<float>(bounds[1]), static_cast<float>(bounds[3]),
                   static_cast<float>(bounds[5])};
        box.valid = true;
        return box;
    }

    // Pick the supplied geometry without knowing the interaction tool or domain.
    // Acceleration is prepared when geometry or Pickable changes, never by the query.
    std::optional<PickHit> intersectPickRay(const Vector3& origin, const Vector3& direction,
                                         float maxDistance) const override {
        const auto locator = m_pickLocator;
        if (!isPickable() || !locator || maxDistance <= 0 || std::isnan(maxDistance))
            return std::nullopt;
        const auto matrix = getTransformMatrix();
        if (!matrix.allFinite() || !Eigen::FullPivLU<Transform::Matrix4>(matrix).isInvertible())
            return std::nullopt;
        const Eigen::Vector3f worldOrigin(origin.x, origin.y, origin.z);
        Eigen::Vector3f worldDirection(direction.x, direction.y, direction.z);
        if (!worldOrigin.allFinite() || !worldDirection.allFinite()) return std::nullopt;
        const float directionLength = worldDirection.stableNorm();
        if (!std::isfinite(directionLength) || directionLength <= 1.e-12f)
            return std::nullopt;
        worldDirection /= directionLength;
        const auto bounds = worldBounds();
        if (!bounds.valid) return std::nullopt;
        const auto center = bounds.getCenter();
        const auto size = bounds.getSize();
        // Bound an otherwise infinite ray by the farthest possible geometry distance.
        const float reach = (worldOrigin - Eigen::Vector3f(center.x, center.y, center.z)).norm() +
                            Eigen::Vector3f(size.x, size.y, size.z).norm() + 1.f;
        const float limit = std::min(maxDistance, reach);
        if (!std::isfinite(limit) || limit <= 0) return std::nullopt;
        const auto inverse = matrix.inverse().eval();
        const Eigen::Vector3f localOrigin =
            (inverse * Eigen::Vector4f(origin.x, origin.y, origin.z, 1)).head<3>();
        // Keep the transformed direction unnormalized so limit remains a world distance,
        // including when the actor has nonuniform scale.
        const Eigen::Vector3f localDirection =
            inverse.block<3, 3>(0, 0) * worldDirection;
        const Eigen::Vector3f end = localOrigin + localDirection * limit;
        if (!localOrigin.allFinite() || !end.allFinite()) return std::nullopt;
        const double first[3]{localOrigin.x(), localOrigin.y(), localOrigin.z()};
        const double last[3]{end.x(), end.y(), end.z()};
        double fraction = 0, position[3], coordinates[3];
        int subId = 0;
        vtkIdType cellId = -1;
        const auto cell = vtkSmartPointer<vtkGenericCell>::New();
        if (!locator->IntersectWithLine(first, last, 1.e-6, fraction, position, coordinates,
                                        subId, cellId, cell)) return std::nullopt;
        const float distance = float(fraction) * limit;
        const Eigen::Vector3f worldPosition = worldOrigin + worldDirection * distance;
        return PickHit{distance, {worldPosition.x(), worldPosition.y(), worldPosition.z()},
                       {float(position[0]), float(position[1]), float(position[2])},
                       std::nullopt, std::nullopt};
    }

    std::shared_ptr<AutoRegisterDB> clone() const override {
        auto lock = getSharedLock();
        auto copy = trans::TransDB::create<TransientPolyDataActorDB>();
        copy->setPickable(isPickable());
        copy->setGeometry(m_geometry);
        copy->copyTransformFrom(*this);
        copy->setVisible(isVisible());
        copy->setDragable(isDragable());
        copy->setOpacity(getOpacity());
        copy->setUseCellColors(m_useCellColors);
        return copy;
    }

    void setGeometry(vtkSmartPointer<vtkPolyData> geometry) {
        m_geometry = std::move(geometry);
        m_pickLocator = nullptr;
        preparePickLocator(isPickable());
        notifyGeometryChange();
    }

    vtkSmartPointer<vtkPolyData> getGeometry() const { return m_geometry; }

    void setUseCellColors(bool enabled) {
        if (m_useCellColors == enabled) return;
        m_useCellColors = enabled;
        notifyGeometryChange();
    }
    bool usesCellColors() const { return m_useCellColors; }

protected:
    void setPropertyImpl(const trans::Prop& prop, const std::any& value) override {
        if (prop == PROP_Pickable()) {
            // Use the existing property write path so ActorDB callers and property
            // actions also prepare the cache before publishing the change.
            if (const auto* enabled = std::any_cast<bool>(&value)) {
                preparePickLocator(*enabled);
            }
        }
        ActorDB::setPropertyImpl(prop, value);
    }

private:
    void preparePickLocator(bool enabled) {
        if (!enabled || !m_geometry || m_geometry->GetNumberOfCells() == 0) {
            m_pickLocator = nullptr;
            return;
        }
        if (m_pickLocator) return;
        auto locator = vtkSmartPointer<vtkStaticCellLocator>::New();
        locator->SetDataSet(m_geometry);
        locator->BuildLocator();
        m_pickLocator = std::move(locator);
    }

    vtkSmartPointer<vtkPolyData> m_geometry;
    vtkSmartPointer<vtkStaticCellLocator> m_pickLocator;
    bool m_useCellColors{false};
};
