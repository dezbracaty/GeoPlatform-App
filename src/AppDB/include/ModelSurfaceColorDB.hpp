#pragma once

#include <AutoRegisterDB.hpp>
#include <Geometry.hpp>
#include <SystemTypes.hpp>
#include <QtGlobal>
#include <cstdint>
#include <string>
#include <vector>

/** Persistent, renderer-independent facet labels owned by one model Part. */
class ModelSurfaceColorDB : public AutoRegisterDB {
public:
    ModelSurfaceColorDB() = default;
    ~ModelSurfaceColorDB() override = default;

    FIELD_VALUE(ModelSurfaceColorDB, std::string, SurfaceColorData)
    FIELD_VALUE_SIMPLE(ModelSurfaceColorDB, int, SourceTriangleCount)
    FIELD_VALUE(ModelSurfaceColorDB, std::string, TopologyFingerprint)
    FIELD_VALUE_SIMPLE(ModelSurfaceColorDB, int, Revision)
    FIELD_VALUE_SIMPLE(ModelSurfaceColorDB, int, DataVersion)

    TypeID getTypeID() const override {
        return TypeID::MODEL_SURFACE_COLOR_DB;
    }

    // This is semantic child data, not an independently rendered object.
    // ModelInstanceDBSync consumes it when selecting the model's surface
    // presentation.
    bool needsVTKSync() const override { return false; }

    /** The single label-to-color rule shared by every presentation backend. */
    static Color colorForLabel(
        std::uint32_t label,
        const std::vector<Color>& palette,
        const Color& baseColor);

    std::shared_ptr<AutoRegisterDB> clone() const override {
        auto copy = trans::TransDB::create<ModelSurfaceColorDB>();
        copy->setSurfaceColorData(getSurfaceColorData());
        copy->setSourceTriangleCount(getSourceTriangleCount());
        copy->setTopologyFingerprint(getTopologyFingerprint());
        copy->setRevision(getRevision());
        copy->setDataVersion(getDataVersion());
        return copy;
    }

    void initializeProperties() override {
        setSurfaceColorData(std::string());
        setSourceTriangleCount(0);
        setTopologyFingerprint(std::string());
        setRevision(0);
        setDataVersion(2);
    }
};
