#pragma once

#include "FiberMaskDB.hpp"
#include <vtkSmartPointer.h>
#include <optional>
#include <array>
#include <vector>

class vtkPolyData;

// Stateless geometry operations. Authored data and its validation belong to FiberMaskDB;
// preview colors, borders and interaction state belong to the editing tool.
namespace FiberMaskGeometryUtils {
std::optional<Vector3> pointOnPlane(const Vector3& rayOrigin, const Vector3& rayDirection,
                                  const Vector3& planeOrigin, const Vector3& normal);
struct Section {
    bool valid{false};
    std::vector<std::array<Vector3, 3>> modelTriangles;
    std::vector<std::array<Vector3, 3>> coveredTriangles;
};
// Horizontal section in model coordinates, for display only. Curved boundaries
// are tessellated; this does not affect native modifier slicing.
Section section(const FiberMaskDB::Region& region, vtkPolyData* modelGeometry, double z);
// Centered local surface only, without colors, borders or scene objects.
vtkSmartPointer<vtkPolyData> createMesh(FiberMaskDB::Shape shape, Vector3 dimensions);
} // namespace FiberMaskGeometryUtils
