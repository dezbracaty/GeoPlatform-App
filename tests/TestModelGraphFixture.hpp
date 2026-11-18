#pragma once

#include <DocumentManager.hpp>
#include <MaterialDB.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelObjectDB.hpp>
#include <ModelPartDB.hpp>
#include <transdb.h>

#include <memory>
#include <string>
#include <vector>

#include <vtkCellArray.h>
#include <vtkCubeSource.h>
#include <vtkTriangleFilter.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

struct TestModelGraphFixture {
    std::shared_ptr<ModelObjectDB> object;
    std::shared_ptr<ModelPartDB> part;
    std::shared_ptr<ModelGeometryDB> geometry;
    std::shared_ptr<MaterialDB> material;
    std::shared_ptr<ModelInstanceDB> instance;

    explicit operator bool() const {
        return object && part && geometry && material && instance &&
            part->geometry() == geometry && part->getMaterial() == material &&
            instance->isValid();
    }
};

inline std::vector<GeomTriangle> testModelTriangle() {
    return {GeomTriangle{
        Vector3(-1.0f, -1.0f, 0.0f),
        Vector3(1.0f, -1.0f, 0.0f),
        Vector3(0.0f, 1.0f, 1.0f)}};
}

inline TestModelGraphFixture createTestModelGraph(
    const std::vector<GeomTriangle>& triangles = testModelTriangle(),
    const std::string& name = "test model") {
    TestModelGraphFixture fixture;
    auto* document = DocumentManager::instance();
    if (!document) {
        return {};
    }
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    points->SetNumberOfPoints(static_cast<vtkIdType>(triangles.size() * 3));
    vtkIdType pointId = 0;
    for (const auto& triangle : triangles) {
        const Vector3 vertices[3] = {
            triangle.vertex1, triangle.vertex2, triangle.vertex3};
        vtkIdType ids[3];
        for (int corner = 0; corner < 3; ++corner) {
            points->SetPoint(pointId, vertices[corner].x,
                             vertices[corner].y, vertices[corner].z);
            ids[corner] = pointId++;
        }
        cells->InsertNextCell(3, ids);
    }
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetPolys(cells);

    fixture.object = trans::TransDB::create<ModelObjectDB>();
    fixture.part = trans::TransDB::create<ModelPartDB>();
    fixture.geometry = trans::TransDB::create<ModelGeometryDB>();
    fixture.material = trans::TransDB::create<MaterialDB>();
    fixture.instance = trans::TransDB::create<ModelInstanceDB>();
    if (!fixture.object || !fixture.part || !fixture.geometry ||
        !fixture.material ||
        !fixture.instance) return {};
    fixture.object->setDisplayName(name);
    fixture.object->setSourceFile("test://model-graph");
    fixture.object->setFileFormat("test");
    fixture.part->setDisplayName(name + " part");
    fixture.geometry->replacePolyData(polyData);
    const bool partAttached = document->attachOwnedChild(
        fixture.object->getDBInstanceID(),
        fixture.part->getDBInstanceID(), ModelPartDB::kObjectRelation);
    const bool geometryRelationAttached = document->attachOwnedChild(
        fixture.part->getDBInstanceID(),
        fixture.geometry->getDBInstanceID(), ModelPartDB::kGeometryRelation);
    const bool materialRelationAttached = document->attachOwnedChild(
        fixture.part->getDBInstanceID(),
        fixture.material->getDBInstanceID(), ModelPartDB::kMaterialRelation);
    const bool geometryAttached = document->getOwner(
        fixture.geometry->getDBInstanceID()) == fixture.part->getDBInstanceID();
    const bool materialBound = document->getOwner(
        fixture.material->getDBInstanceID()) == fixture.part->getDBInstanceID();
    if (!partAttached || !geometryRelationAttached ||
        !materialRelationAttached || !geometryAttached || !materialBound) {
        return {};
    }
    fixture.instance->setDisplayName(name);
    document->attachOwnedChild(
        fixture.object->getDBInstanceID(),
        fixture.instance->getDBInstanceID(), ModelInstanceDB::kObjectRelation);
    if (document->getOwner(fixture.part->getDBInstanceID()) !=
            fixture.object->getDBInstanceID() ||
        document->getOwner(fixture.instance->getDBInstanceID()) !=
            fixture.object->getDBInstanceID() ||
        fixture.part->geometry() != fixture.geometry ||
        fixture.part->getMaterial() != fixture.material) {
        return {};
    }
    return fixture;
}

// Primitive geometry now uses the same model graph as imported geometry.
inline TestModelGraphFixture createTestBoxModel(
    Vector3 center = {0, 0, 0}, Vector3 size = {2, 2, 2}) {
    auto fixture = createTestModelGraph();
    if (!fixture) return {};
    auto source = vtkSmartPointer<vtkCubeSource>::New();
    source->SetCenter(center.x, center.y, center.z);
    source->SetXLength(size.x);
    source->SetYLength(size.y);
    source->SetZLength(size.z);
    auto triangles = vtkSmartPointer<vtkTriangleFilter>::New();
    triangles->SetInputConnection(source->GetOutputPort());
    triangles->Update();
    fixture.geometry->replacePolyData(triangles->GetOutput());
    return fixture;
}
