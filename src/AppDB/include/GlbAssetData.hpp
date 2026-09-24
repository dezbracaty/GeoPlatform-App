#pragma once

#include <Geometry.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/** Immutable, backend-independent source payload for one binary glTF asset. */
struct GlbAssetMetadata {
    std::string gltfVersion;
    std::string generator;
    std::string contentHash;
    std::size_t byteSize{0};
    std::size_t nodeCount{0};
    std::size_t meshCount{0};
    std::size_t primitiveCount{0};
    std::size_t materialCount{0};
    std::size_t textureCount{0};
    std::size_t animationCount{0};
};

class GlbAssetData final {
public:
    using Bytes = std::vector<std::uint8_t>;

    GlbAssetData(
        std::shared_ptr<const Bytes> bytes,
        GlbAssetMetadata metadata)
        : m_bytes(std::move(bytes)), m_metadata(std::move(metadata)) {}

    const Bytes& bytes() const noexcept { return *m_bytes; }
    const std::shared_ptr<const Bytes>& sharedBytes() const noexcept {
        return m_bytes;
    }
    const GlbAssetMetadata& metadata() const noexcept { return m_metadata; }
    bool isValid() const noexcept { return m_bytes && !m_bytes->empty(); }

private:
    std::shared_ptr<const Bytes> m_bytes;
    GlbAssetMetadata m_metadata;
};

struct GlbImportResult {
    bool success{false};
    std::string errorMessage;
    std::shared_ptr<const GlbAssetData> asset;
    std::vector<GeomTriangle> printableTriangles;
    Vector3 boundingBoxMin;
    Vector3 boundingBoxMax;
    std::size_t vertexCount{0};
    std::size_t faceCount{0};
};

/** One atomically published GLB payload and its DB-local revision. */
struct GlbAssetSnapshot {
    std::shared_ptr<const GlbAssetData> asset;
    std::uint64_t revision{0};

    explicit operator bool() const noexcept {
        return asset && asset->isValid();
    }
};
