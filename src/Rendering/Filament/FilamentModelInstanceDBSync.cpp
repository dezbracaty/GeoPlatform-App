#include "FilamentModelInstanceDBSync.hpp"

#include "FilamentActorPresentation.hpp"
#include "FilamentRenderableResource.hpp"
#include "FilamentSceneAdapter.hpp"
#include "FilamentSpace.hpp"
#include "IFilamentDBSync.hpp"

#include "Foundation/Log.h"
#include <IDocumentReadView.hpp>
#include <MaterialDB.hpp>
#include <ModelRenderAppearanceResolver.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelObjectDB.hpp>
#include <ModelPartDB.hpp>
#include <ModelSurfaceColorDB.hpp>
#include <SlicingConfigDB.hpp>
#include <SurfaceColorRenderProjection.hpp>

#include <vtkCell.h>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <gltfio/materials/uberarchive.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace GPlatform::Rendering {
namespace {

constexpr float kNormalLengthEpsilon = 1.0e-12f;
constexpr float kSmoothNormalMinimumDot = 0.5f;

float lengthSquared(const Vector3& value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

float dot(const Vector3& lhs, const Vector3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vector3 cross(const Vector3& lhs, const Vector3& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
}

Vector3 normalized(const Vector3& value) {
    const float squared = lengthSquared(value);
    if (squared <= kNormalLengthEpsilon) return {};
    const float inverseLength = 1.0f / std::sqrt(squared);
    return {
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength};
}

Vector3 add(const Vector3& lhs, const Vector3& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

struct PositionKey {
    std::int64_t x{0};
    std::int64_t y{0};
    std::int64_t z{0};

    bool operator==(const PositionKey& other) const noexcept {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct PositionKeyHash {
    std::size_t operator()(const PositionKey& key) const noexcept {
        std::size_t seed = std::hash<std::int64_t>{}(key.x);
        seed ^= std::hash<std::int64_t>{}(key.y) + 0x9e3779b9u +
            (seed << 6u) + (seed >> 2u);
        seed ^= std::hash<std::int64_t>{}(key.z) + 0x9e3779b9u +
            (seed << 6u) + (seed >> 2u);
        return seed;
    }
};

PositionKey positionKey(const Vector3& value) {
    constexpr double scale = 1000000.0;
    return {
        static_cast<std::int64_t>(std::llround(value.x * scale)),
        static_cast<std::int64_t>(std::llround(value.y * scale)),
        static_cast<std::int64_t>(std::llround(value.z * scale))};
}

struct TriangleRecord {
    std::array<Vector3, 3> positions;
    std::array<vtkIdType, 3> pointIds;
    Vector3 faceNormal;
};

std::optional<Vector3> pointNormal(
    vtkDataArray* normals, vtkIdType pointId) {
    if (!normals || pointId < 0 || pointId >= normals->GetNumberOfTuples() ||
        normals->GetNumberOfComponents() < 3) {
        return std::nullopt;
    }
    const Vector3 result = normalized({
        static_cast<float>(normals->GetComponent(pointId, 0)),
        static_cast<float>(normals->GetComponent(pointId, 1)),
        static_cast<float>(normals->GetComponent(pointId, 2))});
    return lengthSquared(result) > kNormalLengthEpsilon
        ? std::optional<Vector3>(result)
        : std::nullopt;
}

FilamentGeometry buildPartGeometry(
    const ModelGeometryDB::ReadHandle& readHandle,
    const Vector3& displayColor) {
    FilamentGeometry result;
    auto* polyData = readHandle
        ? const_cast<vtkPolyData*>(&readHandle.polyData())
        : nullptr;
    if (!polyData || !polyData->GetPoints() ||
        polyData->GetNumberOfCells() <= 0) {
        result.error = "model part has no triangle geometry";
        return result;
    }

    std::vector<TriangleRecord> triangles;
    triangles.reserve(static_cast<std::size_t>(polyData->GetNumberOfCells()));
    std::unordered_map<PositionKey, std::vector<Vector3>, PositionKeyHash>
        adjacentFaceNormals;
    adjacentFaceNormals.reserve(
        static_cast<std::size_t>(polyData->GetNumberOfPoints()));

    for (vtkIdType cellId = 0; cellId < polyData->GetNumberOfCells(); ++cellId) {
        vtkCell* cell = polyData->GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() != 3) continue;
        TriangleRecord triangle;
        for (int corner = 0; corner < 3; ++corner) {
            triangle.pointIds[corner] = cell->GetPointId(corner);
            double raw[3]{};
            polyData->GetPoint(triangle.pointIds[corner], raw);
            triangle.positions[corner] = {
                static_cast<float>(raw[0]),
                static_cast<float>(raw[1]),
                static_cast<float>(raw[2])};
        }
        triangle.faceNormal = normalized(cross(
            {
                triangle.positions[1].x - triangle.positions[0].x,
                triangle.positions[1].y - triangle.positions[0].y,
                triangle.positions[1].z - triangle.positions[0].z},
            {
                triangle.positions[2].x - triangle.positions[0].x,
                triangle.positions[2].y - triangle.positions[0].y,
                triangle.positions[2].z - triangle.positions[0].z}));
        if (lengthSquared(triangle.faceNormal) <= kNormalLengthEpsilon) {
            continue;
        }
        for (const auto& position : triangle.positions) {
            adjacentFaceNormals[positionKey(position)].push_back(
                triangle.faceNormal);
        }
        triangles.push_back(triangle);
    }

    if (triangles.empty()) {
        result.error = "model part contains no valid triangles";
        return result;
    }
    if (triangles.size() >
        std::numeric_limits<std::uint32_t>::max() / 3u) {
        result.error = "model part exceeds Filament index limits";
        return result;
    }

    vtkDataArray* authoredNormals = polyData->GetPointData()
        ? polyData->GetPointData()->GetNormals()
        : nullptr;
    result.vertices.reserve(triangles.size() * 3u);
    result.indices.reserve(triangles.size() * 3u);
    for (const auto& triangle : triangles) {
        for (int corner = 0; corner < 3; ++corner) {
            Vector3 normal;
            if (const auto authored = pointNormal(
                    authoredNormals, triangle.pointIds[corner])) {
                normal = *authored;
            } else {
                const auto found = adjacentFaceNormals.find(
                    positionKey(triangle.positions[corner]));
                if (found != adjacentFaceNormals.end()) {
                    for (const auto& candidate : found->second) {
                        if (dot(triangle.faceNormal, candidate) >=
                            kSmoothNormalMinimumDot) {
                            normal = add(normal, candidate);
                        }
                    }
                }
                normal = normalized(normal);
                if (lengthSquared(normal) <= kNormalLengthEpsilon) {
                    normal = triangle.faceNormal;
                }
            }
            result.indices.push_back(
                static_cast<std::uint32_t>(result.vertices.size()));
            result.vertices.push_back({
                toFilamentSpace(triangle.positions[corner]),
                toFilamentTangentFrame(normal)});
        }
    }

    result.primitives.push_back({
        0,
        0,
        static_cast<std::uint32_t>(result.indices.size()),
        displayColor});
    result.valid = true;
    return result;
}

std::vector<GeomTriangle> partTriangles(
    const ModelGeometryDB& geometry,
    std::uint64_t* geometryRevision = nullptr) {
    std::vector<GeomTriangle> result;
    const auto readHandle = geometry.read();
    auto* polyData = readHandle
        ? const_cast<vtkPolyData*>(&readHandle.polyData())
        : nullptr;
    if (!polyData) return result;
    if (geometryRevision) *geometryRevision = readHandle.revision();
    result.reserve(static_cast<std::size_t>(polyData->GetNumberOfCells()));
    for (vtkIdType cellId = 0; cellId < polyData->GetNumberOfCells(); ++cellId) {
        vtkCell* cell = polyData->GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() != 3) continue;
        Vector3 points[3];
        for (int corner = 0; corner < 3; ++corner) {
            double raw[3]{};
            polyData->GetPoint(cell->GetPointId(corner), raw);
            points[corner] = {
                static_cast<float>(raw[0]),
                static_cast<float>(raw[1]),
                static_cast<float>(raw[2])};
        }
        result.emplace_back(points[0], points[1], points[2]);
    }
    return result;
}

Vector3 rgb(const Color& color) {
    return {color.r, color.g, color.b};
}

FilamentGeometry buildSurfaceGeometry(
    const SurfaceColorRenderProjectionResult& source,
    const std::vector<Color>& palette,
    const Color& baseColor) {
    FilamentGeometry result;
    if (!source.valid) {
        result.error = source.error;
        return result;
    }
    if (source.points.size() != source.normals.size() ||
        source.points.size() % 3u != 0u ||
        source.cellOffsets.size() != source.labels.size() + 1u) {
        result.error = "surface-color arrays do not match";
        return result;
    }
    const std::size_t vertexCount = source.points.size() / 3u;
    if (vertexCount > std::numeric_limits<std::uint32_t>::max()) {
        result.error = "surface-color geometry exceeds Filament limits";
        return result;
    }
    result.vertices.reserve(vertexCount);
    for (std::size_t index = 0; index < vertexCount; ++index) {
        const std::size_t offset = index * 3u;
        const Vector3 position(
            source.points[offset], source.points[offset + 1u],
            source.points[offset + 2u]);
        const Vector3 normal(
            source.normals[offset], source.normals[offset + 1u],
            source.normals[offset + 2u]);
        result.vertices.push_back({
            toFilamentSpace(position), toFilamentTangentFrame(normal)});
    }

    std::map<std::uint32_t, std::vector<std::uint32_t>> indicesByLabel;
    for (std::size_t cell = 0; cell < source.labels.size(); ++cell) {
        const std::int64_t begin = source.cellOffsets[cell];
        const std::int64_t end = source.cellOffsets[cell + 1u];
        if (begin < 0 || end < begin || end - begin != 3 ||
            static_cast<std::size_t>(end) > source.connectivity.size()) {
            result.error = "surface-color data contains a non-triangle cell";
            return result;
        }
        auto& destination = indicesByLabel[source.labels[cell]];
        for (std::int64_t offset = begin; offset < end; ++offset) {
            const std::int64_t vertex =
                source.connectivity[static_cast<std::size_t>(offset)];
            if (vertex < 0 || static_cast<std::size_t>(vertex) >= vertexCount) {
                result.error = "surface-color data references an invalid vertex";
                return result;
            }
            destination.push_back(static_cast<std::uint32_t>(vertex));
        }
    }
    for (auto& [label, indices] : indicesByLabel) {
        if (indices.empty()) continue;
        const auto offset = static_cast<std::uint32_t>(result.indices.size());
        result.indices.insert(
            result.indices.end(), indices.begin(), indices.end());
        result.primitives.push_back({
            label,
            offset,
            static_cast<std::uint32_t>(indices.size()),
            rgb(ModelSurfaceColorDB::colorForLabel(
                label, palette, baseColor))});
    }
    result.valid = !result.vertices.empty() && !result.indices.empty() &&
        !result.primitives.empty();
    if (!result.valid) result.error = "surface-color geometry is empty";
    return result;
}

FilamentMaterialDescriptor partMaterial(
    const MaterialDB* material, const ModelInstanceDB& instance,
    const Vector3& displayColor) {
    return material
        ? filamentMaterialDescriptor(
              *material, instance.getOpacity(), displayColor)
        : FilamentMaterialDescriptor{};
}

struct ProjectionIdentity {
    DBInstanceID surfaceColorId{};
    DBInstanceID partId{};
    int surfaceRevision{-1};
    std::uint64_t geometryRevision{0};
    std::uint64_t token{0};

    bool sameSource(const ProjectionIdentity& other) const noexcept {
        return surfaceColorId == other.surfaceColorId &&
            partId == other.partId &&
            surfaceRevision == other.surfaceRevision &&
            geometryRevision == other.geometryRevision;
    }
};

struct ProjectionCompletion {
    ProjectionIdentity identity;
    std::shared_ptr<const SurfaceColorRenderProjectionResult> projection;
};

struct ProjectionMailbox {
    std::mutex mutex;
    bool accepting{true};
    std::vector<ProjectionCompletion> completions;
    std::function<void()> requestFrame;
};

class FilamentModelInstanceDBSync final : public IFilamentDBSync {
public:
    FilamentModelInstanceDBSync(
        DBInstanceID id, FilamentSceneAdapter& adapter)
        : m_id(id),
          m_adapter(adapter),
          m_mailbox(std::make_shared<ProjectionMailbox>()) {
        m_mailbox->requestFrame = [&adapter] { adapter.requestFrame(); };
    }

    ~FilamentModelInstanceDBSync() override { shutdown(); }

    DBInstanceID objectId() const noexcept override { return m_id; }
    TypeID dbType() const noexcept override {
        return TypeID::MODEL_INSTANCE_DB;
    }

    void initialize() override { rebuildAll(); }

    void applyChanges(std::span<const DocumentChange> changes) override {
        if (m_shutdown) return;
        bool rebuild = false;
        bool updateTransform = false;
        bool updatePresentation = false;

        for (const auto& change : changes) {
            if (change.changeType != ChangeType::PROPERTY_CHANGED ||
                change.propertyName.empty()) {
                rebuild = true;
                continue;
            }
            const std::string& property = change.propertyName;
            if (property == "Transform") {
                updateTransform = true;
            } else if (property == "Opacity" ||
                       property == "SlicingConfigDBId" ||
                       property ==
                           ModelInstanceDB::kSlicingConfigRelation) {
                updatePresentation = true;
            } else if (property == "Visible" ||
                       property == ModelInstanceDB::kObjectRelation ||
                       property == "PartSet") {
                rebuild = true;
            }
        }

        if (rebuild) {
            rebuildAll();
            return;
        }
        const auto model = instance();
        if (!model || !model->getVisible() || !model->isValid()) {
            cancelProjection();
            clearAllPresentations();
            return;
        }
        if (updateTransform) updateTransforms(*model);
        if (updatePresentation) updateMaterials(*model);
    }

    void synchronize(const QSize&) override {
        if (m_shutdown || !m_mailbox || !m_adapter.document()) return;
        std::vector<ProjectionCompletion> completions;
        {
            std::lock_guard lock(m_mailbox->mutex);
            completions.swap(m_mailbox->completions);
        }
        for (auto& completion : completions) {
            applyCompletion(std::move(completion));
        }
    }

    void shutdown() noexcept override {
        if (m_shutdown) return;
        m_shutdown = true;
        cancelProjection();
        if (m_mailbox) {
            std::lock_guard lock(m_mailbox->mutex);
            m_mailbox->accepting = false;
            m_mailbox->requestFrame = {};
            m_mailbox->completions.clear();
        }
        clearAllPresentations();
        m_mailbox.reset();
    }

private:
    struct PartPresentation {
        DBInstanceID partId;
        DBInstanceID geometryId;
        DBInstanceID materialId;
        DBInstanceID surfaceColorId;
        ModelGeometryDB::ReadHandle geometry;
        std::unique_ptr<FilamentRenderableResource> renderable;
        ProjectionIdentity surfaceIdentity;
        std::shared_ptr<const SurfaceColorRenderProjectionResult>
            surfaceProjection;
    };

    std::shared_ptr<const ModelInstanceDB> instance() const {
        return m_adapter.document()
            ? m_adapter.document()->get<ModelInstanceDB>(m_id)
            : nullptr;
    }

    std::shared_ptr<const ModelObjectDB> object(
        const ModelInstanceDB& model) const {
        return m_adapter.document()
            ? m_adapter.document()->get<ModelObjectDB>(
                  m_adapter.document()->getOwner(model.getDBInstanceID()))
            : nullptr;
    }

    std::vector<std::shared_ptr<const ModelPartDB>> parts(
        const ModelInstanceDB& model) const {
        std::vector<std::shared_ptr<const ModelPartDB>> result;
        if (!m_adapter.document()) return result;
        const DBInstanceID objectId = m_adapter.document()->getOwner(
            model.getDBInstanceID());
        if (!objectId.isValid()) return result;
        for (const DBInstanceID partId :
             m_adapter.document()->getOwnedChildren(
                 objectId, ModelPartDB::kObjectRelation)) {
            if (const auto part =
                    m_adapter.document()->get<ModelPartDB>(partId)) {
                result.push_back(part);
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& lhs,
                                                   const auto& rhs) {
            if (lhs->getOrderIndex() != rhs->getOrderIndex()) {
                return lhs->getOrderIndex() < rhs->getOrderIndex();
            }
            return lhs->getDBInstanceID().getValue() <
                rhs->getDBInstanceID().getValue();
        });
        return result;
    }

    std::shared_ptr<const ModelGeometryDB> geometry(
        DBInstanceID partId) const {
        if (!m_adapter.document()) return {};
        const auto part = m_adapter.document()->get<ModelPartDB>(partId);
        if (!part) return {};
        const auto ids = m_adapter.document()->getOwnedChildren(
            partId, ModelPartDB::kGeometryRelation);
        return ids.empty() ? std::shared_ptr<const ModelGeometryDB>{}
                           : m_adapter.document()->get<ModelGeometryDB>(
                                 ids.front());
    }

    std::shared_ptr<const MaterialDB> material(
        DBInstanceID partId) const {
        if (!m_adapter.document()) return {};
        const auto part = m_adapter.document()->get<ModelPartDB>(partId);
        if (!part) return {};
        const auto ids = m_adapter.document()->getOwnedChildren(
            partId, ModelPartDB::kMaterialRelation);
        return ids.empty() ? std::shared_ptr<const MaterialDB>{}
                           : m_adapter.document()->get<MaterialDB>(ids.front());
    }

    std::shared_ptr<const ModelSurfaceColorDB> surfaceColors(
        DBInstanceID partId) const {
        if (!m_adapter.document()) return {};
        const auto part = m_adapter.document()->get<ModelPartDB>(partId);
        if (!part) return {};
        const auto ids = m_adapter.document()->getOwnedChildren(
            partId, ModelPartDB::kSurfaceColorsRelation);
        return ids.empty() ? std::shared_ptr<const ModelSurfaceColorDB>{}
                           : m_adapter.document()->get<ModelSurfaceColorDB>(
                                 ids.front());
    }

    std::vector<Color> activePalette(
        const ModelInstanceDB& model) const {
        const auto config = slicingConfig(model);
        if (config) {
            std::vector<Color> result = config->filamentColors();
            if (!result.empty()) return result;
        }
        return {};
    }

    std::shared_ptr<const GPlatform::SlicingConfigDB> slicingConfig(
        const ModelInstanceDB& model) const {
        return m_adapter.document()
            ? m_adapter.document()->get<GPlatform::SlicingConfigDB>(
                  model.getSlicingConfigDBId())
            : nullptr;
    }

    std::vector<FilamentMaterialDescriptor> surfaceMaterials(
        const ModelInstanceDB& model,
        const ModelPartDB& part,
        const std::vector<std::uint32_t>& labels) const {
        const auto partMaterialDB = material(part.getDBInstanceID());
        if (!partMaterialDB) return {};
        const auto palette = activePalette(model);
        const Vector3 displayColor =
            GPlatform::Rendering::resolveModelDisplayColor(
            part, partMaterialDB.get(), slicingConfig(model).get());
        std::vector<FilamentMaterialDescriptor> result;
        result.reserve(labels.size());
        for (const std::uint32_t label : labels) {
            result.push_back(filamentMaterialDescriptor(
                *partMaterialDB, model.getOpacity(),
                rgb(ModelSurfaceColorDB::colorForLabel(
                    label, palette,
                    {displayColor.x,
                     displayColor.y,
                     displayColor.z, 1.0f}))));
        }
        return result;
    }

    void rebuildAll() {
        cancelProjection();
        clearAllPresentations();
        const auto model = instance();
        if (!model || !model->getVisible()) return;
        const auto modelParts = parts(*model);
        const auto modelObject = object(*model);
        if (modelObject) {
            if (const auto sourceAppearance =
                    GPlatform::Rendering::resolveModelSourceAppearance(
                        *modelObject, modelParts)) {
                if (buildNativeGlb(*model, sourceAppearance)) return;
            }
        }
        buildBasePresentations(*model);
        scheduleSurfaceProjection();
    }

    bool buildNativeGlb(
        const ModelInstanceDB& model,
        std::shared_ptr<const GlbAssetSnapshot> snapshot) {
        if (!snapshot || !*snapshot) return false;
        const auto& bytes = snapshot->asset->bytes();
        if (bytes.empty() ||
            bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }

        auto& engine = m_adapter.engine();
        m_nativeMaterials = filament::gltfio::createUbershaderProvider(
            &engine, UBERARCHIVE_DEFAULT_DATA, UBERARCHIVE_DEFAULT_SIZE);
        if (!m_nativeMaterials) return false;
        filament::gltfio::AssetConfiguration assetConfig{};
        assetConfig.engine = &engine;
        assetConfig.materials = m_nativeMaterials;
        m_nativeLoader = filament::gltfio::AssetLoader::create(assetConfig);
        if (!m_nativeLoader) {
            releaseNativeGlb();
            return false;
        }
        m_nativeAsset = m_nativeLoader->createAsset(
            bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        if (!m_nativeAsset) {
            releaseNativeGlb();
            return false;
        }

        filament::gltfio::ResourceConfiguration resourceConfig{};
        resourceConfig.engine = &engine;
        resourceConfig.normalizeSkinningWeights = true;
        m_nativeResources =
            std::make_unique<filament::gltfio::ResourceLoader>(
                resourceConfig);
        m_nativeStbTextures = filament::gltfio::createStbProvider(&engine);
        m_nativeKtx2Textures = filament::gltfio::createKtx2Provider(&engine);
        if (m_nativeStbTextures) {
            m_nativeResources->addTextureProvider(
                "image/png", m_nativeStbTextures);
            m_nativeResources->addTextureProvider(
                "image/jpeg", m_nativeStbTextures);
        }
        if (m_nativeKtx2Textures) {
            m_nativeResources->addTextureProvider(
                "image/ktx2", m_nativeKtx2Textures);
        }
        if (!m_nativeResources->loadResources(m_nativeAsset)) {
            releaseNativeGlb();
            return false;
        }
        m_nativeAsset->releaseSourceData();
        const auto bounds = m_nativeAsset->getBoundingBox();
        m_nativeSourceCenter = bounds.isEmpty()
            ? filament::math::float3{}
            : bounds.center();
        const auto modelParts = parts(model);
        m_nativePartId = modelParts.empty()
            ? DBInstanceID{} : modelParts.front()->getDBInstanceID();
        const auto* entities = m_nativeAsset->getRenderableEntities();
        const auto count = m_nativeAsset->getRenderableEntityCount();
        for (std::size_t index = 0; index < count; ++index) {
            m_adapter.bindPickIdentity(
                entities[index].getId(), m_id, m_nativePartId);
        }
        updateNativeGlbTransform(model);
        m_adapter.scene().addEntities(entities, count);
        m_nativeInScene = true;
        m_nativeSnapshot = std::move(snapshot);
        m_adapter.markPickGeometryChanged();
        m_adapter.requestFrame();
        LOG_INFO(
            "FilamentModelInstanceDBSync: loaded authored GLB model={} renderables={} bytes={}",
            m_id.getValue(), count, bytes.size());
        return true;
    }

    void updateNativeGlbTransform(const ModelInstanceDB& model) {
        if (!m_nativeAsset) return;
        auto& transforms = m_adapter.engine().getTransformManager();
        const auto root = transforms.getInstance(m_nativeAsset->getRoot());
        if (!root) return;
        const auto centerTranslation = filament::math::mat4f::translation(
            -m_nativeSourceCenter);
        constexpr float kMetersToMillimeters = 1000.0f;
        const auto sourceScale = filament::math::mat4f::scaling(
            filament::math::float3{kMetersToMillimeters});
        transforms.setTransform(
            root,
            toFilamentTransform(model.getTransformMatrix()) *
                sourceScale * centerTranslation);
        m_adapter.markPickGeometryChanged();
    }

    bool releaseNativeGlb() noexcept {
        const bool changed = m_nativeAsset != nullptr;
        if (m_nativeAsset) {
            if (m_nativeInScene) {
                m_adapter.scene().removeEntities(
                    m_nativeAsset->getRenderableEntities(),
                    m_nativeAsset->getRenderableEntityCount());
                m_nativeInScene = false;
            }
            const auto* entities = m_nativeAsset->getRenderableEntities();
            for (std::size_t index = 0;
                 index < m_nativeAsset->getRenderableEntityCount(); ++index) {
                m_adapter.unbindPickIdentity(entities[index].getId());
            }
        }
        if (m_nativeLoader && m_nativeAsset) {
            m_nativeLoader->destroyAsset(m_nativeAsset);
        }
        m_nativeAsset = nullptr;
        m_nativeResources.reset();
        if (m_nativeMaterials) {
            m_nativeMaterials->destroyMaterials();
            delete m_nativeMaterials;
            m_nativeMaterials = nullptr;
        }
        delete m_nativeKtx2Textures;
        m_nativeKtx2Textures = nullptr;
        delete m_nativeStbTextures;
        m_nativeStbTextures = nullptr;
        if (m_nativeLoader) {
            filament::gltfio::AssetLoader::destroy(&m_nativeLoader);
        }
        m_nativeSnapshot.reset();
        m_nativeSourceCenter = {};
        m_nativePartId = DBInstanceID{};
        return changed;
    }

    void buildBasePresentations(const ModelInstanceDB& model) {
        bool created = false;
        for (const auto& part : parts(model)) {
            if (!part) continue;
            const auto geometryDB = geometry(part->getDBInstanceID());
            const auto materialDB = material(part->getDBInstanceID());
            if (!geometryDB || !geometryDB->isValid() || !materialDB) continue;
            auto readHandle = geometryDB
                ? geometryDB->read()
                : ModelGeometryDB::ReadHandle{};
            const Vector3 displayColor =
                GPlatform::Rendering::resolveModelDisplayColor(
                *part, materialDB.get(), slicingConfig(model).get());
            FilamentGeometry filamentGeometry = buildPartGeometry(
                readHandle, displayColor);
            if (!filamentGeometry.valid) {
                LOG_ERROR(
                    "FilamentModelInstanceDBSync: geometry conversion failed model={} part={} error={}",
                    m_id.getValue(), part->getDBInstanceID().getValue(),
                    filamentGeometry.error);
                continue;
            }
            auto renderable = std::make_unique<FilamentRenderableResource>(
                m_adapter.engine(), m_adapter.scene(),
                m_adapter.materialLibrary());
            const Transform::Matrix4 transform =
                model.getTransform().getMatrix() *
                part->getLocalTransform().getMatrix();
            if (!renderable->rebuild(
                    std::move(filamentGeometry),
                    {partMaterial(materialDB.get(), model, displayColor)},
                    transform)) {
                LOG_ERROR(
                    "FilamentModelInstanceDBSync: renderable creation failed model={} part={}",
                    m_id.getValue(), part->getDBInstanceID().getValue());
                continue;
            }
            m_adapter.bindPickIdentity(
                renderable->entityId(), m_id, part->getDBInstanceID());
            PartPresentation presentation;
            presentation.partId = part->getDBInstanceID();
            presentation.geometryId = geometryDB->getDBInstanceID();
            presentation.materialId = materialDB->getDBInstanceID();
            if (const auto colors = surfaceColors(part->getDBInstanceID())) {
                presentation.surfaceColorId = colors->getDBInstanceID();
            }
            presentation.geometry = std::move(readHandle);
            presentation.renderable = std::move(renderable);
            m_parts.push_back(std::move(presentation));
            created = true;
        }
        if (created) m_adapter.markPickGeometryChanged();
    }

    void updateTransforms(const ModelInstanceDB& model) {
        if (m_nativeAsset) {
            updateNativeGlbTransform(model);
            return;
        }
        bool changed = false;
        for (auto& presentation : m_parts) {
            const auto part = m_adapter.document()->get<ModelPartDB>(
                presentation.partId);
            if (!part) continue;
            changed = presentation.renderable->updateTransform(
                          model.getTransform().getMatrix() *
                          part->getLocalTransform().getMatrix()) ||
                changed;
        }
        if (changed) m_adapter.markPickGeometryChanged();
    }

    void updateMaterials(const ModelInstanceDB& model) {
        if (m_nativeAsset) return;
        bool failed = false;
        for (auto& presentation : m_parts) {
            const auto part = m_adapter.document()->get<ModelPartDB>(
                presentation.partId);
            const auto materialDB = material(presentation.partId);
            if (!part) {
                failed = true;
                continue;
            }
            const auto materials = presentation.surfaceProjection
                ? surfaceMaterials(
                      model, *part,
                      presentation.renderable->materialLabels())
                : std::vector<FilamentMaterialDescriptor>{
                      partMaterial(
                          materialDB.get(), model,
                          GPlatform::Rendering::resolveModelDisplayColor(
                              *part, materialDB.get(),
                              slicingConfig(model).get()))};
            if (!presentation.renderable->updateMaterials(materials)) {
                failed = true;
            }
        }
        if (failed) rebuildAll();
    }

    PartPresentation* findPresentation(DBInstanceID partId) {
        const auto found = std::find_if(
            m_parts.begin(), m_parts.end(),
            [partId](const auto& value) { return value.partId == partId; });
        return found == m_parts.end() ? nullptr : &*found;
    }

    void scheduleSurfaceProjection() {
        const auto model = instance();
        if (!model || !model->getVisible()) return;
        for (const auto& part : parts(*model)) {
            if (part && geometry(part->getDBInstanceID())) {
                schedulePartSurfaceProjection(*model, *part);
            }
        }
    }

    void schedulePartSurfaceProjection(
        const ModelInstanceDB& model, const ModelPartDB& part) {
        auto* presentation = findPresentation(part.getDBInstanceID());
        const auto colors = surfaceColors(part.getDBInstanceID());
        if (!presentation || !colors ||
            colors->getSurfaceColorData().empty()) {
            restoreBasePresentation(model, part);
            return;
        }
        std::uint64_t geometryRevision = 0;
        const auto geometryDB = geometry(part.getDBInstanceID());
        if (!geometryDB) {
            restoreBasePresentation(model, part);
            return;
        }
        const auto triangles = partTriangles(
            *geometryDB, &geometryRevision);
        ProjectionIdentity identity{
            colors->getDBInstanceID(),
            part.getDBInstanceID(),
            colors->getRevision(),
            geometryRevision,
            ++m_nextProjectionToken};

        if (presentation->surfaceProjection &&
            presentation->surfaceIdentity.sameSource(identity)) {
            updateMaterials(model);
            return;
        }
        const auto active = m_activeProjections.find(
            part.getDBInstanceID().getValue());
        if (active != m_activeProjections.end() &&
            active->second.sameSource(identity)) {
            return;
        }

        const auto key = makeSurfaceColorRenderProjectionKey(
            colors->getTopologyFingerprint(), colors->getSurfaceColorData(),
            colors->getSourceTriangleCount());
        if (const auto cached = findSurfaceColorRenderProjection(key)) {
            identity.token = ++m_nextProjectionToken;
            replaceWithSurfaceProjection(
                model, part, cached, identity);
            return;
        }

        identity.token = ++m_nextProjectionToken;
        m_activeProjections[part.getDBInstanceID().getValue()] = identity;
        const std::string encodedState = colors->getSurfaceColorData();
        const std::string fingerprint = colors->getTopologyFingerprint();
        const int sourceTriangleCount = colors->getSourceTriangleCount();
        std::weak_ptr<ProjectionMailbox> weakMailbox = m_mailbox;
        const auto disposition = requestSurfaceColorRenderProjection(
            {key,
             [triangles, encodedState, fingerprint, sourceTriangleCount]() {
                 return SurfaceColorRenderProjectionInput{
                     triangles, encodedState, fingerprint,
                     sourceTriangleCount};
             }},
            [weakMailbox, identity](
                std::shared_ptr<const SurfaceColorRenderProjectionResult>
                    projection) {
                const auto mailbox = weakMailbox.lock();
                if (!mailbox) return;
                std::function<void()> requestFrame;
                {
                    std::lock_guard lock(mailbox->mutex);
                    if (!mailbox->accepting) return;
                    mailbox->completions.push_back(
                        {identity, std::move(projection)});
                    requestFrame = mailbox->requestFrame;
                }
                if (requestFrame) requestFrame();
            });
        if (disposition ==
            SurfaceColorRenderProjectionRequestDisposition::Rejected) {
            LOG_ERROR(
                "FilamentModelInstanceDBSync: surface projection request rejected model={} part={}",
                m_id.getValue(), part.getDBInstanceID().getValue());
            m_activeProjections.erase(part.getDBInstanceID().getValue());
        }
    }

    void applyCompletion(ProjectionCompletion completion) {
        const auto active = m_activeProjections.find(
            completion.identity.partId.getValue());
        if (active == m_activeProjections.end() ||
            completion.identity.token != active->second.token) {
            return;
        }
        const auto model = instance();
        const auto colors = surfaceColors(completion.identity.partId);
        const auto part = m_adapter.document()->get<ModelPartDB>(
            completion.identity.partId);
        const auto geometryDB = geometry(completion.identity.partId);
        const auto geometryHandle = geometryDB
            ? geometryDB->read()
            : ModelGeometryDB::ReadHandle{};
        if (!model || !model->getVisible() || !colors || !part ||
            colors->getDBInstanceID() != completion.identity.surfaceColorId ||
            colors->getRevision() != completion.identity.surfaceRevision ||
            !geometryHandle ||
            geometryHandle.revision() !=
                completion.identity.geometryRevision) {
            m_activeProjections.erase(completion.identity.partId.getValue());
            return;
        }
        m_activeProjections.erase(completion.identity.partId.getValue());
        if (!completion.projection || !completion.projection->valid ||
            !replaceWithSurfaceProjection(
                *model, *part, completion.projection,
                completion.identity)) {
            LOG_ERROR(
                "FilamentModelInstanceDBSync: surface projection failed model={} part={} revision={}",
                m_id.getValue(), part->getDBInstanceID().getValue(),
                colors->getRevision());
        }
    }

    bool replaceWithSurfaceProjection(
        const ModelInstanceDB& model,
        const ModelPartDB& part,
        const std::shared_ptr<const SurfaceColorRenderProjectionResult>& projection,
        const ProjectionIdentity& identity) {
        if (!projection || !projection->valid) return false;
        auto* presentation = findPresentation(part.getDBInstanceID());
        if (!presentation) return false;
        const auto partMaterialDB = material(part.getDBInstanceID());
        if (!partMaterialDB) return false;
        const Vector3 displayColor =
            GPlatform::Rendering::resolveModelDisplayColor(
            part, partMaterialDB.get(), slicingConfig(model).get());
        FilamentGeometry geometry = buildSurfaceGeometry(
            *projection, activePalette(model),
            {displayColor.x, displayColor.y, displayColor.z, 1.0f});
        if (!geometry.valid) {
            LOG_ERROR(
                "FilamentModelInstanceDBSync: surface geometry conversion failed model={} error={}",
                m_id.getValue(), geometry.error);
            return false;
        }
        std::vector<std::uint32_t> labels;
        labels.reserve(geometry.primitives.size());
        for (const auto& primitive : geometry.primitives) {
            labels.push_back(primitive.label);
        }
        const auto materials = surfaceMaterials(model, part, labels);
        if (materials.size() != geometry.primitives.size()) return false;

        auto replacement = std::make_unique<FilamentRenderableResource>(
            m_adapter.engine(), m_adapter.scene(),
            m_adapter.materialLibrary());
        if (!replacement->rebuild(
                std::move(geometry), materials,
                model.getTransform().getMatrix() *
                    part.getLocalTransform().getMatrix())) {
            return false;
        }

        if (presentation->renderable) {
            m_adapter.unbindPickIdentity(
                presentation->renderable->entityId());
            presentation->renderable->reset();
        }
        presentation->renderable = std::move(replacement);
        presentation->surfaceColorId = identity.surfaceColorId;
        presentation->surfaceIdentity = identity;
        presentation->surfaceProjection = projection;
        m_adapter.bindPickIdentity(
            presentation->renderable->entityId(), m_id,
            part.getDBInstanceID());
        m_adapter.markPickGeometryChanged();
        return true;
    }

    void restoreBasePresentation(
        const ModelInstanceDB& model, const ModelPartDB& part) {
        auto* presentation = findPresentation(part.getDBInstanceID());
        const auto colors = surfaceColors(part.getDBInstanceID());
        if (!presentation || !presentation->surfaceProjection) {
            if (presentation) {
                presentation->surfaceColorId = colors
                    ? colors->getDBInstanceID()
                    : DBInstanceID{};
            }
            return;
        }
        const auto geometryDB = geometry(part.getDBInstanceID());
        const auto materialDB = material(part.getDBInstanceID());
        auto readHandle = geometryDB
            ? geometryDB->read()
            : ModelGeometryDB::ReadHandle{};
        const Vector3 displayColor = materialDB
            ? GPlatform::Rendering::resolveModelDisplayColor(
                  part, materialDB.get(), slicingConfig(model).get())
            : Vector3(0.8f, 0.8f, 0.8f);
        FilamentGeometry filamentGeometry = buildPartGeometry(
            readHandle, displayColor);
        if (!filamentGeometry.valid || !materialDB) return;
        auto replacement = std::make_unique<FilamentRenderableResource>(
            m_adapter.engine(), m_adapter.scene(),
            m_adapter.materialLibrary());
        if (!replacement->rebuild(
                std::move(filamentGeometry),
                {partMaterial(materialDB.get(), model, displayColor)},
                model.getTransform().getMatrix() *
                    part.getLocalTransform().getMatrix())) {
            return;
        }
        if (presentation->renderable) {
            m_adapter.unbindPickIdentity(
                presentation->renderable->entityId());
            presentation->renderable->reset();
        }
        presentation->geometry = std::move(readHandle);
        presentation->geometryId = geometryDB
            ? geometryDB->getDBInstanceID()
            : DBInstanceID{};
        presentation->renderable = std::move(replacement);
        presentation->surfaceColorId = colors
            ? colors->getDBInstanceID()
            : DBInstanceID{};
        presentation->surfaceIdentity = ProjectionIdentity{};
        presentation->surfaceProjection.reset();
        m_adapter.bindPickIdentity(
            presentation->renderable->entityId(), m_id,
            part.getDBInstanceID());
        m_adapter.markPickGeometryChanged();
    }

    void cancelProjection() {
        m_activeProjections.clear();
        ++m_nextProjectionToken;
    }

    bool clearBasePresentations() noexcept {
        bool changed = false;
        for (auto& presentation : m_parts) {
            if (presentation.renderable) {
                const std::uint32_t entity =
                    presentation.renderable->entityId();
                if (entity != 0) m_adapter.unbindPickIdentity(entity);
                changed = presentation.renderable->reset() || changed;
            }
        }
        m_parts.clear();
        return changed;
    }

    void clearAllPresentations() noexcept {
        bool changed = clearBasePresentations();
        changed = releaseNativeGlb() || changed;
        if (changed) m_adapter.markPickGeometryChanged();
    }

    DBInstanceID m_id;
    FilamentSceneAdapter& m_adapter;
    std::vector<PartPresentation> m_parts;
    std::unordered_map<DBInstanceID::ValueType, ProjectionIdentity>
        m_activeProjections;
    std::shared_ptr<ProjectionMailbox> m_mailbox;
    std::shared_ptr<const GlbAssetSnapshot> m_nativeSnapshot;
    filament::gltfio::MaterialProvider* m_nativeMaterials{nullptr};
    filament::gltfio::AssetLoader* m_nativeLoader{nullptr};
    std::unique_ptr<filament::gltfio::ResourceLoader> m_nativeResources;
    filament::gltfio::TextureProvider* m_nativeStbTextures{nullptr};
    filament::gltfio::TextureProvider* m_nativeKtx2Textures{nullptr};
    filament::gltfio::FilamentAsset* m_nativeAsset{nullptr};
    filament::math::float3 m_nativeSourceCenter{};
    DBInstanceID m_nativePartId{};
    bool m_nativeInScene{false};
    std::uint64_t m_nextProjectionToken{0};
    bool m_shutdown{false};
};

} // namespace

std::unique_ptr<IFilamentDBSync> createFilamentModelInstanceDBSync(
    DBInstanceID id, FilamentSceneAdapter& adapter) {
    return std::make_unique<FilamentModelInstanceDBSync>(id, adapter);
}

} // namespace GPlatform::Rendering
