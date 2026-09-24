#pragma once

#include "GlbAssetData.hpp"

#include <string>

/** DB-layer importer for binary glTF source assets. */
class GlbDBImporter final {
public:
    GlbImportResult importFile(
        const std::string& filePath,
        float scale = 1.0f) const;
};
