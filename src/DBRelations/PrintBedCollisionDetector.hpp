#pragma once

#include <ActorDB.hpp>
#include <PrintBedDB.hpp>

#include <cstdint>

/** Stateless build-volume collision calculation shared by relations and commands. */
class PrintBedCollisionDetector final {
public:
    struct BuildVolume {
        float minX;
        float maxX;
        float minY;
        float maxY;
        float minZ;
        float maxZ;
    };

    static BuildVolume buildVolume(const PrintBedDB& bed);
    static std::uint32_t detect(
        const ActorDB::BoundingBox& worldBounds,
        const BuildVolume& volume);
    static std::uint32_t detect(
        const ActorDB& actor,
        const Transform& transform,
        const BuildVolume& volume);

private:
    static constexpr float ToleranceMm = 0.01f;
};
