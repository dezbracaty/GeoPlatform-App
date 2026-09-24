#pragma once

#include "ActorDB.hpp"

#include <AutoRegisterDB.hpp>
#include <Geometry.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <vtkSmartPointer.h>

class vtkPolyData;
class ModelInstanceDB;
class ModelObjectDB;

/** The single authoritative geometry resource owned by one ModelPartDB. */
class ModelGeometryDB final : public AutoRegisterDB {
private:
    struct Version;

public:
    class ReadHandle final {
    public:
        ReadHandle() = default;

        explicit operator bool() const noexcept {
            return static_cast<bool>(m_version);
        }
        const vtkPolyData& polyData() const noexcept;
        std::uint64_t revision() const noexcept { return m_revision; }

    private:
        friend class ModelGeometryDB;
        ReadHandle(std::shared_ptr<const Version> version,
                   std::uint64_t revision) noexcept
            : m_version(std::move(version)), m_revision(revision) {}

        std::shared_ptr<const Version> m_version;
        std::uint64_t m_revision{0};
    };

    struct RayHit {
        std::uint64_t primitiveIndex{0};
        Vector3 localPosition;
        float localDistance{0.0f};
    };

    ModelGeometryDB();
    ~ModelGeometryDB() override;

    TypeID getTypeID() const override { return TypeID::MODEL_GEOMETRY_DB; }
    bool needsVTKSync() const override { return false; }
    bool isValid() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

    static const trans::Prop& PROP_GeometryVersion();

    ReadHandle read() const;
    bool replacePolyData(vtkPolyData* polyData,
                         const std::string& source = {},
                         std::string* error = nullptr);
    std::size_t vertexCount() const;
    std::size_t triangleCount() const;
    std::uint64_t revision() const;
    ActorDB::BoundingBox localBounds() const;
    std::string source() const;

    std::optional<RayHit> intersectLocalRay(
        const Vector3& origin,
        const Vector3& direction,
        float maxDistance = std::numeric_limits<float>::infinity()) const;

protected:
    void initializeProperties() override;
    std::any getPropertyImpl(const trans::Prop& prop) const override;
    void setPropertyImpl(
        const trans::Prop& prop, const std::any& value) override;

private:
    friend class ModelInstanceDB;
    friend class ModelObjectDB;
    struct RuntimeData;
    ActorDB::BoundingBox boundsAfterTransform(
        const Transform::Matrix4& matrix) const;
    void applyVersion(std::shared_ptr<const Version> version);
    std::unique_ptr<RuntimeData> m_runtime;
};
