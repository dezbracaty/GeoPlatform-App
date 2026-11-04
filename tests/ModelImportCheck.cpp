#include <UnifiedModelImportHandler.hpp>
#include <DocumentManager.hpp>
#include <ModelInstanceDB.hpp>
#include <PrintBedDB.hpp>
#include <PrintBedTemplateDB.hpp>
#include <SlicingConfigDB.hpp>
#include <TransactionManager.hpp>
#include <DBRelationsRegistration.hpp>
#include <ModelGraphRelations.hpp>
#include <PrintBedRelations.hpp>
#include <Foundation/Log.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <vtkCubeSource.h>
#include <vtkCell.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkTriangleFilter.h>
#include <cmath>
#include <algorithm>
#include <utility>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool near(float a, float b) { return std::abs(a - b) < std::max(1e-6f, std::abs(b) * 1e-4f); }
ModelLoaderUtil::LoadResult box(float x, float y, float z) {
    vtkNew<vtkCubeSource> cube;
    cube->SetXLength(x); cube->SetYLength(y); cube->SetZLength(z);
    vtkNew<vtkTriangleFilter> filter;
    filter->SetInputConnection(cube->GetOutputPort()); filter->Update();
    auto* mesh = filter->GetOutput();
    ModelLoaderUtil::LoadResult result;
    result.success = true;
    result.boundingBoxMin = {-x/2, -y/2, -z/2};
    result.boundingBoxMax = {x/2, y/2, z/2};
    for (vtkIdType i = 0; i < mesh->GetNumberOfCells(); ++i) {
        auto* cell = mesh->GetCell(i);
        Vector3 vertices[3];
        for (int j = 0; j < 3; ++j) {
            double point[3]; mesh->GetPoint(cell->GetPointId(j), point);
            vertices[j] = {float(point[0]), float(point[1]), float(point[2])};
        }
        result.triangles.emplace_back(vertices[0], vertices[1], vertices[2]);
    }
    return result;
}
void writeStl(const QString& path, const ModelLoaderUtil::LoadResult& mesh) {
    QFile file(path); require(file.open(QIODevice::WriteOnly), "Cannot create STL fixture");
    QTextStream text(&file); text.setRealNumberPrecision(9); text << "solid unitless\n";
    for (const auto& tri : mesh.triangles) {
        text << "facet normal 0 0 0\nouter loop\n";
        for (const auto& v : {tri.vertex1,tri.vertex2,tri.vertex3}) text << "vertex " << v.x << ' ' << v.y << ' ' << v.z << "\n";
        text << "endloop\nendfacet\n";
    }
    text << "endsolid unitless\n"; text.flush(); file.close();
}

auto importModel(const QString& path, QVariantMap params) {
    auto context = std::make_shared<ActionContext>();
    context->setActionCode("import_model");
    context->setInvocationId("model-import-check");
    params.insert("filePath", path); context->setParams(params);
    UnifiedModelImportHandler handler;
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&handler, &IActionHandlerBase::requestBackgroundExecution, &handler, &IActionHandlerBase::onSuspend);
    QObject::connect(&handler, &IActionHandlerBase::requestResume, &handler, &IActionHandlerBase::onResume);
    QObject::connect(&handler, &IActionHandlerBase::invocationCompleted, &loop, &QEventLoop::quit);
    handler.onEnter(context);
    if (!context->hasError()) { timeout.start(5000); loop.exec(); require(timeout.isActive(), "Import completion timed out"); }
    handler.onExit();
    return context;
}
}
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    Log::setup(argv, "ModelImportCheck");
    try {
        ModelGraphRelations::registerRelations();
        GPlatform::DBRelations::PrintBedRelations::registerRelations();
        auto* document = DocumentManager::instance();
        auto definition = trans::TransDB::create<PrintBedTemplateDB>();
        definition->setWidth(220); definition->setHeight(220); definition->setOrigin({0,0,7});
        auto bed = trans::TransDB::create<PrintBedDB>(); bed->setTemplateDBId(definition->getDBInstanceID());
        auto config = trans::TransDB::create<GPlatform::SlicingConfigDB>();
        QTemporaryDir directory;
        const QString path = directory.filePath("unitless.stl");
        writeStl(path, box(0.02f,0.02f,0.02f));
        // Exercise unit hints through the production handler, not internal helpers.
        auto open = box(0.1f,0.1f,0.1f); open.triangles.pop_back();
        for (const auto& test : {
                std::pair{box(0.1f,0.1f,0.1f),1000.0f},
                std::pair{box(0.2f,0.2f,0.2f),25.4f},
                std::pair{box(1,1,1),25.4f},
                std::pair{box(2,2,2),1.0f},
                std::pair{box(100,1,1),1.0f},
                std::pair{open,1.0f}}) {
            const QString hintPath = directory.filePath("hint.stl");
            writeStl(hintPath,test.first);
            auto& history = TransactionManager::instance(); history.clear();
            const auto count = document->getAllDBInstanceIds().size();
            const auto context = importModel(hintPath,{});
            if (test.second > 1) {
                const auto result = context->getResult().toMap();
                require(context->hasError() && result.value("needsUnitConfirmation").toBool() &&
                        near(result.value("suggestedScale").toFloat(),test.second),
                        "Small-model volume thresholds must suggest the expected units");
                require(document->getAllDBInstanceIds().size() == count,
                        "Unit confirmation must not publish DB objects");
            } else {
                require(!context->hasError(),context->getError().toStdString().c_str());
                const auto id = DBInstanceID(context->getResult().toMap().value("modelId").toULongLong());
                auto model = document->getDB<ModelInstanceDB>(id);
                require(static_cast<bool>(model),"Ordinary/open meshes must import without a unit hint");
                const auto local = model->localBounds();
                const auto size = test.first.boundingBoxMax - test.first.boundingBoxMin;
                require(local.valid && near(local.max.x-local.min.x,size.x) &&
                        near(local.max.y-local.min.y,size.y) && near(local.max.z-local.min.z,size.z),
                        "Import without a unit hint must preserve mesh dimensions");
                model.reset();
                require(history.undo() && document->getAllDBInstanceIds().size() == count,
                        "Imported fixture must be removed in one undo");
            }
        }
        const auto before = document->getAllDBInstanceIds().size();
        const auto pending = importModel(path, {});
        require(pending->hasError() && pending->getResult().toMap().value("needsUnitConfirmation").toBool(),
                "Headless auto import must request explicit units instead of silently scaling");
        require(document->getAllDBInstanceIds().size() == before, "Confirmation must precede DB publication");
        for (const auto& parameters : {QVariantMap{{"unit","bad"}}, QVariantMap{{"scale",0}},
                QVariantMap{{"scale",std::numeric_limits<double>::infinity()}}, QVariantMap{{"scale","bad"}}}) {
            require(importModel(path,parameters)->hasError(), "Invalid unit/scale must be rejected");
            require(document->getAllDBInstanceIds().size() == before, "Rejected import must leave no DB residue");
        }
        for (const auto& test : {std::pair{QVariantMap{{"unit","mm"}},0.02f},
                std::pair{QVariantMap{{"unit","m"}},20.0f},
                std::pair{QVariantMap{{"unit","in"}},0.508f},
                std::pair{QVariantMap{{"unit","m"},{"scale",2}},40.0f}}) {
            auto& history = TransactionManager::instance(); history.clear();
            const auto context = importModel(path,test.first);
            require(!context->hasError(), context->getError().toStdString().c_str());
            const auto id = DBInstanceID(context->getResult().toMap().value("modelId").toULongLong());
            auto model = document->getDB<ModelInstanceDB>(id);
            require(static_cast<bool>(model), "Confirmed import must create the ordinary model graph");
            const auto bounds = model->worldBounds();
            // Check dimensions before world-space float translation loses tiny lengths.
            const auto local = model->localBounds();
            require(local.valid && near(local.max.x-local.min.x,test.second) &&
                    bounds.valid && near(bounds.min.z,7),
                    "Unit and custom scale must be applied once before bed placement");
            model.reset();
            require(history.undo() && !document->getDBInstance(id), "One undo must remove the imported model");
            require(history.redo() && document->getDB<ModelInstanceDB>(id), "Redo must restore the imported model");
        }
        std::cout << "PASS handler unit hints, open meshes, invalid parameters, headless confirmation, explicit units, placement and undo/redo\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    Log::shutdown(); return 0;
}
