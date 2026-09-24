#include "GlbDB.hpp"

#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <atomic>
#include <algorithm>
#include <limits>
#include <utility>

GlbDB::GlbDB() = default;
GlbDB::GlbDB(CreationOptions options)
    : m_creationOptions(options) {}
GlbDB::~GlbDB() = default;

void GlbDB::initializeSubActorProperties() {
    setVisible(m_creationOptions.visible);
    setPickable(m_creationOptions.pickable);
    setSourceFile({});
    setFileFormat({});
    setDataSource({});
    setBoundingBoxMin({});
    setBoundingBoxMax({});
    setVertexCount(0);
    setFaceCount(0);
    setAssetRevision(0);
    setAssetContentHash({});
    setAssetByteSize(0);
    setNodeCount(0);
    setMeshCount(0);
    setMaterialCount(0);
    setTextureCount(0);
    setAnimationCount(0);
}

bool GlbDB::replaceImportedAsset(GlbImportResult result) {
    if (!result.success || !result.asset || !result.asset->isValid() ||
        result.printableTriangles.empty()) {
        return false;
    }

    m_printableTriangles = std::move(result.printableTriangles);
    setBoundingBoxMin(result.boundingBoxMin);
    setBoundingBoxMax(result.boundingBoxMax);
    setVertexCount(result.vertexCount);
    setFaceCount(result.faceCount);
    const auto& metadata = result.asset->metadata();
    setAssetContentHash(metadata.contentHash);
    setAssetByteSize(static_cast<std::uint64_t>(metadata.byteSize));
    setNodeCount(static_cast<int>(std::min<std::size_t>(
        metadata.nodeCount, static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    setMeshCount(static_cast<int>(std::min<std::size_t>(
        metadata.meshCount, static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    setMaterialCount(static_cast<int>(std::min<std::size_t>(
        metadata.materialCount, static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    setTextureCount(static_cast<int>(std::min<std::size_t>(
        metadata.textureCount, static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    setAnimationCount(static_cast<int>(std::min<std::size_t>(
        metadata.animationCount, static_cast<std::size_t>(std::numeric_limits<int>::max()))));

    publishAsset(std::move(result.asset));
    return true;
}

void GlbDB::publishAsset(std::shared_ptr<const GlbAssetData> asset) {
    const std::uint64_t revision = getAssetRevision() + 1;
    auto snapshot = std::make_shared<const GlbAssetSnapshot>(
        GlbAssetSnapshot{std::move(asset), revision});
    std::atomic_store_explicit(
        &m_assetSnapshot, std::move(snapshot), std::memory_order_release);

    // Publish the transaction-visible revision last. Render adapters receive
    // this notification only after the immutable payload is complete.
    setAssetRevision(revision);
}

std::shared_ptr<const GlbAssetSnapshot>
GlbDB::acquireAssetSnapshot() const noexcept {
    return std::atomic_load_explicit(
        &m_assetSnapshot, std::memory_order_acquire);
}

std::shared_ptr<AutoRegisterDB> GlbDB::clone() const {
    auto cloned = trans::TransDB::create<GlbDB>(
        CreationOptions{getVisible(), isPickable()});
    cloned->setSourceFile(getSourceFile());
    cloned->setFileFormat(getFileFormat());
    cloned->setDataSource(getDataSource());
    cloned->setBoundingBoxMin(getBoundingBoxMin());
    cloned->setBoundingBoxMax(getBoundingBoxMax());
    cloned->setVertexCount(getVertexCount());
    cloned->setFaceCount(getFaceCount());
    cloned->m_printableTriangles = m_printableTriangles;
    cloned->copyTransformFrom(*this);

    const auto snapshot = acquireAssetSnapshot();
    if (snapshot && snapshot->asset) {
        cloned->setAssetContentHash(getAssetContentHash());
        cloned->setAssetByteSize(getAssetByteSize());
        cloned->setNodeCount(getNodeCount());
        cloned->setMeshCount(getMeshCount());
        cloned->setMaterialCount(getMaterialCount());
        cloned->setTextureCount(getTextureCount());
        cloned->setAnimationCount(getAnimationCount());
        cloned->publishAsset(snapshot->asset);
    }
    return cloned;
}

ActorDB::BoundingBox GlbDB::localBounds() const {
    return {getBoundingBoxMin(), getBoundingBoxMax(),
            !m_printableTriangles.empty()};
}
