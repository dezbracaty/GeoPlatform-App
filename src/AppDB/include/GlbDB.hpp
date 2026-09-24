#pragma once

#include "ActorDB.hpp"
#include "GlbAssetData.hpp"
#include <Geometry.hpp>

#include <cstdint>
#include <memory>
#include <vector>

/** A transient GLB asset presentation used by preview services. */
class GlbDB final : public ActorDB {
public:
    struct CreationOptions {
        bool visible{true};
        bool pickable{true};
    };

    GlbDB();
    explicit GlbDB(CreationOptions options);
    ~GlbDB() override;

    TypeID getTypeID() const override { return TypeID::GLB_DB; }

    bool replaceImportedAsset(GlbImportResult result);
    std::shared_ptr<const GlbAssetSnapshot> acquireAssetSnapshot() const noexcept;
    const std::vector<GeomTriangle>& printableTriangles() const noexcept {
        return m_printableTriangles;
    }
    BoundingBox localBounds() const override;

    FIELD_VALUE(GlbDB, std::string, SourceFile)
    FIELD_VALUE(GlbDB, std::string, FileFormat)
    FIELD_VALUE(GlbDB, std::string, DataSource)
    FIELD_VALUE(GlbDB, Vector3, BoundingBoxMin)
    FIELD_VALUE(GlbDB, Vector3, BoundingBoxMax)
    FIELD_VALUE_SIMPLE(GlbDB, std::size_t, VertexCount)
    FIELD_VALUE_SIMPLE(GlbDB, std::size_t, FaceCount)

    FIELD_VALUE_SIMPLE(GlbDB, std::uint64_t, AssetRevision)
    FIELD_VALUE(GlbDB, std::string, AssetContentHash)
    FIELD_VALUE_SIMPLE(GlbDB, std::uint64_t, AssetByteSize)
    FIELD_VALUE_SIMPLE(GlbDB, int, NodeCount)
    FIELD_VALUE_SIMPLE(GlbDB, int, MeshCount)
    FIELD_VALUE_SIMPLE(GlbDB, int, MaterialCount)
    FIELD_VALUE_SIMPLE(GlbDB, int, TextureCount)
    FIELD_VALUE_SIMPLE(GlbDB, int, AnimationCount)

    std::shared_ptr<AutoRegisterDB> clone() const override;

protected:
    void initializeSubActorProperties() override;

private:
    void publishAsset(std::shared_ptr<const GlbAssetData> asset);

    CreationOptions m_creationOptions;
    std::shared_ptr<const GlbAssetSnapshot> m_assetSnapshot;
    std::vector<GeomTriangle> m_printableTriangles;
};
