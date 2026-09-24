#include "PrintBedCollisionDetector.hpp"

PrintBedCollisionDetector::BuildVolume
PrintBedCollisionDetector::buildVolume(const PrintBedDB& bed) {
    const Vector3 center = bed.getCenter();
    const float minZ = bed.getOrigin().z;
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

std::uint32_t PrintBedCollisionDetector::detect(
    const ActorDB& actor,
    const Transform& transform,
    const BuildVolume& volume) {
    return detect(actor.worldBoundsAt(transform.getMatrix()), volume);
}
