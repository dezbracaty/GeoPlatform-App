#include "../include/ModelGeometryDB.hpp"

#include <MeshRayQuery.hpp>
#include <TransactionManager.hpp>

#include <QVector3D>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace {
constexpr double kMinimumCrossLengthSquared = 1.0e-24;
constexpr vtkIdType kMaximumMeshIndex =
    static_cast<vtkIdType>(std::numeric_limits<std::int32_t>::max());

void setError(std::string* output, const std::string& message) {
    if (output) *output = message;
}

bool validatePrintablePolyData(vtkPolyData& data, std::string* error) {
    vtkPoints* points = data.GetPoints();
    vtkCellArray* polygons = data.GetPolys();
    const vtkIdType pointCount = points ? points->GetNumberOfPoints() : 0;
    const vtkIdType cellCount = data.GetNumberOfCells();
    if (!points || !polygons || pointCount <= 0 || cellCount <= 0 ||
        polygons->GetNumberOfCells() != cellCount) {
        setError(error, "Geometry must contain triangle polygons and points");
        return false;
    }
    if (pointCount > kMaximumMeshIndex || cellCount > kMaximumMeshIndex) {
        setError(error, "Geometry exceeds the supported signed 32-bit index range");
        return false;
    }

    for (vtkIdType pointId = 0; pointId < pointCount; ++pointId) {
        double point[3]{};
        points->GetPoint(pointId, point);
        if (!std::isfinite(point[0]) || !std::isfinite(point[1]) ||
            !std::isfinite(point[2])) {
            setError(error, "Geometry contains a non-finite point");
            return false;
        }
    }

    vtkIdType nonDegenerateTriangleCount = 0;
    for (vtkIdType cellId = 0; cellId < cellCount; ++cellId) {
        vtkCell* cell = data.GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() != 3) {
            setError(error, "Geometry contains a non-triangle cell");
            return false;
        }
        vtkIdType ids[3]{};
        double vertices[3][3]{};
        for (int corner = 0; corner < 3; ++corner) {
            ids[corner] = cell->GetPointId(corner);
            if (ids[corner] < 0 || ids[corner] >= pointCount ||
                ids[corner] > kMaximumMeshIndex) {
                setError(error, "Geometry contains an invalid triangle index");
                return false;
            }
            points->GetPoint(ids[corner], vertices[corner]);
        }
        const double ab[3]{
            vertices[1][0] - vertices[0][0],
            vertices[1][1] - vertices[0][1],
            vertices[1][2] - vertices[0][2]};
        const double ac[3]{
            vertices[2][0] - vertices[0][0],
            vertices[2][1] - vertices[0][1],
            vertices[2][2] - vertices[0][2]};
        const double cross[3]{
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0]};
        const double crossLengthSquared =
            cross[0] * cross[0] + cross[1] * cross[1] +
            cross[2] * cross[2];
        if (!std::isfinite(crossLengthSquared)) {
            setError(error, "Geometry contains a non-finite triangle area");
            return false;
        }
        if (crossLengthSquared > kMinimumCrossLengthSquared) {
            ++nonDegenerateTriangleCount;
        }
    }
    if (nonDegenerateTriangleCount == 0) {
        setError(error, "Geometry contains no non-degenerate triangles");
        return false;
    }
    return true;
}

class PointSupportTree final {
public:
    explicit PointSupportTree(std::vector<Vector3> points)
        : m_points(std::move(points)) {
        if (!m_points.empty()) {
            m_nodes.reserve(m_points.size() * 2);
            build(0, m_points.size());
        }
    }

    float maximumDot(const Vector3& direction) const {
        if (m_nodes.empty()) return 0.0f;
        double best = -std::numeric_limits<double>::infinity();
        query(0, direction, best);
        return static_cast<float>(best);
    }

private:
    struct Node {
        Vector3 min;
        Vector3 max;
        std::size_t begin{0};
        std::size_t end{0};
        int left{-1};
        int right{-1};
    };

    static float component(const Vector3& point, int axis) {
        return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
    }

    int build(std::size_t begin, std::size_t end) {
        Node node;
        node.begin = begin;
        node.end = end;
        node.min = node.max = m_points[begin];
        for (std::size_t index = begin + 1; index < end; ++index) {
            const Vector3& point = m_points[index];
            node.min.x = std::min(node.min.x, point.x);
            node.min.y = std::min(node.min.y, point.y);
            node.min.z = std::min(node.min.z, point.z);
            node.max.x = std::max(node.max.x, point.x);
            node.max.y = std::max(node.max.y, point.y);
            node.max.z = std::max(node.max.z, point.z);
        }
        const int nodeIndex = static_cast<int>(m_nodes.size());
        m_nodes.push_back(node);
        constexpr std::size_t kLeafSize = 16;
        if (end - begin <= kLeafSize) return nodeIndex;

        const Vector3 extent = node.max - node.min;
        const int axis = extent.x >= extent.y && extent.x >= extent.z
            ? 0 : extent.y >= extent.z ? 1 : 2;
        const std::size_t middle = begin + (end - begin) / 2;
        std::nth_element(
            m_points.begin() + static_cast<std::ptrdiff_t>(begin),
            m_points.begin() + static_cast<std::ptrdiff_t>(middle),
            m_points.begin() + static_cast<std::ptrdiff_t>(end),
            [axis](const Vector3& lhs, const Vector3& rhs) {
                return component(lhs, axis) < component(rhs, axis);
            });
        const int left = build(begin, middle);
        const int right = build(middle, end);
        m_nodes[static_cast<std::size_t>(nodeIndex)].left = left;
        m_nodes[static_cast<std::size_t>(nodeIndex)].right = right;
        return nodeIndex;
    }

    static double upperBound(const Node& node, const Vector3& direction) {
        return static_cast<double>(direction.x) *
                (direction.x >= 0.0f ? node.max.x : node.min.x) +
            static_cast<double>(direction.y) *
                (direction.y >= 0.0f ? node.max.y : node.min.y) +
            static_cast<double>(direction.z) *
                (direction.z >= 0.0f ? node.max.z : node.min.z);
    }

    void query(int nodeIndex, const Vector3& direction, double& best) const {
        const Node& node = m_nodes[static_cast<std::size_t>(nodeIndex)];
        if (upperBound(node, direction) <= best) return;
        if (node.left < 0) {
            for (std::size_t index = node.begin; index < node.end; ++index) {
                const Vector3& point = m_points[index];
                const double value =
                    static_cast<double>(point.x) * direction.x +
                    static_cast<double>(point.y) * direction.y +
                    static_cast<double>(point.z) * direction.z;
                best = std::max(best, value);
            }
            return;
        }
        const double leftUpper = upperBound(
            m_nodes[static_cast<std::size_t>(node.left)], direction);
        const double rightUpper = upperBound(
            m_nodes[static_cast<std::size_t>(node.right)], direction);
        if (leftUpper >= rightUpper) {
            query(node.left, direction, best);
            query(node.right, direction, best);
        } else {
            query(node.right, direction, best);
            query(node.left, direction, best);
        }
    }

    std::vector<Vector3> m_points;
    std::vector<Node> m_nodes;
};
}

struct ModelGeometryDB::Version {
    vtkSmartPointer<vtkPolyData> polyData;
    std::unique_ptr<MeshRayQuery> bvh;
    std::unique_ptr<PointSupportTree> supportTree;
    ActorDB::BoundingBox bounds;
    std::size_t vertexCount{0};
    std::size_t triangleCount{0};
    std::string source;
};

struct ModelGeometryDB::RuntimeData {
    mutable std::shared_mutex mutex;
    std::shared_ptr<const Version> currentVersion;
    std::uint64_t revision{0};
};

const vtkPolyData& ModelGeometryDB::ReadHandle::polyData() const noexcept {
    return *m_version->polyData;
}

ModelGeometryDB::ModelGeometryDB()
    : m_runtime(std::make_unique<RuntimeData>()) {}

ModelGeometryDB::~ModelGeometryDB() = default;

const trans::Prop& ModelGeometryDB::PROP_GeometryVersion() {
    static const trans::Prop prop(typeid(ModelGeometryDB), "GeometryVersion");
    return prop;
}

void ModelGeometryDB::initializeProperties() {
}

ModelGeometryDB::ReadHandle ModelGeometryDB::read() const {
    std::shared_lock lock(m_runtime->mutex);
    return ReadHandle(m_runtime->currentVersion, m_runtime->revision);
}

bool ModelGeometryDB::replacePolyData(
    vtkPolyData* polyData, const std::string& sourceTag, std::string* error) {
    if (!polyData) {
        setError(error, "Geometry source is null");
        return false;
    }

    auto copied = vtkSmartPointer<vtkPolyData>::New();
    copied->DeepCopy(polyData);
    if (!validatePrintablePolyData(*copied, error)) return false;

    auto next = std::make_shared<Version>();
    next->polyData = std::move(copied);
    next->vertexCount =
        static_cast<std::size_t>(next->polyData->GetNumberOfPoints());
    next->triangleCount =
        static_cast<std::size_t>(next->polyData->GetNumberOfCells());
    next->source = sourceTag;

    double vtkBounds[6]{};
    next->polyData->GetBounds(vtkBounds);
    next->bounds.min = {
        static_cast<float>(vtkBounds[0]),
        static_cast<float>(vtkBounds[2]),
        static_cast<float>(vtkBounds[4])};
    next->bounds.max = {
        static_cast<float>(vtkBounds[1]),
        static_cast<float>(vtkBounds[3]),
        static_cast<float>(vtkBounds[5])};
    next->bounds.valid = true;

    std::vector<QVector3D> vertices;
    vertices.reserve(next->vertexCount);
    for (vtkIdType pointId = 0;
         pointId < next->polyData->GetNumberOfPoints(); ++pointId) {
        double point[3]{};
        next->polyData->GetPoint(pointId, point);
        vertices.emplace_back(
            static_cast<float>(point[0]),
            static_cast<float>(point[1]),
            static_cast<float>(point[2]));
    }
    std::vector<Vector3> supportPoints;
    supportPoints.reserve(vertices.size());
    for (const QVector3D& vertex : vertices) {
        supportPoints.push_back({vertex.x(), vertex.y(), vertex.z()});
    }
    std::sort(supportPoints.begin(), supportPoints.end(),
              [](const Vector3& lhs, const Vector3& rhs) {
        if (lhs.x != rhs.x) return lhs.x < rhs.x;
        if (lhs.y != rhs.y) return lhs.y < rhs.y;
        return lhs.z < rhs.z;
    });
    supportPoints.erase(
        std::unique(supportPoints.begin(), supportPoints.end(),
                    [](const Vector3& lhs, const Vector3& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }),
        supportPoints.end());
    next->supportTree =
        std::make_unique<PointSupportTree>(std::move(supportPoints));

    std::vector<std::array<int, 3>> triangles;
    triangles.reserve(next->triangleCount);
    for (vtkIdType cellId = 0;
         cellId < next->polyData->GetNumberOfCells(); ++cellId) {
        vtkCell* cell = next->polyData->GetCell(cellId);
        triangles.push_back({
            static_cast<int>(cell->GetPointId(0)),
            static_cast<int>(cell->GetPointId(1)),
            static_cast<int>(cell->GetPointId(2))});
    }
    next->bvh = std::make_unique<MeshRayQuery>();
    if (!next->bvh->build(vertices, triangles)) {
        setError(error, "Unable to construct the geometry BVH");
        return false;
    }

    std::shared_ptr<const Version> published = std::move(next);
    {
        std::unique_lock lock(m_runtime->mutex);
        const auto previous = m_runtime->currentVersion;
        if (TransactionManager::instance().isInTransaction()) {
            TransactionManager::instance().recordPropertyChange(
                shared_from_this(), PROP_GeometryVersion(),
                std::any(previous), std::any(published));
        }
        m_runtime->currentVersion = std::move(published);
        ++m_runtime->revision;
    }
    notifyChange(ChangeType::PROPERTY_CHANGED, "Geometry");
    return true;
}

std::size_t ModelGeometryDB::vertexCount() const {
    const auto handle = read();
    return handle ? handle.m_version->vertexCount : 0;
}

std::size_t ModelGeometryDB::triangleCount() const {
    const auto handle = read();
    return handle ? handle.m_version->triangleCount : 0;
}

std::uint64_t ModelGeometryDB::revision() const {
    return read().revision();
}

ActorDB::BoundingBox ModelGeometryDB::localBounds() const {
    const auto handle = read();
    return handle ? handle.m_version->bounds : ActorDB::BoundingBox{};
}

ActorDB::BoundingBox ModelGeometryDB::boundsAfterTransform(
    const Transform::Matrix4& matrix) const {
    const auto handle = read();
    if (!handle || !handle.m_version->supportTree || !matrix.allFinite()) {
        return {};
    }
    float minima[3]{};
    float maxima[3]{};
    for (int axis = 0; axis < 3; ++axis) {
        const Vector3 direction{
            matrix(axis, 0), matrix(axis, 1), matrix(axis, 2)};
        const float translation = matrix(axis, 3);
        maxima[axis] = translation +
            handle.m_version->supportTree->maximumDot(direction);
        minima[axis] = translation -
            handle.m_version->supportTree->maximumDot(direction * -1.0f);
    }
    ActorDB::BoundingBox result;
    result.min = {minima[0], minima[1], minima[2]};
    result.max = {maxima[0], maxima[1], maxima[2]};
    result.valid = true;
    return result;
}

std::string ModelGeometryDB::source() const {
    const auto handle = read();
    return handle ? handle.m_version->source : std::string{};
}

std::optional<ModelGeometryDB::RayHit> ModelGeometryDB::intersectLocalRay(
    const Vector3& origin, const Vector3& direction, float maxDistance) const {
    const auto handle = read();
    if (!handle || !handle.m_version->bvh) return std::nullopt;
    const auto hit = handle.m_version->bvh->firstHit(
        QVector3D(origin.x, origin.y, origin.z),
        QVector3D(direction.x, direction.y, direction.z), maxDistance);
    if (!hit.valid || hit.facetId < 0) return std::nullopt;
    return RayHit{static_cast<std::uint64_t>(hit.facetId),
                  {hit.position.x(), hit.position.y(), hit.position.z()},
                  hit.distance};
}

std::any ModelGeometryDB::getPropertyImpl(const trans::Prop& prop) const {
    if (prop.nameView() == "GeometryVersion") {
        std::shared_lock lock(m_runtime->mutex);
        return std::any(m_runtime->currentVersion);
    }
    return AutoRegisterDB::getPropertyImpl(prop);
}

void ModelGeometryDB::setPropertyImpl(
    const trans::Prop& prop, const std::any& value) {
    if (prop.nameView() == "GeometryVersion") {
        if (const auto* version =
                std::any_cast<std::shared_ptr<const Version>>(&value)) {
            applyVersion(*version);
        }
        return;
    }
    AutoRegisterDB::setPropertyImpl(prop, value);
}

void ModelGeometryDB::applyVersion(std::shared_ptr<const Version> version) {
    {
        std::unique_lock lock(m_runtime->mutex);
        m_runtime->currentVersion = std::move(version);
        ++m_runtime->revision;
    }
    notifyChange(ChangeType::PROPERTY_CHANGED, "Geometry");
}

bool ModelGeometryDB::isValid() const {
    return AutoRegisterDB::isValid() && triangleCount() > 0;
}

std::shared_ptr<AutoRegisterDB> ModelGeometryDB::clone() const {
    auto copy = trans::TransDB::create<ModelGeometryDB>();
    copy->setDisplayName(getDisplayName());
    const auto handle = read();
    if (handle) copy->applyVersion(handle.m_version);
    return copy;
}
