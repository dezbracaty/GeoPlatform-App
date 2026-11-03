#pragma once

#include <ModelGraphUtil.hpp>
#include <ModelLoaderUtil.hpp>
#include <QVariantMap>

class GlbAssetData;
class PrintBedDB;

// The shared publication path for imported and generated printable meshes.
namespace ModelCreationUtil {
std::shared_ptr<PrintBedDB> resolvePrintBed(const QVariant& requestedId = {});
DBInstanceID activeSlicingConfigId();
ModelGraphUtil::CreateResult createModel(
    const ModelLoaderUtil::LoadResult& mesh,
    ModelGraphUtil::ObjectCreateInfo object,
    const QVariantMap& params,
    std::shared_ptr<const GlbAssetData> sourceAsset = {});
}
