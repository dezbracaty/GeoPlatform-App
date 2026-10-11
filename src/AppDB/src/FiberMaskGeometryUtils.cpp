#include "FiberMaskGeometryUtils.hpp"

#include <vtkCubeSource.h>
#include <vtkCutter.h>
#include <vtkCleanPolyData.h>
#include <vtkPlane.h>
#include <vtkStripper.h>
#include <vtkContourTriangulator.h>
#include <vtkCylinderSource.h>
#include <vtkPolyData.h>
#include <vtkSphereSource.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace FiberMaskGeometryUtils {
using Shape = FiberMaskDB::Shape;
using Region = FiberMaskDB::Region;
namespace {
using Vec = Eigen::Vector3d;
Vec vec(Vector3 p) {
    return {p.x, p.y, p.z};
}
Vector3 point(const Vec& v) {
    return {float(v.x()), float(v.y()), float(v.z())};
}
} // namespace

std::optional<Vector3> pointOnPlane(const Vector3& origin, const Vector3& direction,
                                    const Vector3& plane, const Vector3& normal) {
    const Vec n = vec(normal), d = vec(direction);
    const double denominator = n.dot(d);
    if (!n.allFinite() || !d.allFinite() || std::abs(denominator) < 1.e-6 * n.norm() * d.norm())
        return std::nullopt;
    const double t = n.dot(vec(plane) - vec(origin)) / denominator;
    if (!std::isfinite(t) || t < 0)
        return std::nullopt;
    return point(vec(origin) + t * d);
}

Section section(const Region& r, vtkPolyData* model, double z) {
    Section result;
    if (!model || !FiberMaskDB::validRegion(r) || !std::isfinite(z))
        return result;
    auto plane = vtkSmartPointer<vtkPlane>::New();
    plane->SetOrigin(0, 0, z);
    plane->SetNormal(0, 0, 1);
    auto cutter = vtkSmartPointer<vtkCutter>::New();
    cutter->SetInputData(model);
    cutter->SetCutFunction(plane);
    auto clean = vtkSmartPointer<vtkCleanPolyData>::New();
    clean->SetInputConnection(cutter->GetOutputPort());
    auto strip = vtkSmartPointer<vtkStripper>::New();
    strip->SetInputConnection(clean->GetOutputPort());
    strip->JoinContiguousSegmentsOn();
    auto fill = vtkSmartPointer<vtkContourTriangulator>::New();
    fill->SetInputConnection(strip->GetOutputPort());
    fill->Update();
    if (fill->GetTriangulationError())
        return result;
    result.valid = true;
    const auto pose = r.pose.getMatrix().cast<double>().eval();
    const Vec center = pose.block<3, 1>(0, 3);
    std::vector<std::pair<Vec, double>> planes;
    const auto addPlane = [&](const Vec& local, double distance) {
        const Vec n = pose.block<3, 3>(0, 0) * local;
        planes.emplace_back(n, distance + n.dot(center));
    };
    if (r.shape == Shape::Box) {
        for (int axis = 0; axis < 3; ++axis) {
            addPlane(Vec::Unit(axis), vec(r.dimensions)[axis] * .5);
            addPlane(-Vec::Unit(axis), vec(r.dimensions)[axis] * .5);
        }
    } else if (r.shape == Shape::Cylinder) {
        // Inscribed 96-gon for the display boundary.
        for (int i = 0; i < 96; ++i) {
            const double angle = (i + .5) * 2 * M_PI / 96;
            addPlane(Vec(std::cos(angle), std::sin(angle), 0),
                     r.dimensions.x * .5 * std::cos(M_PI / 96));
        }
        addPlane(Vec(0, 0, 1), r.dimensions.z * .5);
        addPlane(Vec(0, 0, -1), r.dimensions.z * .5);
    } else {
        const double radius2 =
            r.dimensions.x * r.dimensions.x * .25 - (z - center.z()) * (z - center.z());
        if (radius2 <= 0) {
            planes.emplace_back(Vec(0, 0, 0), -1);
        } else
            for (int i = 0; i < 96; ++i) {
                const double angle = (i + .5) * 2 * M_PI / 96;
                Vec n(std::cos(angle), std::sin(angle), 0);
                planes.emplace_back(n, std::sqrt(radius2) * std::cos(M_PI / 96) + n.dot(center));
            }
    }
    auto triangles = fill->GetOutput();
    for (vtkIdType cell = 0; cell < triangles->GetNumberOfCells(); ++cell) {
        vtkIdType count = 0;
        const vtkIdType* ids = nullptr;
        triangles->GetCellPoints(cell, count, ids);
        if (count != 3)
            continue;
        std::array<Vector3, 3> source;
        std::vector<Vec> polygon;
        for (int v = 0; v < 3; ++v) {
            double p[3];
            triangles->GetPoint(ids[v], p);
            source[v] = {float(p[0]), float(p[1]), float(z)};
            polygon.emplace_back(p[0], p[1], z);
        }
        result.modelTriangles.push_back(source);
        for (const auto& [n, bound] : planes) {
            if (polygon.empty())
                break;
            std::vector<Vec> clipped;
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const Vec a = polygon[i], b = polygon[(i + 1) % polygon.size()];
                const double da = n.dot(a) - bound, db = n.dot(b) - bound;
                if (da <= 1.e-9)
                    clipped.push_back(a);
                if ((da < 0 && db > 0) || (da > 0 && db < 0))
                    clipped.push_back(a + (b - a) * (da / (da - db)));
            }
            polygon = std::move(clipped);
        }
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
            result.coveredTriangles.push_back(
                {point(polygon[0]), point(polygon[i]), point(polygon[i + 1])});
    }
    return result;
}

vtkSmartPointer<vtkPolyData> createMesh(FiberMaskDB::Shape shape, Vector3 dimensions) {
    auto empty = vtkSmartPointer<vtkPolyData>::New();
    if (!FiberMaskDB::validRegion({shape, {}, dimensions}))
        return empty;
    vtkSmartPointer<vtkPolyData> body;
    if (shape == Shape::Box) {
        auto source = vtkSmartPointer<vtkCubeSource>::New();
        source->SetXLength(dimensions.x);
        source->SetYLength(dimensions.y);
        source->SetZLength(dimensions.z);
        source->Update();
        body = source->GetOutput();
    } else if (shape == Shape::Sphere) {
        auto source = vtkSmartPointer<vtkSphereSource>::New();
        source->SetRadius(dimensions.x * .5);
        source->SetThetaResolution(48);
        source->SetPhiResolution(32);
        source->Update();
        body = source->GetOutput();
    } else {
        auto source = vtkSmartPointer<vtkCylinderSource>::New();
        source->SetRadius(dimensions.x * .5);
        source->SetHeight(dimensions.z);
        source->SetResolution(64);
        source->CappingOn();
        source->Update();
        auto transform = vtkSmartPointer<vtkTransform>::New();
        transform->RotateX(90);
        auto filter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
        filter->SetInputData(source->GetOutput());
        filter->SetTransform(transform);
        filter->Update();
        body = filter->GetOutput();
    }
    return body;
}

} // namespace FiberMaskGeometryUtils
