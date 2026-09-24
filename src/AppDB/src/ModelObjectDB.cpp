#include "../include/ModelObjectDB.hpp"

#include "../include/ModelInstanceDB.hpp"
#include "../include/ModelPartDB.hpp"
#include "../include/ModelGeometryDB.hpp"
#include <DocumentManager.hpp>

#include <algorithm>
#include <atomic>
#include <limits>

#include <vtkPoints.h>
#include <vtkPolyData.h>

namespace {
constexpr const char* kPartsRelation = "model.object.parts";

void expand(ActorDB::BoundingBox& bounds, const Vector3& point) {
    if (!bounds.valid) {
        bounds.min = point;
        bounds.max = point;
        bounds.valid = true;
        return;
    }
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
}
}

const trans::Prop& ModelObjectDB::PROP_SourceFile() {
    static const trans::Prop prop(typeid(ModelObjectDB), "SourceFile");
    return prop;
}

std::string ModelObjectDB::getSourceFile() const {
    return trans::TransDB::getProperty<std::string>(PROP_SourceFile());
}

void ModelObjectDB::setSourceFile(const std::string& value) {
    trans::TransDB::setProperty(PROP_SourceFile(), value);
    onSourceFileChanged(value);
}

const trans::Prop& ModelObjectDB::PROP_FileFormat() {
    static const trans::Prop prop(typeid(ModelObjectDB), "FileFormat");
    return prop;
}

std::string ModelObjectDB::getFileFormat() const {
    return trans::TransDB::getProperty<std::string>(PROP_FileFormat());
}

void ModelObjectDB::setFileFormat(const std::string& value) {
    trans::TransDB::setProperty(PROP_FileFormat(), value);
    onFileFormatChanged(value);
}

const trans::Prop& ModelObjectDB::PROP_PrintableDefault() {
    static const trans::Prop prop(typeid(ModelObjectDB), "PrintableDefault");
    return prop;
}

bool ModelObjectDB::getPrintableDefault() const {
    return trans::TransDB::getProperty<bool>(PROP_PrintableDefault());
}

void ModelObjectDB::setPrintableDefault(bool value) {
    trans::TransDB::setProperty(PROP_PrintableDefault(), value);
    onPrintableDefaultChanged(value);
}

const trans::Prop& ModelObjectDB::PROP_UseSourceAppearance() {
    static const trans::Prop prop(typeid(ModelObjectDB), "UseSourceAppearance");
    return prop;
}

bool ModelObjectDB::getUseSourceAppearance() const {
    return trans::TransDB::getProperty<bool>(PROP_UseSourceAppearance());
}

void ModelObjectDB::setUseSourceAppearance(bool value) {
    trans::TransDB::setProperty(PROP_UseSourceAppearance(), value);
    onUseSourceAppearanceChanged(value);
}

const trans::Prop& ModelObjectDB::PROP_SourceAssetRevision() {
    static const trans::Prop prop(typeid(ModelObjectDB), "SourceAssetRevision");
    return prop;
}

std::uint64_t ModelObjectDB::getSourceAssetRevision() const {
    return trans::TransDB::getProperty<std::uint64_t>(
        PROP_SourceAssetRevision());
}

void ModelObjectDB::setSourceAssetRevision(std::uint64_t value) {
    trans::TransDB::setProperty(PROP_SourceAssetRevision(), value);
    onSourceAssetRevisionChanged(value);
}

bool ModelObjectDB::publishGlbSourceAsset(
    std::shared_ptr<const GlbAssetData> asset) {
    if (!asset || !asset->isValid()) return false;
    const std::uint64_t revision = getSourceAssetRevision() + 1;
    auto snapshot = std::make_shared<const GlbAssetSnapshot>(
        GlbAssetSnapshot{std::move(asset), revision});
    std::atomic_store_explicit(
        &m_glbSourceAssetSnapshot, std::move(snapshot),
        std::memory_order_release);
    setUseSourceAppearance(true);
    // Publish the transaction-visible revision only after the payload.
    setSourceAssetRevision(revision);
    return true;
}

std::shared_ptr<const GlbAssetSnapshot>
ModelObjectDB::acquireGlbSourceAssetSnapshot() const noexcept {
    return std::atomic_load_explicit(
        &m_glbSourceAssetSnapshot, std::memory_order_acquire);
}

void ModelObjectDB::initializeProperties() {
    setSourceFile({});
    setFileFormat({});
    setPrintableDefault(true);
    setUseSourceAppearance(false);
    setSourceAssetRevision(0);
}

std::vector<std::shared_ptr<ModelPartDB>> ModelObjectDB::parts() const {
    std::vector<std::shared_ptr<ModelPartDB>> result;
    auto* document = DocumentManager::instance();
    if (!document || !getDBInstanceID().isValid()) return result;
    for (const auto& id : document->getOwnedChildren(
             getDBInstanceID(), kPartsRelation)) {
        if (auto part = document->getDB<ModelPartDB>(id)) {
            result.push_back(std::move(part));
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs->getOrderIndex() != rhs->getOrderIndex()) {
            return lhs->getOrderIndex() < rhs->getOrderIndex();
        }
        return lhs->getDBInstanceID().getValue() <
            rhs->getDBInstanceID().getValue();
    });
    return result;
}

std::vector<std::shared_ptr<ModelInstanceDB>> ModelObjectDB::instances() const {
    std::vector<std::shared_ptr<ModelInstanceDB>> result;
    auto* document = DocumentManager::instance();
    if (!document || !getDBInstanceID().isValid()) return result;
    for (const auto& id : document->getOwnedChildren(
             getDBInstanceID(), "model.object.instances")) {
        if (auto instance = document->getDB<ModelInstanceDB>(id)) {
            result.push_back(std::move(instance));
        }
    }
    return result;
}

ActorDB::BoundingBox ModelObjectDB::localBounds() const {
    ActorDB::BoundingBox result;
    for (const auto& part : parts()) {
        if (!part || !part->hasValidMesh()) continue;
        const auto geometry = part->geometry();
        if (!geometry) continue;
        const Transform::Matrix4 matrix =
            part->getLocalTransform().getMatrix();
        const ActorDB::BoundingBox partBounds =
            geometry->boundsAfterTransform(matrix);
        if (!partBounds.valid) continue;
        expand(result, partBounds.min);
        expand(result, partBounds.max);
    }
    return result;
}

bool ModelObjectDB::isValid() const {
    const auto ownedParts = parts();
    return AutoRegisterDB::isValid() && !ownedParts.empty() &&
        std::all_of(ownedParts.begin(), ownedParts.end(),
                    [](const auto& part) { return part && part->isValid(); });
}

std::shared_ptr<AutoRegisterDB> ModelObjectDB::clone() const {
    auto copy = trans::TransDB::create<ModelObjectDB>();
    copy->setDisplayName(getDisplayName());
    copy->setSourceFile(getSourceFile());
    copy->setFileFormat(getFileFormat());
    copy->setPrintableDefault(getPrintableDefault());
    copy->setUseSourceAppearance(getUseSourceAppearance());
    const auto sourceAsset = acquireGlbSourceAssetSnapshot();
    if (sourceAsset && sourceAsset->asset) {
        copy->publishGlbSourceAsset(sourceAsset->asset);
        copy->setUseSourceAppearance(getUseSourceAppearance());
    }
    return copy;
}
