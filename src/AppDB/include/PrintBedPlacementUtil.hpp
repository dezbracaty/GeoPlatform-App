#pragma once

#include "ModelInstanceDB.hpp"
#include "PrintBedDB.hpp"
#include <DocumentManager.hpp>
#include <cmath>
#include <stdexcept>

// Model instances retain scene-world transforms. A bed's fixed display
// translation is removed exactly once at the slicing boundary.
namespace PrintBedPlacementUtil {
inline Vector3 sceneOffset(const PrintBedDB& bed) {
    const auto matrix = bed.getTransformMatrix();
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (!std::isfinite(matrix(row, column)))
                throw std::invalid_argument("Print bed display transform is not finite");
            if (column == 3 && row < 3) continue;
            if (std::abs(matrix(row, column) - (row == column ? 1.0f : 0.0f)) > 1e-6f)
                throw std::invalid_argument("Print bed display transform must be a translation");
        }
    }
    return {matrix(0, 3), matrix(1, 3), matrix(2, 3)};
}
inline Vector3 plateToScene(const PrintBedDB& bed, const Vector3& point) {
    return point + sceneOffset(bed);
}
inline Vector3 sceneToPlate(const PrintBedDB& bed, const Vector3& point) {
    return point - sceneOffset(bed);
}
inline Transform::Matrix4 instancePlateMatrix(const ModelInstanceDB& instance) {
    auto matrix = instance.getTransformMatrix();
    auto* document = DocumentManager::instance();
    const auto bed = document
        ? document->getDB<PrintBedDB>(instance.getParentPrintBedDBId()) : nullptr;
    if (!bed) throw std::invalid_argument("Model instance is not assigned to a print bed");
    const auto offset = sceneOffset(*bed);
    matrix(0, 3) -= offset.x;
    matrix(1, 3) -= offset.y;
    matrix(2, 3) -= offset.z;
    return matrix;
}
} // namespace PrintBedPlacementUtil
