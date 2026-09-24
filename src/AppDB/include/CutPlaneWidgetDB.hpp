#pragma once

#include "WidgetDB.hpp"

#include <Transform.hpp>

/** Temporary, authoritative state for the interactive cut-plane gizmo. */
class CutPlaneWidgetDB final : public WidgetDB {
public:
    enum class Part : int {
        None = -1,
        Plane = 1,
        NormalMoveHandle = 2,
        RotateLocalXHandle = 3,
        RotateLocalYHandle = 4
    };

    CutPlaneWidgetDB() = default;
    ~CutPlaneWidgetDB() override = default;

    void initializeProperties() override;
    void initializeSubWidgetProperties() override;

    TypeID getTypeID() const override { return TypeID::CUT_PLANE_WIDGET_DB; }

    // PlaneToWorld is the sole pose representation. Its local XY plane is the
    // cut plane and its local Z axis is the plane normal.
    FIELD_VALUE(CutPlaneWidgetDB, DBInstanceID, TargetModelID)
    FIELD_VALUE(CutPlaneWidgetDB, Transform, PlaneToWorld)
    FIELD_VALUE_SIMPLE(CutPlaneWidgetDB, float, WidgetRadius)
    FIELD_VALUE_SIMPLE(CutPlaneWidgetDB, int, ActivePart)
    FIELD_VALUE_SIMPLE(CutPlaneWidgetDB, bool, IsDragging)
};
