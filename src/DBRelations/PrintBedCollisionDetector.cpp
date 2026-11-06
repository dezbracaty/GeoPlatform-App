#include "PrintBedCollisionDetector.hpp"
#include <ModelInstanceDB.hpp>
#include <PrintBedPlacementUtil.hpp>

PrintBedCollisionDetector::BuildVolume
PrintBedCollisionDetector::buildVolume(const PrintBedDB& bed) {
    const Vector3 center = PrintBedPlacementUtil::plateToScene(bed, bed.getCenter());
    const float minZ = PrintBedPlacementUtil::plateToScene(bed, bed.getOrigin()).z;
    return {
        center.x - bed.getWidth() * 0.5f,
        center.x + bed.getWidth() * 0.5f,
        center.y - bed.getHeight() * 0.5f,
        center.y + bed.getHeight() * 0.5f,
        minZ,
        minZ + bed.getPrintHeight()};
}

std::uint32_t PrintBedCollisionDetector::detect(
    const ActorDB::BoundingBox& bounds,
    const BuildVolume& volume) {
    if (!bounds.valid) return PrintBedDB::CollisionNone;

    std::uint32_t mask = PrintBedDB::CollisionNone;
    if (bounds.min.x < volume.minX - ToleranceMm)
        mask |= PrintBedDB::CollisionLeft;
    if (bounds.max.x > volume.maxX + ToleranceMm)
        mask |= PrintBedDB::CollisionRight;
    if (bounds.min.y < volume.minY - ToleranceMm)
        mask |= PrintBedDB::CollisionFront;
    if (bounds.max.y > volume.maxY + ToleranceMm)
        mask |= PrintBedDB::CollisionBack;
    if (bounds.max.z > volume.maxZ + ToleranceMm)
        mask |= PrintBedDB::CollisionTop;
    if (bounds.min.z < volume.minZ - ToleranceMm)
        mask |= PrintBedDB::CollisionBottom;
    return mask;
}

ActorDB::BoundingBox PrintBedCollisionDetector::worldBounds(
    const ActorDB& actor, const Transform& transform) {
    const auto* instance = dynamic_cast<const ModelInstanceDB*>(&actor);
    if (!instance) return actor.worldBoundsAt(transform.getMatrix());

    // Orca's build-volume check uses model parts. Modifiers and support
    // regions may extend outside the bed without producing printed geometry.
    return instance->modelPartBoundsAt(transform.getMatrix());
}

std::uint32_t PrintBedCollisionDetector::detect(
    const ActorDB& actor,
    const Transform& transform,
    const BuildVolume& volume) {
    return detect(worldBounds(actor, transform), volume);
}
