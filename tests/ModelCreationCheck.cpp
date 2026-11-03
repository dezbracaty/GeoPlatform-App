#include <CreateGeometryHandler.hpp>
#include <SceneQueryHandler.hpp>
#include <ModelCreationUtil.hpp>
#include <DocumentManager.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelObjectDB.hpp>
#include <PrintBedDB.hpp>
#include <PrintBedTemplateDB.hpp>
#include <SlicingConfigDB.hpp>
#include <TransactionManager.hpp>
#include <DBRelationsRegistration.hpp>
#include <ModelGraphRelations.hpp>
#include <PrintBedRelations.hpp>
#include <Foundation/Log.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <vtkCleanPolyData.h>
#include <vtkFeatureEdges.h>
#include <vtkMassProperties.h>
#include <vtkPolyData.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.05f;
}
auto create(const QString& shape, QVariantMap params = {}) {
    auto context = std::make_shared<ActionContext>();
    context->setActionCode("geometry.create." + shape);
    context->setParams(params);
    CreateGeometryHandler handler;
    handler.onEnter(context);
    return context;
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    Log::setup(argv, "ModelCreationCheck");
    try {
        ModelGraphRelations::registerRelations();
        GPlatform::DBRelations::PrintBedRelations::registerRelations();
        auto* document = DocumentManager::instance();
        auto& history = TransactionManager::instance();
        auto definition = trans::TransDB::create<PrintBedTemplateDB>();
        definition->setWidth(220);
        definition->setHeight(220);
        definition->setOrigin({0, 0, 7});
        auto bed = trans::TransDB::create<PrintBedDB>();
        bed->setTemplateDBId(definition->getDBInstanceID());
        auto config = trans::TransDB::create<GPlatform::SlicingConfigDB>();
        history.clear();

        for (const QString shape : {"cube", "sphere", "cylinder", "cone"}) {
            const auto before = document->getDBInstancesByType(TypeID::MODEL_INSTANCE_DB).size();
            const auto context = create(shape, {{"size", 20}, {"radius", 10}, {"height", 30}});
            require(!context->hasError(), context->getError().toStdString().c_str());
            const auto receipt = context->getResult().toMap();
            require(receipt.value("success").toBool(), "Creation must return a completed model result");
            const DBInstanceID id(receipt.value("modelId").toULongLong());
            auto instance = document->getDB<ModelInstanceDB>(id);
            require(instance && instance->getPrintable(), "Primitive must be a printable model instance");
            require(instance->getParentPrintBedDBId() == bed->getDBInstanceID(), "Model must belong to the print bed");
            require(instance->getSlicingConfigDBId() == config->getDBInstanceID(), "Model must use the active slicing configuration");
            const auto parts = instance->parts();
            require(parts.size() == 1 && parts.front()->geometry(), "Model must own one mesh part");
            require(parts.front()->getFilamentBindingMode() == static_cast<int>(ModelFilamentBindingMode::InheritProjectDefault),
                    "Generated models must inherit filament like imported meshes");
            const auto bounds = instance->worldBounds();
            require(bounds.valid && near(bounds.min.z, 7), "Automatic placement must rest on the build-volume origin");
            require(near(bounds.max.z - bounds.min.z, shape == "cylinder" || shape == "cone" ? 30 : 20),
                    "Primitive dimensions and Z axis must match the requested size");
            const Vector3 center = (bounds.min + bounds.max) * 0.5f;
            require(instance->intersectPickRay(
                        {center.x, center.y, bounds.max.z + 10}, {0, 0, -1},
                        std::numeric_limits<float>::infinity()).has_value(),
                    "Generated model must be pickable through the ordinary model mesh");
            auto geometry = parts.front()->geometry()->read();
            require(static_cast<bool>(geometry), "Model geometry must have a published snapshot");
            auto clean = vtkSmartPointer<vtkCleanPolyData>::New();
            auto polyData = vtkSmartPointer<vtkPolyData>::New();
            polyData->DeepCopy(const_cast<vtkPolyData*>(&geometry.polyData()));
            clean->SetInputData(polyData);
            auto edges = vtkSmartPointer<vtkFeatureEdges>::New();
            edges->SetInputConnection(clean->GetOutputPort());
            edges->BoundaryEdgesOn();
            edges->NonManifoldEdgesOn();
            edges->FeatureEdgesOff();
            edges->ManifoldEdgesOff();
            edges->Update();
            require(edges->GetOutput()->GetNumberOfCells() == 0, "Printable primitive must be closed and manifold");
            auto volume = vtkSmartPointer<vtkMassProperties>::New();
            volume->SetInputData(polyData);
            volume->Update();
            const double pi = std::acos(-1.0);
            const double expected = shape == "cube" ? 8000 : shape == "sphere" ? 4000 * pi / 3 :
                shape == "cylinder" ? 3000 * pi : 1000 * pi;
            require(std::abs(volume->GetVolume() - expected) / expected < 0.015,
                    "Generated mesh must have the independently expected solid volume");
            instance.reset();
            require(history.undo(), "Model creation must be undoable");
            require(!document->getDBInstance(id) && document->getDBInstancesByType(TypeID::MODEL_INSTANCE_DB).size() == before,
                    "One undo must remove the newly created model");
            require(history.redo(), "Model creation must be redoable");
            instance = document->getDB<ModelInstanceDB>(id);
            require(instance && instance->parts().size() == 1 && instance->parts().front()->geometry(),
                    "Redo must restore the complete model graph");
            auto query = std::make_shared<ActionContext>();
            query->setActionCode("query.objects");
            query->setParams({{"type", shape}});
            SceneQueryHandler queryHandler;
            queryHandler.onEnter(query);
            const auto matches = query->getResult().toMap().value("objects").toList();
            require(!query->hasError() && matches.size() == 1 &&
                        matches.front().toMap().value("modelId").toLongLong() == id.getValue(),
                    "Scene query must find the generated shape through its model instance");
            std::cout << "PASS " << shape.toStdString() << " model graph, placement, closed solid, undo/redo\n";
        }
        const auto count = document->getAllDBInstanceIds().size();
        for (const auto& params : {QVariantMap{{"size", 0}}, QVariantMap{{"size", -1}},
                                  QVariantMap{{"size", std::numeric_limits<double>::infinity()}},
                                  QVariantMap{{"size", "invalid"}}, QVariantMap{{"color", "invalid"}}}) {
            require(create("cube", params)->hasError(), "Invalid creation parameters must fail");
            require(document->getAllDBInstanceIds().size() == count, "Failure must not leave partial model objects");
        }
        auto positioned = create("cube", {{"x", 0}, {"y", 0}, {"z", 0}});
        require(!positioned->hasError(), "An explicit origin must be accepted");
        auto atOrigin = document->getDB<ModelInstanceDB>(DBInstanceID(positioned->getResult().toMap().value("modelId").toULongLong()));
        require(atOrigin && near(atOrigin->getPosition().x, 0) && near(atOrigin->getPosition().y, 0) && near(atOrigin->getPosition().z, 0),
                "Explicit zero position must not be confused with automatic placement");
        std::cout << "PASS invalid input rollback and explicit origin\n";
        ModelLoaderUtil::LoadResult broken;
        broken.success = true;
        broken.triangles.emplace_back(
            Vector3(std::numeric_limits<float>::quiet_NaN(), 0, 0),
            Vector3(1, 0, 0), Vector3(0, 1, 0));
        const auto beforeFailure = document->getAllDBInstanceIds().size();
        require(!ModelCreationUtil::createModel(broken, {"Broken", {}, "generated", true}, {}),
                "Invalid mesh geometry must fail publication");
        require(document->getAllDBInstanceIds().size() == beforeFailure,
                "Geometry failure must leave no partial object graph");

        require(!create("cube", {{"color", "red"}})->hasError(), "Named color creation must work");
        // Independent STL input exercises the other consumer of publication.
        QTemporaryDir directory;
        require(directory.isValid(), "Unable to create STL fixture directory");
        const QString path = directory.filePath("tetrahedron.stl");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly), "Unable to write STL fixture");
        const QByteArray stl =
            "solid tetrahedron\n"
            "facet normal 0 0 -1\nouter loop\nvertex 0 0 0\nvertex 0 1 0\nvertex 1 0 0\nendloop\nendfacet\n"
            "facet normal 0 -1 0\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 0 1\nendloop\nendfacet\n"
            "facet normal -1 0 0\nouter loop\nvertex 0 0 0\nvertex 0 0 1\nvertex 0 1 0\nendloop\nendfacet\n"
            "facet normal 1 1 1\nouter loop\nvertex 1 0 0\nvertex 0 1 0\nvertex 0 0 1\nendloop\nendfacet\n"
            "endsolid tetrahedron\n";
        require(file.write(stl) == stl.size(), "Unable to write complete STL fixture");
        file.close();
        const auto loaded = ModelLoaderUtil::loadModel(path);
        require(loaded.success && loaded.triangles.size() == 4, "Existing STL loader must still load the mesh");
        const auto imported = ModelCreationUtil::createModel(
            loaded, {"Tetrahedron", path.toStdString(), "stl", true}, {{"color", "red"}});
        require(imported && imported.instance && near(imported.instance->worldBounds().min.z, 7),
                "Loaded STL must use the same model graph and bed placement");
        require(imported.object->getSourceFile() == path.toStdString(), "Import must retain its real source path");
        std::cout << "PASS malformed mesh rejection and subsequent STL loader/shared publication regression\n";
        const auto importedId = imported.instance->getDBInstanceID();
        document->clearAllGeometry();
        require(document->getDBInstancesByType(TypeID::MODEL_INSTANCE_DB).empty() &&
                    document->getDBInstancesByType(TypeID::MODEL_OBJECT_DB).empty() &&
                    document->getDBInstancesByType(TypeID::MODEL_PART_DB).empty() &&
                    document->getDBInstancesByType(TypeID::MODEL_GEOMETRY_DB).empty(),
                "Clear geometry must remove the complete generated and imported model graphs");
        require(document->getDBInstance(bed->getDBInstanceID()) &&
                    document->getDBInstance(config->getDBInstanceID()),
                "Clear geometry must preserve print bed and slicing configuration");
        require(history.undo() && document->getDBInstance(importedId),
                "Clear geometry undo must restore ordinary models");
        std::cout << "PASS unified scene query and clear/undo model lifecycle\n";

    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    Log::shutdown();
    return 0;
}
