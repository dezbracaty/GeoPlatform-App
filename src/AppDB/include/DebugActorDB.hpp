#pragma once

#include "TransientPolyDataActorDB.hpp"

/** Existing diagnostic visualization type retained for API and TypeID stability. */
class DebugActorDB final : public TransientPolyDataActorDB {
public:
    TypeID getTypeID() const override { return TypeID::DEBUG_ACTOR_DB; }

    std::shared_ptr<AutoRegisterDB> clone() const override {
        auto lock = getSharedLock();
        auto copy = trans::TransDB::create<DebugActorDB>();
        copy->setGeometry(getGeometry());
        copy->copyTransformFrom(*this);
        copy->setVisible(isVisible());
        copy->setPickable(isPickable());
        copy->setDragable(isDragable());
        copy->setOpacity(getOpacity());
        copy->setUseCellColors(usesCellColors());
        return copy;
    }
};
