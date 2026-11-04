#include "FilamentPrintBedDBSync.hpp"

#include "FilamentActorPresentation.hpp"
#include "FilamentRenderableResource.hpp"
#include "FilamentSceneAdapter.hpp"
#include "FilamentSpace.hpp"

#include <Foundation/Log.h>
#include <IDocumentReadView.hpp>
#include <PrintBedDB.hpp>
#include <PrintBedViewPolicy.hpp>
#include <filament/Camera.h>

#include <QImageReader>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

namespace GPlatform::Rendering {
namespace {

using Geometry = FilamentGeometry;
using Primitive = FilamentPrimitive;
using Vertex = FilamentVertex;

constexpr std::uint32_t CollisionFillLabel = 1;
constexpr std::uint32_t CollisionBorderLabel = 2;
constexpr std::uint32_t BedTextureLabel = 3;
constexpr std::uint32_t BedGridLabel = 4;
constexpr float BedModelZOffset = -0.45f;
constexpr float BedTextureZOffset = 0.08f;
constexpr float BedGridZOffset = 0.20f;

struct BoundaryQuad {
    std::array<Vector3, 4> corners;
    Vector3 normal;
};

void appendQuad(
    Geometry& geometry,
    const std::array<Vector3, 4>& corners,
    const Vector3& normal) {
    const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
    const auto frame = toFilamentTangentFrame(normal);
    for (const auto& corner : corners) {
        geometry.vertices.push_back({toFilamentSpace(corner), frame});
    }
    geometry.indices.insert(
        geometry.indices.end(),
        {first, first + 1, first + 2, first, first + 2, first + 3});
}

void appendTexturedQuad(
    Geometry& geometry,
    const std::array<Vector3, 4>& corners,
    const Vector3& normal) {
    const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
    const auto frame = toFilamentTangentFrame(normal);
    constexpr std::array<filament::math::float2, 4> uvs = {
        filament::math::float2{0.0f, 0.0f},
        filament::math::float2{1.0f, 0.0f},
        filament::math::float2{1.0f, 1.0f},
        filament::math::float2{0.0f, 1.0f}};
    for (std::size_t index = 0; index < corners.size(); ++index) {
        geometry.vertices.push_back(
            {toFilamentSpace(corners[index]), frame, uvs[index]});
    }
    geometry.indices.insert(
        geometry.indices.end(),
        {first, first + 1, first + 2, first, first + 2, first + 3});
}

Vector3 triangleNormal(
    const Vector3& first,
    const Vector3& second,
    const Vector3& third) {
    const Vector3 left = second - first;
    const Vector3 right = third - first;
    Vector3 normal(
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x);
    const float length = std::sqrt(
        normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    return length > 1.0e-8f
        ? normal * (1.0f / length)
        : Vector3(0.0f, 0.0f, 1.0f);
}

void appendTriangle(
    Geometry& geometry,
    const std::array<Vector3, 3>& positions,
    Vector3 normal,
    const Vector3& translation) {
    const float lengthSquared =
        normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
    if (lengthSquared <= 1.0e-12f) {
        normal = triangleNormal(positions[0], positions[1], positions[2]);
    }
    const auto frame = toFilamentTangentFrame(normal);
    const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
    for (const Vector3& position : positions) {
        geometry.vertices.push_back(
            {toFilamentSpace(position + translation), frame});
    }
    geometry.indices.insert(
        geometry.indices.end(), {first, first + 1, first + 2});
}

bool appendBinaryStl(
    Geometry& geometry,
    const std::filesystem::path& path,
    const Vector3& translation) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    const std::vector<char> bytes{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    if (bytes.size() < 84) return false;

    std::uint32_t triangleCount = 0;
    std::memcpy(&triangleCount, bytes.data() + 80, sizeof(triangleCount));
    constexpr std::size_t recordSize = 50;
    const std::size_t availableTriangles = (bytes.size() - 84) / recordSize;
    if (triangleCount == 0 || triangleCount > availableTriangles ||
        triangleCount > std::numeric_limits<std::uint32_t>::max() / 3) {
        return false;
    }

    geometry.vertices.reserve(
        geometry.vertices.size() + static_cast<std::size_t>(triangleCount) * 3);
    geometry.indices.reserve(
        geometry.indices.size() + static_cast<std::size_t>(triangleCount) * 3);
    for (std::uint32_t index = 0; index < triangleCount; ++index) {
        const char* record = bytes.data() + 84 +
            static_cast<std::size_t>(index) * recordSize;
        std::array<float, 12> values{};
        std::memcpy(values.data(), record, values.size() * sizeof(float));
        appendTriangle(
            geometry,
            {Vector3(values[3], values[4], values[5]),
             Vector3(values[6], values[7], values[8]),
             Vector3(values[9], values[10], values[11])},
            Vector3(values[0], values[1], values[2]), translation);
    }
    return true;
}

bool appendAsciiStl(
    Geometry& geometry,
    const std::filesystem::path& path,
    const Vector3& translation) {
    std::ifstream stream(path);
    if (!stream) return false;
    std::string token;
    Vector3 normal(0.0f, 0.0f, 1.0f);
    std::array<Vector3, 3> positions;
    std::size_t vertexCount = 0;
    while (stream >> token) {
        if (token == "facet") {
            stream >> token;
            if (token == "normal") {
                stream >> normal.x >> normal.y >> normal.z;
            }
        } else if (token == "vertex") {
            Vector3 position;
            stream >> position.x >> position.y >> position.z;
            if (!stream) return false;
            positions[vertexCount++] = position;
            if (vertexCount == positions.size()) {
                appendTriangle(geometry, positions, normal, translation);
                vertexCount = 0;
            }
        }
    }
    return !geometry.indices.empty() && vertexCount == 0;
}

Geometry bedModelGeometry(const PrintBedDB& bed, const Vector3& color) {
    const std::filesystem::path path(bed.getBedModelPath());
    Geometry geometry;
    if (path.empty() || !std::filesystem::is_regular_file(path)) {
        return geometry;
    }
    const Vector3 center = bed.getCenter();
    const Vector3 translation(
        center.x, center.y, bed.getOrigin().z + BedModelZOffset);
    const bool loaded = appendBinaryStl(geometry, path, translation) ||
        appendAsciiStl(geometry, path, translation);
    if (!loaded) {
        geometry.vertices.clear();
        geometry.indices.clear();
        return geometry;
    }
    geometry.primitives.push_back({
        0, 0, static_cast<std::uint32_t>(geometry.indices.size()), color});
    geometry.valid = true;
    LOG_INFO(
        "FilamentPrintBedDBSync: loaded bed model '{}' triangles={}",
        path.string(), geometry.indices.size() / 3);
    return geometry;
}

void appendLineQuad(
    Geometry& geometry,
    const Vector3& start,
    const Vector3& end,
    float width) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1.0e-6f) return;
    const float halfWidth = width * 0.5f;
    const float offsetX = -dy / length * halfWidth;
    const float offsetY = dx / length * halfWidth;
    appendQuad(
        geometry,
        {Vector3(start.x + offsetX, start.y + offsetY, start.z),
         Vector3(end.x + offsetX, end.y + offsetY, end.z),
         Vector3(end.x - offsetX, end.y - offsetY, end.z),
         Vector3(start.x - offsetX, start.y - offsetY, start.z)},
        Vector3(0.0f, 0.0f, 1.0f));
}

void appendBoundaryFrame(
    Geometry& geometry,
    const BoundaryQuad& boundary,
    float width) {
    Vector3 center;
    for (const Vector3& corner : boundary.corners) {
        center = center + corner * 0.25f;
    }

    for (std::size_t index = 0; index < boundary.corners.size(); ++index) {
        const Vector3& start = boundary.corners[index];
        const Vector3& end = boundary.corners[
            (index + 1) % boundary.corners.size()];
        const Vector3 midpoint = (start + end) * 0.5f;
        const Vector3 towardCenter = center - midpoint;
        const float length = std::sqrt(
            towardCenter.x * towardCenter.x +
            towardCenter.y * towardCenter.y +
            towardCenter.z * towardCenter.z);
        if (length <= 1.0e-6f) continue;
        const Vector3 inset = towardCenter * (width / length);
        appendQuad(
            geometry,
            {start, end, end + inset, start + inset},
            boundary.normal);
    }
}

bool isTransformOnly(std::span<const DocumentChange> changes) {
    return !changes.empty() && std::all_of(
        changes.begin(), changes.end(), [](const DocumentChange& change) {
            return change.changeType == ChangeType::PROPERTY_CHANGED &&
                change.propertyName == "Transform";
        });
}

bool isPresentationOnly(std::span<const DocumentChange> changes) {
    return !changes.empty() && std::all_of(
        changes.begin(), changes.end(), [](const DocumentChange& change) {
            if (change.changeType != ChangeType::PROPERTY_CHANGED) return false;
            const std::string_view property(change.propertyName);
            return property == "Opacity" ||
                property == "Material" || property.starts_with("Material.") ||
                property == "DiffuseColor" || property == "Roughness" ||
                property == "Metallic" || property == "RenderingMode" ||
                property == "Specular" || property == "SpecularPower" ||
                property == "Lighting";
        });
}

Geometry boxGeometry(
    const Vector3& minimum,
    const Vector3& maximum,
    const Vector3& color) {
    Geometry geometry;
    const float x0 = minimum.x;
    const float y0 = minimum.y;
    const float z0 = minimum.z;
    const float x1 = maximum.x;
    const float y1 = maximum.y;
    const float z1 = maximum.z;
    appendQuad(geometry,
        {Vector3(x0, y0, z1), Vector3(x1, y0, z1),
         Vector3(x1, y1, z1), Vector3(x0, y1, z1)},
        Vector3(0.0f, 0.0f, 1.0f));
    appendQuad(geometry,
        {Vector3(x0, y1, z0), Vector3(x1, y1, z0),
         Vector3(x1, y0, z0), Vector3(x0, y0, z0)},
        Vector3(0.0f, 0.0f, -1.0f));
    appendQuad(geometry,
        {Vector3(x0, y0, z0), Vector3(x1, y0, z0),
         Vector3(x1, y0, z1), Vector3(x0, y0, z1)},
        Vector3(0.0f, -1.0f, 0.0f));
    appendQuad(geometry,
        {Vector3(x1, y1, z0), Vector3(x0, y1, z0),
         Vector3(x0, y1, z1), Vector3(x1, y1, z1)},
        Vector3(0.0f, 1.0f, 0.0f));
    appendQuad(geometry,
        {Vector3(x0, y1, z0), Vector3(x0, y0, z0),
         Vector3(x0, y0, z1), Vector3(x0, y1, z1)},
        Vector3(-1.0f, 0.0f, 0.0f));
    appendQuad(geometry,
        {Vector3(x1, y0, z0), Vector3(x1, y1, z0),
         Vector3(x1, y1, z1), Vector3(x1, y0, z1)},
        Vector3(1.0f, 0.0f, 0.0f));
    geometry.primitives.push_back({
        0, 0, static_cast<std::uint32_t>(geometry.indices.size()), color});
    geometry.valid = true;
    return geometry;
}

void appendCollisionBoundaries(Geometry& geometry, const PrintBedDB& bed) {
    if (!bed.getCollisionEffectsEnabled()) return;

    const Vector3 center = bed.getCenter();
    const Vector3 origin = bed.getOrigin();
    const float minX = center.x - bed.getWidth() * 0.5f;
    const float maxX = center.x + bed.getWidth() * 0.5f;
    const float minY = center.y - bed.getHeight() * 0.5f;
    const float maxY = center.y + bed.getHeight() * 0.5f;
    const float minZ = origin.z;
    const float maxZ = origin.z + bed.getPrintHeight();
    const float sideBandHeight = std::clamp(
        std::min(bed.getWidth(), bed.getHeight()) *
            PrintBedViewDefaults::COLLISION_SIDE_BAND_RATIO,
        PrintBedViewDefaults::COLLISION_SIDE_BAND_MIN_HEIGHT_MM,
        PrintBedViewDefaults::COLLISION_SIDE_BAND_MAX_HEIGHT_MM);
    const float sideMaxZ = std::min(maxZ, minZ + sideBandHeight);
    std::vector<BoundaryQuad> boundaries;
    boundaries.reserve(6);

    if (bed.isWallCollisionSet(0)) {
        boundaries.push_back({
            {Vector3(minX, minY, minZ), Vector3(minX, maxY, minZ),
             Vector3(minX, maxY, sideMaxZ), Vector3(minX, minY, sideMaxZ)},
            Vector3(-1.0f, 0.0f, 0.0f)});
    }
    if (bed.isWallCollisionSet(1)) {
        boundaries.push_back({
            {Vector3(maxX, maxY, minZ), Vector3(maxX, minY, minZ),
             Vector3(maxX, minY, sideMaxZ), Vector3(maxX, maxY, sideMaxZ)},
            Vector3(1.0f, 0.0f, 0.0f)});
    }
    if (bed.isWallCollisionSet(2)) {
        boundaries.push_back({
            {Vector3(maxX, minY, minZ), Vector3(minX, minY, minZ),
             Vector3(minX, minY, sideMaxZ), Vector3(maxX, minY, sideMaxZ)},
            Vector3(0.0f, -1.0f, 0.0f)});
    }
    if (bed.isWallCollisionSet(3)) {
        boundaries.push_back({
            {Vector3(minX, maxY, minZ), Vector3(maxX, maxY, minZ),
             Vector3(maxX, maxY, sideMaxZ), Vector3(minX, maxY, sideMaxZ)},
            Vector3(0.0f, 1.0f, 0.0f)});
    }
    if (bed.isWallCollisionSet(4)) {
        boundaries.push_back({
            {Vector3(minX, minY, maxZ), Vector3(maxX, minY, maxZ),
             Vector3(maxX, maxY, maxZ), Vector3(minX, maxY, maxZ)},
            Vector3(0.0f, 0.0f, 1.0f)});
    }
    if (bed.isWallCollisionSet(5)) {
        boundaries.push_back({
            {Vector3(minX, maxY, minZ), Vector3(maxX, maxY, minZ),
             Vector3(maxX, minY, minZ), Vector3(minX, minY, minZ)},
            Vector3(0.0f, 0.0f, -1.0f)});
    }

    const auto fillStart = static_cast<std::uint32_t>(geometry.indices.size());
    for (const BoundaryQuad& boundary : boundaries) {
        appendQuad(geometry, boundary.corners, boundary.normal);
    }
    const auto fillCount = static_cast<std::uint32_t>(geometry.indices.size()) -
        fillStart;
    const Vector3 color(
        PrintBedViewDefaults::COLLISION_COLOR_R,
        PrintBedViewDefaults::COLLISION_COLOR_G,
        PrintBedViewDefaults::COLLISION_COLOR_B);
    if (fillCount > 0) {
        geometry.primitives.push_back({
            CollisionFillLabel, fillStart, fillCount, color});
    }

    const auto borderStart = static_cast<std::uint32_t>(geometry.indices.size());
    for (const BoundaryQuad& boundary : boundaries) {
        appendBoundaryFrame(
            geometry, boundary,
            PrintBedViewDefaults::COLLISION_BORDER_WIDTH_MM);
    }
    const auto borderCount =
        static_cast<std::uint32_t>(geometry.indices.size()) - borderStart;
    if (borderCount > 0) {
        geometry.primitives.push_back({
            CollisionBorderLabel, borderStart, borderCount, color});
    }
}

class FilamentPrintBedDBSync final : public IFilamentDBSync {
public:
    FilamentPrintBedDBSync(DBInstanceID id, FilamentSceneAdapter& adapter)
        : m_id(id), m_adapter(adapter) {
        for (auto& resource : m_renderables) {
            resource = std::make_unique<FilamentRenderableResource>(
                adapter.engine(), adapter.scene(), adapter.materialLibrary());
        }
    }

    ~FilamentPrintBedDBSync() override { shutdown(); }

    DBInstanceID objectId() const noexcept override { return m_id; }
    TypeID dbType() const noexcept override { return TypeID::PRINT_BED_DB; }
    void initialize() override { rebuild(); }

    void applyChanges(std::span<const DocumentChange> changes) override {
        const auto bed = db();
        if (!bed || !bed->getVisible()) {
            clearRenderable();
        } else if (isTransformOnly(changes)) {
            updateTransform(*bed);
        } else if (isPresentationOnly(changes)) {
            for (auto& resource : m_renderables) {
                if (resource->valid() && !resource->updateMaterials(
                        materialsFor(*bed, resource->materialLabels()))) {
                    rebuild();
                    break;
                }
            }
        } else {
            rebuild();
        }
    }

    void synchronize(const QSize&) override {
        const auto bed = db();
        if (!bed) return;
        const auto forward = m_adapter.camera().getForwardVector();
        // Filament (x, y, z) corresponds to application (x, -z, y).
        const bool changed = m_viewPolicy.update(
            {forward.x, -forward.z, forward.y}, bed->getTransformMatrix());
        auto& body = *m_renderables[0];
        if (body.setVisible(bed->getVisible() && !m_viewPolicy.bottom())) {
            if (m_viewPolicy.bottom() || !bed->getVisible())
                m_adapter.unbindPickIdentity(body.entityId());
            else
                m_adapter.bindPickIdentity(body.entityId(), m_id);
            m_adapter.markPickGeometryChanged();
        }
        if (changed && m_renderables[1]->valid()) {
            m_renderables[1]->updateMaterials(
                materialsFor(*bed, m_renderables[1]->materialLabels()));
        }
    }

    void shutdown() noexcept override { clearRenderable(); }

private:
    std::shared_ptr<const PrintBedDB> db() const {
        return m_adapter.document()
            ? m_adapter.document()->get<PrintBedDB>(m_id)
            : std::shared_ptr<const PrintBedDB>{};
    }

    static Geometry geometryFor(const PrintBedDB& bed) {
        const auto bounds = bed.localBounds();
        const auto material = bed.getMaterial();
        if (!bounds.valid || !material) return {};
        Geometry geometry = bedModelGeometry(bed, material->getDiffuseColor());
        if (!geometry.valid) {
            geometry = boxGeometry(
                bounds.min, bounds.max, material->getDiffuseColor());
        }
        const Vector3 center = bed.getCenter();
        const Vector3 origin = bed.getOrigin();
        const float minX = center.x - bed.getWidth() * 0.5f;
        const float maxX = center.x + bed.getWidth() * 0.5f;
        const float minY = center.y - bed.getHeight() * 0.5f;
        const float maxY = center.y + bed.getHeight() * 0.5f;

        const std::string texturePath = bed.getBedTexturePath();
        QImageReader textureReader(QString::fromStdString(texturePath));
        if (!texturePath.empty() && textureReader.canRead()) {
            const auto textureStart =
                static_cast<std::uint32_t>(geometry.indices.size());
            const float z = origin.z + BedTextureZOffset;
            appendTexturedQuad(
                geometry,
                {Vector3(minX, minY, z), Vector3(maxX, minY, z),
                 Vector3(maxX, maxY, z), Vector3(minX, maxY, z)},
                Vector3(0.0f, 0.0f, 1.0f));
            geometry.primitives.push_back({
                BedTextureLabel, textureStart,
                static_cast<std::uint32_t>(geometry.indices.size()) -
                    textureStart,
                Vector3(1.0f, 1.0f, 1.0f)});
        }
        if (bed.getShowGrid()) {
            const auto gridStart = static_cast<std::uint32_t>(geometry.indices.size());
            const float z = origin.z + BedGridZOffset;
            const float spacing = std::max(bed.getGridSpacing(), 1.0f);
            constexpr int majorEvery = 5;
            const float thinWidth = std::max(
                0.18f,
                std::min(bed.getWidth(), bed.getHeight()) / 700.0f);
            const float firstX = origin.x +
                std::ceil((minX - origin.x) / spacing) * spacing;
            for (float x = firstX; x <= maxX + 0.001f; x += spacing) {
                const int line = static_cast<int>(
                    std::llround((x - origin.x) / spacing));
                appendLineQuad(
                    geometry, Vector3(x, minY, z), Vector3(x, maxY, z),
                    line % majorEvery == 0 ? thinWidth * 1.8f : thinWidth);
            }
            const float firstY = origin.y +
                std::ceil((minY - origin.y) / spacing) * spacing;
            for (float y = firstY; y <= maxY + 0.001f; y += spacing) {
                const int line = static_cast<int>(
                    std::llround((y - origin.y) / spacing));
                appendLineQuad(
                    geometry, Vector3(minX, y, z), Vector3(maxX, y, z),
                    line % majorEvery == 0 ? thinWidth * 1.8f : thinWidth);
            }
            const auto area = bed.getPrintableArea();
            for (std::size_t index = 0; index < area.size(); ++index) {
                const auto& start = area[index];
                const auto& end = area[(index + 1) % area.size()];
                appendLineQuad(
                    geometry, Vector3(start.x, start.y, z),
                    Vector3(end.x, end.y, z), thinWidth * 2.2f);
            }
            if (geometry.indices.size() > gridStart) {
                geometry.primitives.push_back({
                    BedGridLabel, gridStart,
                    static_cast<std::uint32_t>(geometry.indices.size()) - gridStart,
                    bed.getGridColor()});
            }
        }
        appendCollisionBoundaries(geometry, bed);
        return geometry;
    }

    std::vector<FilamentMaterialDescriptor> materialsFor(
        const PrintBedDB& bed,
        const std::vector<std::uint32_t>& labels) const {
        std::vector<FilamentMaterialDescriptor> materials;
        materials.reserve(labels.size());
        const auto material = bed.getMaterial();
        const Vector3 bedColor = material
            ? material->getDiffuseColor()
            : Vector3(
                  PrintBedViewDefaults::BED_COLOR_R,
                  PrintBedViewDefaults::BED_COLOR_G,
                  PrintBedViewDefaults::BED_COLOR_B);
        for (const std::uint32_t label : labels) {
            const Vector3 authoredColor = label == BedGridLabel
                ? bed.getGridColor()
                : (label == BedTextureLabel
                       ? Vector3(1.0f, 1.0f, 1.0f)
                       : bedColor);
            FilamentMaterialDescriptor parameters =
                filamentMaterialDescriptor(bed, authoredColor);
            if (label == BedTextureLabel) {
                parameters.baseColor = Vector3(1.0f, 1.0f, 1.0f);
                parameters.unlit = true;
                parameters.refractive = false;
                parameters.texturePath = bed.getBedTexturePath();
            } else if (label == BedGridLabel) {
                parameters.baseColor = bed.getGridColor();
                parameters.refractive = false;
                parameters.transparentOverlay = true;
                parameters.alpha = m_viewPolicy.gridOpacity();
            }
            if (label != CollisionFillLabel && label != CollisionBorderLabel) {
                materials.push_back(std::move(parameters));
                continue;
            }
            parameters.baseColor = Vector3(
                PrintBedViewDefaults::COLLISION_COLOR_R,
                PrintBedViewDefaults::COLLISION_COLOR_G,
                PrintBedViewDefaults::COLLISION_COLOR_B);
            parameters.roughness = 0.6f;
            parameters.metallic = 0.0f;
            parameters.refractive = false;
            parameters.transparentOverlay = true;
            parameters.alpha = label == CollisionFillLabel
                ? PrintBedViewDefaults::COLLISION_FILL_OPACITY
                : PrintBedViewDefaults::COLLISION_BORDER_OPACITY;
            materials.push_back(std::move(parameters));
        }
        return materials;
    }

    void rebuild() {
        const auto bed = db();
        if (!bed || !bed->getVisible()) {
            clearRenderable();
            return;
        }
        const auto geometry = geometryFor(*bed);
        if (!geometry.valid) {
            clearRenderable();
            return;
        }
        // Keep surface, grid and collision resources independent. Camera
        // movement changes scene membership/materials, never mesh buffers.
        for (std::size_t group = 0; group < m_renderables.size(); ++group) {
            Geometry part;
            part.valid = true;
            std::vector<std::uint32_t> labels;
            for (const auto& primitive : geometry.primitives) {
                const std::size_t primitiveGroup = primitive.label == BedGridLabel ? 1
                    : ((primitive.label == CollisionFillLabel ||
                        primitive.label == CollisionBorderLabel) ? 2 : 0);
                if (primitiveGroup != group) continue;
                auto copy = primitive;
                copy.indexOffset = static_cast<std::uint32_t>(part.indices.size());
                part.indices.insert(part.indices.end(),
                    geometry.indices.begin() + primitive.indexOffset,
                    geometry.indices.begin() + primitive.indexOffset + primitive.indexCount);
                part.primitives.push_back(copy);
                labels.push_back(primitive.label);
            }
            auto& resource = *m_renderables[group];
            const auto previousEntity = resource.entityId();
            if (part.primitives.empty()) {
                if (previousEntity) m_adapter.unbindPickIdentity(previousEntity);
                resource.reset();
                continue;
            }
            part.vertices = geometry.vertices;
            if (!resource.rebuild(std::move(part), materialsFor(*bed, labels),
                                  bed->getTransformMatrix())) {
                clearRenderable();
                return;
            }
            if (previousEntity) m_adapter.unbindPickIdentity(previousEntity);
            m_adapter.bindPickIdentity(resource.entityId(), m_id);
        }
        m_adapter.markPickGeometryChanged();
        synchronize({});
    }

    bool clearRenderable() noexcept {
        bool changed = false;
        for (auto& resource : m_renderables) {
            if (resource->entityId()) m_adapter.unbindPickIdentity(resource->entityId());
            changed = resource->reset() || changed;
        }
        if (changed) m_adapter.markPickGeometryChanged();
        return changed;
    }

    bool updateTransform(const PrintBedDB& bed) {
        bool changed = false;
        for (auto& resource : m_renderables) {
            if (resource->valid())
                changed = resource->updateTransform(bed.getTransformMatrix()) || changed;
        }
        if (changed) m_adapter.markPickGeometryChanged();
        return changed;
    }

    DBInstanceID m_id;
    FilamentSceneAdapter& m_adapter;
    PrintBedViewPolicy m_viewPolicy;
    std::array<std::unique_ptr<FilamentRenderableResource>, 3> m_renderables;

};

} // namespace

std::unique_ptr<IFilamentDBSync> createFilamentPrintBedDBSync(
    DBInstanceID id,
    FilamentSceneAdapter& adapter) {
    return std::make_unique<FilamentPrintBedDBSync>(id, adapter);
}

} // namespace GPlatform::Rendering
