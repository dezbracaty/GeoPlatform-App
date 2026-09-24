#pragma once

#include "ActorDB.hpp"
#include "GlbAssetData.hpp"
#include <AutoRegisterDB.hpp>

#include <cstdint>
#include <memory>
#include <vector>

class ModelInstanceDB;
class ModelPartDB;

class ModelObjectDB final : public AutoRegisterDB {
public:
    ModelObjectDB() = default;

    static const trans::Prop& PROP_SourceFile();
    std::string getSourceFile() const;
    void setSourceFile(const std::string& value);
    std::function<void(const std::string&)> onSourceFileChanged =
        [](const std::string&) {};

    static const trans::Prop& PROP_FileFormat();
    std::string getFileFormat() const;
    void setFileFormat(const std::string& value);
    std::function<void(const std::string&)> onFileFormatChanged =
        [](const std::string&) {};

    static const trans::Prop& PROP_PrintableDefault();
    bool getPrintableDefault() const;
    void setPrintableDefault(bool value);
    std::function<void(bool)> onPrintableDefaultChanged = [](bool) {};

    static const trans::Prop& PROP_UseSourceAppearance();
    bool getUseSourceAppearance() const;
    void setUseSourceAppearance(bool value);
    std::function<void(bool)> onUseSourceAppearanceChanged = [](bool) {};

    static const trans::Prop& PROP_SourceAssetRevision();
    std::uint64_t getSourceAssetRevision() const;
    void setSourceAssetRevision(std::uint64_t value);
    std::function<void(std::uint64_t)> onSourceAssetRevisionChanged =
        [](std::uint64_t) {};

    /** Publish an immutable GLB source appearance owned by this model. */
    bool publishGlbSourceAsset(std::shared_ptr<const GlbAssetData> asset);
    std::shared_ptr<const GlbAssetSnapshot>
    acquireGlbSourceAssetSnapshot() const noexcept;

    TypeID getTypeID() const override { return TypeID::MODEL_OBJECT_DB; }
    bool needsVTKSync() const override { return false; }
    bool isValid() const override;
    std::shared_ptr<AutoRegisterDB> clone() const override;

    std::vector<std::shared_ptr<ModelPartDB>> parts() const;
    std::vector<std::shared_ptr<ModelInstanceDB>> instances() const;
    ActorDB::BoundingBox localBounds() const;

protected:
    void initializeProperties() override;

private:
    std::shared_ptr<const GlbAssetSnapshot> m_glbSourceAssetSnapshot;
};
