#include "SelectionBoxWidgetDB.hpp"

#include "ActorDB.hpp"
#include "DocumentManager.hpp"
#include "Foundation/Log.h"
#include <cmath>

SelectionBoxWidgetDB::SelectionBoxWidgetDB()
    : WidgetDB() {
}

SelectionBoxWidgetDB::SelectionBoxWidgetDB(const DBInstanceID& linkedActorId)
    : WidgetDB(),
      m_initialLinkedActorId(linkedActorId),
      m_hasInitialLinkedActor(true) {
}

void SelectionBoxWidgetDB::initializeProperties() {
    WidgetDB::initializeProperties();
}

void SelectionBoxWidgetDB::initializeSubWidgetProperties() {
    // The dependency edge can only be attached after this widget has been
    // registered and has a concrete TypeID in DocumentManager.
    setLinkedActorID(DBInstanceID());

    setModelBoundsMin(Vector3());
    setModelBoundsMax(Vector3());
    setHasValidBounds(false);
    setName("SelectionBoxWidget");
    setVisible(true);
    setInteractive(false);
}

void SelectionBoxWidgetDB::onFullyInitialized() {
    WidgetDB::onFullyInitialized();
    if (m_hasInitialLinkedActor) {
        setLinkedActorID(m_initialLinkedActorId);
        m_hasInitialLinkedActor = false;
    }
    if (getLinkedActorID().isValid()) {
        updateFromLinkedActor();
    }
}

void SelectionBoxWidgetDB::updateFromLinkedActor() {
    const DBInstanceID linkedId = getLinkedActorID();
    auto* document = DocumentManager::instance();
    if (!document || !linkedId.isValid()) {
        setHasValidBounds(false);
        return;
    }

    const auto actor = std::dynamic_pointer_cast<ActorDB>(document->getDBInstance(linkedId));
    if (!actor) {
        setHasValidBounds(false);
        return;
    }

    const ActorDB::BoundingBox bounds = actor->worldBounds();
    const bool finite =
        std::isfinite(bounds.min.x) && std::isfinite(bounds.min.y) && std::isfinite(bounds.min.z) &&
        std::isfinite(bounds.max.x) && std::isfinite(bounds.max.y) && std::isfinite(bounds.max.z);
    const bool ordered =
        bounds.min.x <= bounds.max.x &&
        bounds.min.y <= bounds.max.y &&
        bounds.min.z <= bounds.max.z;

    if (!bounds.valid || !finite || !ordered) {
        LOG_WARN("SelectionBoxWidgetDB: invalid bounds for actor {}", linkedId.getValue());
        setHasValidBounds(false);
        return;
    }

    setModelBoundsMin(bounds.min);
    setModelBoundsMax(bounds.max);
    setHasValidBounds(true);
}

void SelectionBoxWidgetDB::setLinkedActor(const DBInstanceID& actorId) {
    setLinkedActorID(actorId);
    if (getDBInstanceID().isValid()) {
        updateFromLinkedActor();
    }
}
