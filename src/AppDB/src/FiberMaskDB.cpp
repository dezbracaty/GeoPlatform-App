#include "FiberMaskDB.hpp"
#include <DocumentManager.hpp>
#include <ModelInstanceDB.hpp>
#include <algorithm>
#include <cmath>

namespace {
// Minimum positive extent and round-shape dimension equality tolerance.
constexpr double kDimensionTolerance = 1.e-6;
}

bool FiberMaskDB::validRegion(const Region& r) {
    const auto m = r.pose.getMatrix().cast<double>().eval();
    const Eigen::Vector3d size(r.dimensions.x, r.dimensions.y, r.dimensions.z);
    if (r.shape != Shape::Box &&
        std::abs(size.y() - size.x()) > kDimensionTolerance * std::max(size.x(), size.y()))
        return false;
    if (r.shape == Shape::Sphere &&
        std::abs(size.z() - size.x()) > kDimensionTolerance * std::max(size.x(), size.z()))
        return false;
    return int(r.shape) >= 0 && int(r.shape) <= 2 && size.allFinite() &&
           (size.array() > 2 * kDimensionTolerance).all() &&
           (size.array() <= 1000000.).all() && m.allFinite() &&
           m.row(3).isApprox(Eigen::RowVector4d(0, 0, 0, 1), 1.e-7) &&
           (m.block<3, 3>(0, 0).transpose() * m.block<3, 3>(0, 0))
               .isApprox(Eigen::Matrix3d::Identity(), 1.e-5) &&
           std::abs(m.block<3, 3>(0, 0).determinant() - 1.) <= 1.e-5;
}

FiberMaskDB::Region FiberMaskDB::region() const {
    return {static_cast<Shape>(getShape()), getTransform(), getDimensions()};
}
bool FiberMaskDB::precedes(const FiberMaskDB& first, const FiberMaskDB& second) {
    if (first.getOrder() != second.getOrder()) return first.getOrder() < second.getOrder();
    return first.getDBInstanceID().getValue() < second.getDBInstanceID().getValue();
}
Transform::Matrix4 FiberMaskDB::worldTransform() const {
    auto* document = DocumentManager::instance();
    const auto owner = document
                           ? document->getDB<ModelInstanceDB>(document->getOwner(getDBInstanceID()))
                           : nullptr;
    return owner ? (owner->getTransformMatrix() * getTransformMatrix()).eval()
                 : getTransformMatrix();
}
ActorDB::BoundingBox FiberMaskDB::localBounds() const {
    const auto d = getDimensions();
    return {{-d.x * .5f, -d.y * .5f, -d.z * .5f}, {d.x * .5f, d.y * .5f, d.z * .5f}, true};
}
ActorDB::BoundingBox FiberMaskDB::worldBounds() const {
    return worldBoundsAt(worldTransform());
}
std::shared_ptr<AutoRegisterDB> FiberMaskDB::clone() const {
    auto copy = trans::TransDB::create<FiberMaskDB>();
    copy->setShape(getShape());
    copy->setDimensions(getDimensions());
    copy->setParameterOverridesJson(getParameterOverridesJson());
    copy->setOrder(getOrder());
    copy->setEnabled(getEnabled());
    copy->setDisplayName(getDisplayName());
    copy->copyTransformFrom(*this);
    copy->setVisible(getVisible());
    return copy;
}
void FiberMaskDB::initializeSubActorProperties() {
    setShape(int(Shape::Box));
    setDimensions({1, 1, 1});
    setParameterOverridesJson("{}");
    setOrder(0);
    setEnabled(true);
    setDisplayName("Fiber mask");
    setPickable(false);
    setDragable(false);
}
