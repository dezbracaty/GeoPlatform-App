#pragma once

#include <Geometry.hpp>

#include <memory>
#include <vector>

class MaterialDB;
class ModelObjectDB;
class ModelPartDB;
struct GlbAssetSnapshot;

namespace GPlatform {
class SlicingConfigDB;
}

namespace GPlatform::Rendering {

/** Project AppDB state into the color consumed by renderer backends. */
Vector3 resolveModelDisplayColor(
    const ModelPartDB& part,
    const MaterialDB* material,
    const GPlatform::SlicingConfigDB* slicingConfig);

/**
 * Return the authored GLB presentation while the object still permits it and
 * no part has an explicit filament binding. Appearance-editing workflows turn
 * the object flag off before publishing their renderer invalidation.
 */
std::shared_ptr<const GlbAssetSnapshot> resolveModelSourceAppearance(
    const ModelObjectDB& object,
    const std::vector<std::shared_ptr<const ModelPartDB>>& parts);

} // namespace GPlatform::Rendering
