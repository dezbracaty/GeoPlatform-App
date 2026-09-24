#include "SnapPreviewDB.hpp"
#include <algorithm>
#include <atomic>

void SnapPreviewDB::initializeSubActorProperties() {
    setPickable(false);
    setDragable(false);
    setVisible(false);
}

void SnapPreviewDB::onFullyInitialized() {
    ActorDB::onFullyInitialized();
    auto material = getMaterial();
    if (!material) return;
    material->setDiffuseColor({1.0f, 0.15f, 0.12f});
    material->setColor({1.0f, 0.15f, 0.12f});
    material->setLighting(false);
    material->setLineWidth(6);
}

void SnapPreviewDB::setPreview(DBInstanceID ownerViewId, std::vector<Line> lines) {
    auto data = std::make_shared<Preview>();
    data->ownerViewId = ownerViewId;
    data->lines = std::move(lines);
    for (const auto& line : data->lines) {
        for (const auto& point : {line.start, line.end}) {
            if (!data->bounds.valid) {
                data->bounds.min = data->bounds.max = point;
                data->bounds.valid = true;
            } else {
                auto& box = data->bounds;
                box.min = {std::min(box.min.x, point.x), std::min(box.min.y, point.y), std::min(box.min.z, point.z)};
                box.max = {std::max(box.max.x, point.x), std::max(box.max.y, point.y), std::max(box.max.z, point.z)};
            }
        }
    }
    std::atomic_store_explicit(&m_preview, PreviewPtr(std::move(data)), std::memory_order_release);
    notifyGeometryChange();
}

SnapPreviewDB::PreviewPtr SnapPreviewDB::preview() const {
    return std::atomic_load_explicit(&m_preview, std::memory_order_acquire);
}
DBInstanceID SnapPreviewDB::ownerViewId() const {
    const auto data = preview();
    return data ? data->ownerViewId : DBInstanceID{};
}
ActorDB::BoundingBox SnapPreviewDB::localBounds() const {
    const auto data = preview();
    return data ? data->bounds : BoundingBox{};
}
std::shared_ptr<AutoRegisterDB> SnapPreviewDB::clone() const {
    auto lock = getSharedLock();
    auto copy = trans::TransDB::create<SnapPreviewDB>();
    std::atomic_store_explicit(&copy->m_preview, preview(), std::memory_order_release);
    copy->copyTransformFrom(*this);
    copy->setVisible(isVisible());
    copy->setOpacity(getOpacity());
    return copy;
}
