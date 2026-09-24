#pragma once

#include "ActorDB.hpp"
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

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

    std::shared_ptr<AutoRegisterDB> clone() const override {
        auto lock = getSharedLock();
        auto copy = trans::TransDB::create<TransientPolyDataActorDB>();
        copy->setGeometry(m_geometry);
        copy->copyTransformFrom(*this);
        copy->setVisible(isVisible());
        copy->setPickable(isPickable());
        copy->setDragable(isDragable());
        copy->setOpacity(getOpacity());
        copy->setUseCellColors(m_useCellColors);
        return copy;
    }

    void setGeometry(vtkSmartPointer<vtkPolyData> geometry) {
        m_geometry = std::move(geometry);
        notifyGeometryChange();
    }

    vtkSmartPointer<vtkPolyData> getGeometry() const { return m_geometry; }

    void setUseCellColors(bool enabled) {
        if (m_useCellColors == enabled) return;
        m_useCellColors = enabled;
        notifyGeometryChange();
    }
    bool usesCellColors() const { return m_useCellColors; }

private:
    vtkSmartPointer<vtkPolyData> m_geometry;
    bool m_useCellColors{false};
};
