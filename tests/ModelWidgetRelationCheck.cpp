#include <ChainUpdateSystem.hpp>

#include <DBRelationRegistry.hpp>
#include "DBRelationsRegistration.hpp"
#include "ModelWidgetRelations.hpp"
#include <DocumentManager.hpp>
#include "TestModelGraphFixture.hpp"
#include <SelectionBoxWidgetDB.hpp>
#include <TransactionManager.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {

int fail(const char* message) {
    std::cerr << message << std::endl;
    return 1;
}

bool near(float lhs, float rhs) {
    return std::abs(lhs - rhs) < 0.001f;
}

bool sameBounds(const ActorDB::BoundingBox& lhs,
                const ActorDB::BoundingBox& rhs) {
    return lhs.valid == rhs.valid &&
        (!lhs.valid ||
         (near(lhs.min.x, rhs.min.x) && near(lhs.min.y, rhs.min.y) &&
          near(lhs.min.z, rhs.min.z) && near(lhs.max.x, rhs.max.x) &&
          near(lhs.max.y, rhs.max.y) && near(lhs.max.z, rhs.max.z)));
}

ActorDB::BoundingBox transformedVertexBounds(
    const std::vector<GeomTriangle>& triangles,
    const Transform::Matrix4& matrix) {
    ActorDB::BoundingBox result;
    result.min = Vector3(std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max());
    result.max = Vector3(std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest());
    const auto expand = [&result, &matrix](const Vector3& point) {
        const Eigen::Vector4f world =
            matrix * Eigen::Vector4f(point.x, point.y, point.z, 1.0f);
        result.min.x = std::min(result.min.x, world.x());
        result.min.y = std::min(result.min.y, world.y());
        result.min.z = std::min(result.min.z, world.z());
        result.max.x = std::max(result.max.x, world.x());
        result.max.y = std::max(result.max.y, world.y());
        result.max.z = std::max(result.max.z, world.z());
        result.valid = true;
    };
    for (const auto& triangle : triangles) {
        expand(triangle.vertex1);
        expand(triangle.vertex2);
        expand(triangle.vertex3);
    }
    return result;
}

} // namespace

int main() {
    auto* document = DocumentManager::instance();
    auto& chain = ChainUpdateSystem::instance();
    auto& registry = DBRelationRegistry::instance();
    registry.clear();
    chain.clear();
    TransactionManager::instance().clear();
    GPlatform::DBRelations::registerModule();

    const auto cubeGraph = createTestBoxModel({2.0f, 3.0f, 4.0f}, {2.0f, 4.0f, 6.0f});
    const auto cube = cubeGraph.instance;
    cube->setPosition(Vector3(10.0f, 20.0f, 30.0f));
    const ActorDB::BoundingBox expectedCubeLocal{
        Vector3(1.0f, 1.0f, 1.0f), Vector3(3.0f, 5.0f, 7.0f), true};
    const ActorDB::BoundingBox expectedCubeWorld{
        Vector3(11.0f, 21.0f, 31.0f), Vector3(13.0f, 25.0f, 37.0f), true};
    if (!sameBounds(cube->localBounds(), expectedCubeLocal) ||
        !sameBounds(cube->worldBounds(), expectedCubeWorld)) {
        return fail("local bounds and Transform responsibilities are mixed");
    }

    const std::vector<GeomTriangle> triangles{
        GeomTriangle{Vector3(0.0f, 0.0f, 0.0f),
                     Vector3(4.0f, 0.0f, 0.0f),
                     Vector3(0.0f, 2.0f, 3.0f)},
        GeomTriangle{Vector3(4.0f, 0.0f, 0.0f),
                     Vector3(4.0f, 2.0f, 3.0f),
                     Vector3(0.0f, 2.0f, 3.0f)}};
    const auto modelGraph = createTestModelGraph(triangles);
    const auto mesh = modelGraph.instance;
    const auto box = trans::TransDB::create<SelectionBoxWidgetDB>(
        mesh->getDBInstanceID());
    if (!document || !mesh || !box || !box->getHasValidBounds()) {
        return fail("failed to create linked model selection box");
    }

    const auto relationEdges = document->getDependencyRelationEdges();
    const bool attached = std::any_of(
        relationEdges.begin(), relationEdges.end(), [&](const auto& edge) {
            return edge.sourceId == mesh->getDBInstanceID() &&
                   edge.targetId == box->getDBInstanceID() &&
                   edge.relationName == ModelWidgetRelations::SelectionBoxRelation;
        });
    if (!attached) {
        return fail("selection box dependency relation was not attached");
    }

    Transform transformed;
    transformed.rotateWXYZ(45.0f, Vector3(1.0f, 0.0f, 0.0f));
    transformed.setPosition(Vector3(10.0f, 20.0f, 5.0f));
    {
        TransactionGuard transaction("transform linked model");
        mesh->setTransform(transformed);
        transaction.commit();
    }
    const auto rotatedExpected = transformedVertexBounds(
        triangles, mesh->getTransformMatrix());
    const auto rotatedActual = mesh->worldBounds();
    const ActorDB::BoundingBox rotatedBox{
        box->getModelBoundsMin(), box->getModelBoundsMax(),
        box->getHasValidBounds()};
    if (!sameBounds(rotatedActual, rotatedExpected) ||
        !sameBounds(rotatedBox, rotatedExpected)) {
        return fail("rotation did not produce exact model/selection bounds");
    }

    if (!TransactionManager::instance().undo()) {
        return fail("failed to undo model rotation");
    }
    const auto undoExpected = transformedVertexBounds(
        triangles, mesh->getTransformMatrix());
    const ActorDB::BoundingBox undoBox{
        box->getModelBoundsMin(), box->getModelBoundsMax(),
        box->getHasValidBounds()};
    if (!sameBounds(mesh->worldBounds(), undoExpected) ||
        !sameBounds(undoBox, undoExpected)) {
        return fail("undo did not restore model/selection bounds");
    }
    if (!TransactionManager::instance().redo()) {
        return fail("failed to redo model rotation");
    }
    const auto redoExpected = transformedVertexBounds(
        triangles, mesh->getTransformMatrix());
    const ActorDB::BoundingBox redoBox{
        box->getModelBoundsMin(), box->getModelBoundsMax(),
        box->getHasValidBounds()};
    if (!sameBounds(mesh->worldBounds(), redoExpected) ||
        !sameBounds(redoBox, redoExpected)) {
        return fail("redo did not restore model/selection bounds");
    }

    const Vector3 dragDelta(7.0f, -3.0f, 2.5f);
    mesh->setPosition(mesh->getPosition() + dragDelta);
    const auto draggedExpected = transformedVertexBounds(
        triangles, mesh->getTransformMatrix());
    const auto draggedActual = mesh->worldBounds();
    const ActorDB::BoundingBox draggedBox{
        box->getModelBoundsMin(), box->getModelBoundsMax(),
        box->getHasValidBounds()};
    if (!sameBounds(draggedActual, draggedExpected) ||
        !sameBounds(draggedBox, draggedExpected)) {
        return fail("drag did not move model/selection bounds exactly");
    }

    return 0;
}
