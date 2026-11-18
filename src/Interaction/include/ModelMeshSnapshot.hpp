#pragma once
#include <ModelGeometryDB.hpp>
#include <libslicer/Library.hpp>
#include <functional>
#include <optional>
#include <string>

// Common document-snapshot conversion for slicing and editable model export.
namespace ModelMeshSnapshot {
std::optional<std::string> topologyFingerprint(const ModelGeometryDB::ReadHandle&, std::string* error);
bool appendGeometry(const ModelGeometryDB::ReadHandle&, libslicer::SliceVolumeInput*,
                    const std::function<bool()>& isCancelled, std::string* error);
}
