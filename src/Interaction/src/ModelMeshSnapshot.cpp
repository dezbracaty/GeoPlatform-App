#include "ModelMeshSnapshot.hpp"
#include "ModelSurfaceColorCodec.hpp"
#include "SliceVertexWelder.hpp"
#include <Geometry.hpp>
#include <vtkCell.h>
#include <vtkPolyData.h>
#include <cmath>
#include <limits>

namespace {
bool cancelled(const std::function<bool()>& f) { return f && f(); }
}

std::optional<std::string> ModelMeshSnapshot::topologyFingerprint(
    const ModelGeometryDB::ReadHandle& geometry, std::string* error) {
    vtkPolyData* polyData = geometry
        ? const_cast<vtkPolyData*>(&geometry.polyData())
        : nullptr;
    if (!polyData || !polyData->GetPoints() ||
        polyData->GetNumberOfCells() <= 0) {
        if (error) *error = "Model has no valid topology for surface colors";
        return std::nullopt;
    }

    std::vector<GeomTriangle> triangles;
    triangles.reserve(static_cast<std::size_t>(polyData->GetNumberOfCells()));
    for (vtkIdType cellId = 0; cellId < polyData->GetNumberOfCells(); ++cellId) {
        vtkCell* cell = polyData->GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() != 3) {
            if (error) *error = "Surface color topology contains a non-triangle cell";
            return std::nullopt;
        }
        double raw[3][3];
        for (int corner = 0; corner < 3; ++corner) {
            polyData->GetPoint(cell->GetPointId(corner), raw[corner]);
        }
        triangles.emplace_back(
            Vector3(static_cast<float>(raw[0][0]),
                    static_cast<float>(raw[0][1]),
                    static_cast<float>(raw[0][2])),
            Vector3(static_cast<float>(raw[1][0]),
                    static_cast<float>(raw[1][1]),
                    static_cast<float>(raw[1][2])),
            Vector3(static_cast<float>(raw[2][0]),
                    static_cast<float>(raw[2][1]),
                    static_cast<float>(raw[2][2])));
    }
    return ModelSurfaceColorCodec::topologyFingerprint(triangles);
}

bool ModelMeshSnapshot::appendGeometry(const ModelGeometryDB::ReadHandle& geometry,
                    libslicer::SliceVolumeInput* output,
                    const std::function<bool()>& isCancelled,
                    std::string* error) {
    if (!output || !error) return false;
    vtkPolyData* polyData = geometry
        ? const_cast<vtkPolyData*>(&geometry.polyData())
        : nullptr;
    if (!polyData || !polyData->GetPoints() ||
        polyData->GetNumberOfPoints() <= 0 || polyData->GetNumberOfCells() <= 0) {
        *error = "Model has no valid indexed mesh snapshot";
        return false;
    }
    if (static_cast<unsigned long long>(polyData->GetNumberOfPoints()) >
        std::numeric_limits<std::uint32_t>::max()) {
        *error = "Model exceeds the supported vertex index range";
        return false;
    }

    const vtkIdType pointCount = polyData->GetNumberOfPoints();
    std::vector<std::uint32_t> sourcePointToVertex(
        static_cast<std::size_t>(pointCount));
    GPlatform::SliceVertexWelder weldedVertices(
        static_cast<std::size_t>(pointCount));

    // Imported triangle data may store three independent VTK points per face.
    // The libslicer in-memory API accepts an indexed TriangleMesh and
    // therefore requires shared positions to reference shared vertex indices.
    // Weld exact float positions here while preserving cell order, so facet
    // painting remains indexed by the original triangle order.
    for (vtkIdType pointId = 0; pointId < pointCount; ++pointId) {
        if ((pointId & 0x3fffu) == 0 && cancelled(isCancelled)) {
            *error = "Slice session preparation was cancelled";
            return false;
        }
        double point[3];
        polyData->GetPoint(pointId, point);
        // Keep the mesh and its facet trees in their original local frame.
        // Native ModelVolume applies the part transform, including mirroring.
        const libslicer::SliceVertex vertex{
            static_cast<float>(point[0]),
            static_cast<float>(point[1]),
            static_cast<float>(point[2])};
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z)) {
            *error = "Slice input contains a non-finite vertex";
            return false;
        }
        sourcePointToVertex[static_cast<std::size_t>(pointId)] =
            weldedVertices.indexFor(vertex);
    }
    output->vertices = weldedVertices.takeVertices();
    output->triangles.reserve(static_cast<std::size_t>(polyData->GetNumberOfCells()));
    for (vtkIdType cellId = 0; cellId < polyData->GetNumberOfCells(); ++cellId) {
        if ((cellId & 0x3fffu) == 0 && cancelled(isCancelled)) {
            *error = "Slice session preparation was cancelled";
            return false;
        }
        vtkCell* cell = polyData->GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() != 3) {
            *error = "Slice input contains a non-triangle cell";
            return false;
        }
        const vtkIdType pointA = cell->GetPointId(0);
        const vtkIdType pointB = cell->GetPointId(1);
        const vtkIdType pointC = cell->GetPointId(2);
        if (pointA < 0 || pointB < 0 || pointC < 0 ||
            pointA >= pointCount || pointB >= pointCount || pointC >= pointCount) {
            *error = "Slice input contains an invalid triangle index";
            return false;
        }
        output->triangles.push_back({
            sourcePointToVertex[static_cast<std::size_t>(pointA)],
            sourcePointToVertex[static_cast<std::size_t>(pointB)],
            sourcePointToVertex[static_cast<std::size_t>(pointC)]});
    }
    return true;
}
