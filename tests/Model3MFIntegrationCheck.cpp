#include "ApplicationModuleRegistration.hpp"
#include "ActionManager.hpp"
#include "ActionHandlerRegistry.hpp"
#include "DocumentManager.hpp"
#include "MenuData.hpp"
#include "MaterialDB.hpp"
#include "ModelGeometryDB.hpp"
#include "ModelGraphUtil.hpp"
#include "ModelInstanceDB.hpp"
#include "ModelObjectDB.hpp"
#include "ModelPartDB.hpp"
#include "ModelSurfaceColorDB.hpp"
#include "PrintBedDB.hpp"
#include "PrintBedTemplateDB.hpp"
#include "PrintBedPlacementUtil.hpp"
#include "SliceSettingsBridge.hpp"
#include "SliceSessionBuilder.hpp"
#include "SlicingBackend.hpp"
#include "SlicingConfigDB.hpp"
#include "TaskStateNotifier.hpp"
#include "TransactionManager.hpp"
#include "TransDBSerialization.hpp"
#include "NotificationManager.h"
#include "SlicingPreviewBridge.hpp"
#include "ToolpathResultPublication.hpp"
#include "AppInfo.h"
#include "SettingsHelper.h"
#include "LocalModelLibraryService.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <vtkCellArray.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <map>
#include <string>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
QByteArray bytes(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read controlled 3MF fixture");
    return file.readAll();
}
template<class T> std::vector<std::shared_ptr<T>> objects(TypeID type) {
    std::vector<std::shared_ptr<T>> result;
    for (const auto& value : DocumentManager::instance()->getDBInstancesByType(type))
        if (auto object = std::dynamic_pointer_cast<T>(value)) result.push_back(object);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a->getDBInstanceID().getValue() < b->getDBInstanceID().getValue();
    });
    return result;
}
DBInstanceID configurationActivePlate() {
    const auto configs=objects<GPlatform::SlicingConfigDB>(TypeID::SLICING_CONFIG_DB);
    require(configs.size()==1,"Expected one live slicing configuration");
    return configs.front()->getActivePrintBedDBId();
}
QString importAndWait(const QString& filename, const QString& intent,
                      const QString& action = "file.import") {
    const auto path = QStringLiteral(THREE_MF_FIXTURES) + '/' + filename;
    const auto source = bytes(path);
    auto* actions = ActionManager::getInstance();
    QStringList warnings;
    const auto warningConnection = QObject::connect(NotificationManager::instance(),
        &NotificationManager::warningOccurred, actions,
        [&](const QString&, const QString& message) { warnings.push_back(message); });
    const auto disconnectWarning = qScopeGuard([&] { QObject::disconnect(warningConnection); });
    int completions=0;
    const auto connection=QObject::connect(actions, &ActionManager::actionCompleted,
        actions, [&](const QString& action, bool) {
            // This observes cleanup, not success. The independently checked
            // published graph below owns the business success assertion.
            if (action=="ProjectImportHandler") ++completions;
        });
    const auto disconnect=qScopeGuard([&]{QObject::disconnect(connection);});
    QVariantMap params{{"filePath",path},{"confirmReplace",true},
        {"requestId",QUuid::createUuid().toString(QUuid::WithoutBraces)}};
    if (!intent.isEmpty()) params.insert("intent",intent);
    auto* library = LocalModelLibraryService::instance();
    const auto libraryId = action == "model.library.import" ? library->registerSessionModel(path) : QString{};
    const auto unregister = qScopeGuard([&] { if (!libraryId.isEmpty()) library->unregisterSessionModel(libraryId); });
    if (action == "model.library.import") {
        require(!libraryId.isEmpty(), "Cannot register the controlled library model");
        params.remove("filePath");
        params.insert("libraryItemId",libraryId);
    }
    const auto receipt=actions->invokeActionWithReceipt(action, params);
    require(receipt.value("accepted").toBool() && receipt.value("success").toBool(),
            "Common file.import rejected controlled 3MF input");
    if (!action.startsWith("project.")) {
        const auto routed=receipt.value("result").toMap();
        require(routed.value("importAction").toString()=="project.import" && routed.value("importAccepted").toBool(),
                "3MF did not route to the real ProjectImportHandler");
    }
    require(QTest::qWaitFor([&]{return completions==1 && !TaskStateNotifier::instance()->isBusy();},20000),
            "Concrete 3MF task did not complete exactly once and release its busy state");
    require(source == bytes(path), "Application import modified its controlled input");
    DocumentManager::instance()->flushPendingChanges();
    return warnings.join('\n');
}

// Independent controlled XML: four vertices and four triangle indices. Orca
// centers each resource mesh at (5,5,5); the Part transform retains that offset.
const std::array<Vector3, 4> vertices{{{-5,-5,-5},{5,-5,-5},{-5,5,-5},{-5,-5,5}}};
const std::array<std::array<vtkIdType, 3>, 4> faces{{{{0,2,1}},{{0,1,3}},{{0,3,2}},{{1,2,3}}}};
void checkMesh(const std::shared_ptr<ModelPartDB>& part) {
    auto* document = DocumentManager::instance();
    const auto geometry = part->geometry();
    require(geometry && document->getOwner(geometry->getDBInstanceID()) == part->getDBInstanceID(),
            "Part geometry is absent or not owned by the Part");
    const auto read = geometry->read();
    require(bool(read), "Imported geometry has no immutable VTK version");
    auto& data = const_cast<vtkPolyData&>(read.polyData());
    require(data.GetNumberOfPoints() == 4 && data.GetNumberOfCells() == 4,
            "Controlled tetrahedron mesh has the wrong vertex/triangle count");
    for (vtkIdType i=0; i<4; ++i) {
        double p[3]; data.GetPoint(i,p);
        require(std::abs(p[0]-vertices[i].x)<1e-5 && std::abs(p[1]-vertices[i].y)<1e-5 && std::abs(p[2]-vertices[i].z)<1e-5,
                "Imported VTK vertex differs from the independent centered tetrahedron");
    }
    data.GetPolys()->InitTraversal();
    for (const auto& expected : faces) {
        vtkIdType count=0; const vtkIdType* ids=nullptr;
        require(data.GetPolys()->GetNextCell(count,ids) && count==3 &&
                std::equal(expected.begin(),expected.end(),ids), "Imported VTK triangle order/content differs from source XML");
    }
    const auto material = part->getMaterial();
    require(material && document->getOwner(material->getDBInstanceID()) == part->getDBInstanceID(),
            "Part material ownership was not restored");
}
void checkForeignInstances() {
    auto* document = DocumentManager::instance();
    const auto models = objects<ModelObjectDB>(TypeID::MODEL_OBJECT_DB);
    const auto instances = objects<ModelInstanceDB>(TypeID::MODEL_INSTANCE_DB);
    if (models.size()!=2 || instances.size()!=2 || models[0]->parts().size()!=1 || models[1]->parts().size()!=1) {
        throw std::runtime_error("Foreign-instance graph count mismatch: objects="+std::to_string(models.size())+
            " instances="+std::to_string(instances.size())+" parts="+
            std::to_string(models.empty()?0:models[0]->parts().size()));
    }
    // Unlike native Orca archives, this vendor-neutral XML has no Application
    // metadata. Original bbs_3mf.cpp splits such multi-instance resources into
    // separate objects before constructing volumes (the native foreign rule).
    const std::array<std::shared_ptr<ModelPartDB>,2> parts{{models[0]->parts().front(),models[1]->parts().front()}};
    for (int i=0;i<2;++i) {
        checkMesh(parts[i]);
        require(instances[i]->object()==models[i] &&
                document->getOwner(instances[i]->getDBInstanceID())==models[i]->getDBInstanceID() &&
                document->getOwner(parts[i]->getDBInstanceID())==models[i]->getDBInstanceID(),
                "Foreign source instance has the wrong independent object owner");
    }
    require(parts[0]->geometry()!=parts[1]->geometry(),
            "Foreign multi-instance import did not preserve Orca's independent geometry copies");
    require(instances[0]->getPrintable() && !instances[1]->getPrintable(),
            "Source printable=false was lost");
    const auto a=instances[0]->getTransformMatrix(), b=instances[1]->getTransformMatrix();
    auto identity=Transform::Matrix4::Identity().eval();
    require(a.block<3,3>(0,0).isApprox(identity.block<3,3>(0,0),1e-5f) &&
            b.block<3,3>(0,0).isApprox((Eigen::Vector3f(-1,1,1).asDiagonal()).toDenseMatrix(),1e-5f),
            "Source mirrored instance transform was lost");
    require(std::abs((b(0,3)-a(0,3))-((60-5)-(20+5)))<1e-4 && std::abs(b(1,3)-a(1,3))<1e-4 && std::abs(b(2,3)-a(2,3))<1e-4,
            "Geometry append changed relative source instance placement");
    // Single-volume Orca centering moves source center (5,5,5) into each
    // instance: source centers are (25,35,5) and (55,35,5), 30mm apart.
    // Verify every corresponding source point after one common append offset,
    // rather than confusing source origin distance40 with center distance30.
    const Eigen::Vector4f first=a*parts[0]->getLocalTransform().getMatrix()*Eigen::Vector4f(-5,-5,-5,1);
    const Eigen::Vector3f offset=first.head<3>()-Eigen::Vector3f(20,30,0);
    require(std::abs(offset.z())<1e-4, "Geometry append moved the model above or below its source bed");
    for (int instance=0;instance<2;++instance) for (const auto& v:vertices) {
        const Eigen::Vector4f world=instances[instance]->getTransformMatrix()*parts[instance]->getLocalTransform().getMatrix()*
            Eigen::Vector4f(v.x,v.y,v.z,1);
        const Eigen::Vector3f source(instance==0 ? v.x+5+20 : -(v.x+5)+60,v.y+5+30,v.z+5);
        require((world.head<3>()-source-offset).norm()<1e-4,
                "Shared imported vertices do not preserve source mirror/relative placement");
    }
    std::cout << "foreign shared resource: native object split, exact geometry, mirror, printable flag and ownership verified\n";
}

std::vector<std::shared_ptr<ModelPartDB>> checkFivePartGraph(const std::shared_ptr<ModelObjectDB>& model) {
    auto* document=DocumentManager::instance();
    auto parts=model->parts();
    std::sort(parts.begin(),parts.end(),[](const auto& a,const auto& b){return a->getOrderIndex()<b->getOrderIndex();});
    require(parts.size()==5,"Complete source graph lost a role");
    // Native Orca sorts support blockers before enforcers; source resource5 is
    // therefore Part3, source resource4 Part4. No SDK DTO is used as an oracle.
    const std::array<int,5> roles{{0,3,4,2,1}};
    const std::array<Vector3,5> translations{{{5,5,5},{2,2,2},{3,3,2},{2,4,1},{4,2,1}}};
    for (std::size_t i=0;i<parts.size();++i) {
        const auto& part=parts[i];
        require(part->getRole()==roles[i] && part->getOrderIndex()==int(i) &&
                document->getOwner(part->getDBInstanceID())==model->getDBInstanceID(),
                "Project role/order/part ownership differs from source XML");
        auto expected=Transform::Matrix4::Identity().eval();
        const float scale=i==0?1.f:.2f;
        expected(0,0)=expected(1,1)=expected(2,2)=scale;
        expected(0,3)=translations[i].x; expected(1,3)=translations[i].y; expected(2,3)=translations[i].z;
        require(part->getLocalTransform().getMatrix().isApprox(expected,1e-5f),
                "Part local transform differs from independently transformed source vertices");
        checkMesh(part);
    }
    require(parts[0]->surfaceColors() && document->getOwner(parts[0]->surfaceColors()->getDBInstanceID())==parts[0]->getDBInstanceID(),
            "Material painting was detached from its source Part");
    return parts;
}

void checkProject(bool unassigned) {
    auto* document = DocumentManager::instance();
    const auto models=objects<ModelObjectDB>(TypeID::MODEL_OBJECT_DB);
    const auto instances=objects<ModelInstanceDB>(TypeID::MODEL_INSTANCE_DB);
    const auto beds=objects<PrintBedDB>(TypeID::PRINT_BED_DB);
    require(models.size()==1 && beds.size()==2 && instances.size()==(unassigned?3u:2u),
            "Project replacement has the wrong object/plate/instance count");
    const auto model=models.front();
    require(objects<ModelPartDB>(TypeID::MODEL_PART_DB).size()==5 && objects<ModelGeometryDB>(TypeID::MODEL_GEOMETRY_DB).size()==5,
            "Project replacement retained a previous graph");
    const auto parts=checkFivePartGraph(model);
    const auto semantics=QJsonDocument::fromJson(QByteArray::fromStdString(model->getProjectSemantics())).object();
    require(semantics.value("settings").toObject().value("wall_loops").toString()=="2" &&
            semantics.value("layer_height_profile").toArray()==QJsonDocument::fromJson("[0,0.2,5,0.1,10,0.2]").array() &&
            semantics.value("layer_ranges").toArray()==QJsonDocument::fromJson(
                "[{\"minimum_z\":1,\"maximum_z\":3,\"settings\":{\"layer_height\":\"0.12\",\"wall_loops\":\"4\"}}]").array() &&
            semantics.value("brim_points").toArray()==QJsonDocument::fromJson(
                "[{\"x\":0,\"y\":0,\"z\":0,\"head_front_radius\":1}]").array(),
            "Object-level process overrides or adaptive layer data were dropped");
    for (std::size_t i=0;i<beds.size();++i) {
        const auto definition=beds[i]->getTemplateDB();
        require(definition && definition->getWidth()==180 && definition->getHeight()==180 && definition->getPrintHeight()==180,
                "Imported project did not restore its independent machine definition");
        require(instances[i]->object()==model && document->getOwner(instances[i]->getDBInstanceID())==model->getDBInstanceID() &&
                instances[i]->getParentPrintBedDBId()==beds[i]->getDBInstanceID(), "Imported instance has the wrong owning object or print plate");
        const auto matrix=PrintBedPlacementUtil::instancePlateMatrix(*instances[i]);
        auto expected=Transform::Matrix4::Identity().eval(); expected(0,3)=20; expected(1,3)=30;
        require(matrix.isApprox(expected,1e-4f), "Project plate-local placement differs from source XML");
        const auto instanceParts=instances[i]->parts();
        require(instanceParts.size()==parts.size(),"Project instance lost a shared part");
        for (const auto& part : parts) {
            const auto found=std::find(instanceParts.begin(),instanceParts.end(),part);
            require(found!=instanceParts.end() && (*found)->geometry()==part->geometry(),
                    "Native project instances duplicated their shared Part/geometry resources");
        }
    }
    if (unassigned) {
        const auto& last=instances.back();
        require(last->object()==model && document->getOwner(last->getDBInstanceID())==model->getDBInstanceID() &&
                !last->getParentPrintBedDBId().isValid(), "Out-of-plate instance was assigned to a source plate or lost its object owner");
        const auto matrix=last->getTransformMatrix();
        require(std::abs(matrix(0,3)-9000)<1e-4 && std::abs(matrix(1,3)-9000)<1e-4,
                "Unassigned source instance was moved into a print plate");
        // Use the same public capture/build boundary as SlicingHandler. It
        // accepts explicit plate targets and rejects any unassigned instance;
        // this does not replace the Handler's private target-collection logic.
        for (std::size_t plate=0;plate<beds.size();++plate) {
            require(ActionManager::getInstance()->triggerAction("plate.activate",
                    {{"printBedId",QString::number(beds[plate]->getDBInstanceID().getValue())}}),
                    "Cannot activate imported print plate for slice-input validation");
            require(configurationActivePlate()==beds[plate]->getDBInstanceID(),
                    "Plate activation did not reach the real slicing configuration");
            SliceSessionBuilder::Request request;
            request.modelIds={instances[plate]->getDBInstanceID()};
            const auto captured=SliceSessionBuilder::capture(request);
            if (!captured) throw std::runtime_error("Assigned plate capture failed: "+captured.error);
            const auto built=SliceSessionBuilder::build(*captured.request);
            if (!built) throw std::runtime_error("Assigned plate build failed: "+built.error);
            const auto& session=*built.session;
            require(session.sources.size()==1 &&
                    session.sources.front().modelId==std::uint64_t(instances[plate]->getDBInstanceID().getValue()) &&
                    session.request.objects.size()==1 && session.request.objects.front().volumes.size()==5,
                    "Plate slice input contains a different instance or an unexpected mesh object");
            const auto& input=session.request.objects.front();
            require(std::abs(input.transform[3]-20)<1e-4 && std::abs(input.transform[7]-30)<1e-4 &&
                    std::abs(input.transform[11])<1e-4,
                    "Plate slice input retained scene placement or included the unassigned 9000mm instance");
            request.modelIds.push_back(last->getDBInstanceID());
            const auto contaminated=SliceSessionBuilder::capture(request);
            require(!contaminated && contaminated.error.find("assigned print bed")!=std::string::npos,
                    "Unassigned instance was admitted into a valid plate slice input");
        }
        SliceSessionBuilder::Request unassignedRequest;
        unassignedRequest.modelIds={last->getDBInstanceID()};
        const auto rejected=SliceSessionBuilder::capture(unassignedRequest);
        require(!rejected && rejected.error.find("assigned print bed")!=std::string::npos,
                "Unassigned instance alone produced an eligible slicing request");
        std::cout<<"slice-input exclusion: both assigned plates contain only their own source; unassigned input is rejected\n";
    }
    std::cout << (unassigned?"unassigned":"complete") << " project: mesh, five local transforms/roles, shared ownership and plate mapping verified\n";
}
using GraphState=std::map<qlonglong,trans::SerializedState>;
void checkGeometryAnnotationLifetime() {
    auto* document = DocumentManager::instance();
    auto& history = TransactionManager::instance();
    const auto model = objects<ModelObjectDB>(TypeID::MODEL_OBJECT_DB).front();
    const auto parts = checkFivePartGraph(model);
    const auto part = parts.front();
    const auto colors = part->surfaceColors();
    require(!part->getSurfaceAnnotations().empty() && !colors->getSurfaceColorData().empty() &&
            part->getAnnotationsTopologyFingerprint() == colors->getTopologyFingerprint() &&
            part->getAnnotationsSourceTriangleCount() == 4 && colors->getSourceTriangleCount() == 4,
            "Expected real imported annotations and material painting");
    const auto partState = trans::serializeState(*part);
    const auto colorState = trans::serializeState(*colors);
    const auto semantics = part->getProjectSemantics();
    const auto original = part->geometry()->read();
    const auto copyMesh = [&] {
        auto copy = vtkSmartPointer<vtkPolyData>::New();
        copy->DeepCopy(const_cast<vtkPolyData*>(&original.polyData()));
        return copy;
    };
    const auto annotationsRestored = [&] {
        require(trans::serializeState(*part) == partState && trans::serializeState(*colors) == colorState,
                "Identical mesh or undo changed valid imported annotations");
        checkMesh(part);
    };
    history.clear();
    require(ModelGraphUtil::replacePartGeometry(part->getDBInstanceID(), copyMesh()), "Identical mesh replacement failed");
    document->flushPendingChanges();
    annotationsRestored();
    for (bool reorderTriangles : {false, true}) {
        auto changed = copyMesh();
        if (reorderTriangles) {
            auto cells = vtkSmartPointer<vtkCellArray>::New();
            for (const auto index : {1, 0, 2, 3}) cells->InsertNextCell(3, faces[index].data());
            changed->SetPolys(cells);
        } else {
            double point[3]; changed->GetPoint(0, point);
            point[0] += .25; changed->GetPoints()->SetPoint(0, point);
        }
        history.clear();
        require(ModelGraphUtil::replacePartGeometry(part->getDBInstanceID(), changed), "Changed mesh replacement failed");
        const auto annotationsCleared = [&] {
            require(part->getSurfaceAnnotations().empty() && part->getAnnotationsTopologyFingerprint().empty() &&
                    part->getAnnotationsSourceTriangleCount() == 0 && colors->getSurfaceColorData().empty() &&
                    colors->getTopologyFingerprint().empty() && colors->getSourceTriangleCount() == 0,
                    "Changed triangle geometry retained stale annotations");
            require(part->getProjectSemantics() == semantics, "Mesh replacement lost parameter settings");
        };
        annotationsCleared();
        require(history.getUndoStackSize() == 1 && history.undo(), "Mesh and annotations were not one edit");
        document->flushPendingChanges(); annotationsRestored();
        require(history.redo(), "Cannot redo mesh and annotation replacement");
        document->flushPendingChanges(); annotationsCleared();
        require(history.undo(), "Cannot restore original mesh after redo");
        document->flushPendingChanges(); annotationsRestored();
    }
    history.clear();
    std::cout << "geometry editing: identical meshes preserve imported painting; changed vertices/triangle order invalidate and undo/redo together\n";
}

GraphState graphState(const std::shared_ptr<ModelObjectDB>& model) {
    GraphState state;
    const auto add=[&](const auto& value) {
        require(bool(value),"Graph snapshot contains a missing object");
        state.emplace(value->getDBInstanceID().getValue(),trans::serializeState(*value));
    };
    add(model);
    for (const auto& part:model->parts()) {
        add(part); add(part->geometry()); add(part->getMaterial());
        if (const auto colors=part->surfaceColors()) add(colors);
    }
    for (const auto& instance:model->instances()) add(instance);
    return state;
}
void checkComplexAppendHistory() {
    auto* document=DocumentManager::instance();
    auto* actions=ActionManager::getInstance();
    const auto original=objects<ModelObjectDB>(TypeID::MODEL_OBJECT_DB).front();
    const auto originalState=graphState(original);
    const auto originalUnchanged=[&] {
        for (const auto& [id,bytes]:originalState) {
            const auto current=document->getDBInstance(DBInstanceID(id));
            require(current && trans::serializeState(*current)==bytes,"Appending/undoing changed the pre-existing model graph");
        }
        checkFivePartGraph(original);
    };
    importAndWait("complete-project.3mf","geometry");
    const auto models=objects<ModelObjectDB>(TypeID::MODEL_OBJECT_DB);
    require(models.size()==2 && objects<ModelPartDB>(TypeID::MODEL_PART_DB).size()==10 &&
            objects<ModelGeometryDB>(TypeID::MODEL_GEOMETRY_DB).size()==10 &&
            objects<ModelInstanceDB>(TypeID::MODEL_INSTANCE_DB).size()==4,
            "Complex append did not create exactly one five-part/two-instance graph");
    const auto added=models.back();
    require(added!=original,"Complex append reused the pre-existing object");
    checkFivePartGraph(added); originalUnchanged();
    const auto addedState=graphState(added);
    std::map<qlonglong,Transform::Matrix4> poses;
    const auto addedInstances=added->instances();
    require(addedInstances.size()==2,"Complex appended object lost an instance");
    const auto activeBed=configurationActivePlate();
    for (const auto& instance:addedInstances) {
        require(instance->object()==added && document->getOwner(instance->getDBInstanceID())==added->getDBInstanceID() &&
                instance->getParentPrintBedDBId()==activeBed,"Appended instance has wrong object/bed ownership");
        poses.emplace(instance->getDBInstanceID().getValue(),instance->getTransformMatrix());
    }
    const auto a=addedInstances[0]->getTransformMatrix(), b=addedInstances[1]->getTransformMatrix();
    require(std::abs(std::abs(b(0,3)-a(0,3))-216)<1e-4 && std::abs(b(1,3)-a(1,3))<1e-4,
            "Complex geometry append changed the source instance separation");
    require(actions->triggerAction("edit.undo"),"Cannot undo complex geometry append");
    for (const auto& [id,bytes]:addedState)
        require(!document->getDBInstance(DBInstanceID(id)),"Undo left a node from the appended complex graph");
    originalUnchanged(); checkProject(false);
    require(actions->triggerAction("edit.redo"),"Cannot redo complex geometry append");
    const auto restored=document->getDB<ModelObjectDB>(added->getDBInstanceID());
    require(bool(restored),"Redo did not restore the appended object");
    checkFivePartGraph(restored); originalUnchanged();
    for (const auto& [id,bytes]:addedState) {
        const auto current=document->getDBInstance(DBInstanceID(id));
        require(current && trans::serializeState(*current)==bytes,"Redo changed an appended object property");
    }
    for (const auto& [id,matrix]:poses) {
        const auto instance=document->getDB<ModelInstanceDB>(DBInstanceID(id));
        require(instance && instance->getTransformMatrix().isApprox(matrix,1e-5f) && instance->object()==restored &&
                document->getOwner(instance->getDBInstanceID())==restored->getDBInstanceID() &&
                instance->getParentPrintBedDBId()==activeBed,"Redo did not restore complex instance placement/ownership");
    }
    require(objects<ModelObjectDB>(TypeID::MODEL_OBJECT_DB).size()==2 &&
            objects<ModelPartDB>(TypeID::MODEL_PART_DB).size()==10 &&
            objects<ModelGeometryDB>(TypeID::MODEL_GEOMETRY_DB).size()==10 &&
            objects<ModelInstanceDB>(TypeID::MODEL_INSTANCE_DB).size()==4,"Redo restored an incomplete or duplicate complex graph");
    std::cout<<"complex append undo/redo: five roles, exact mesh/local matrices, every owned node and original graph preserved\n";
}

}
void checkPreviewPlacement() {
    using namespace GPlatform;
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create preview fixture directory");
    const auto path = directory.filePath("plate-local.gcode");
    const QByteArray source = "G90\nM83\nG1 Z0.2 F3000\n;TYPE:Sparse infill\nG1 X20 Y30 E1 F600\nG1 X30 Y30 E1\n";
    { QFile file(path); require(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),
                              "Cannot write preview fixture"); }
    auto library = libslicer::Library::open();
    const auto prepared = prepareImportedToolpath(path, [&](const std::string& input) {
        return library->load_gcode_preview({input});
    });
    require(prepared.geometryAvailable, "Preview fixture has no drawable geometry");
    const auto beds = objects<PrintBedDB>(TypeID::PRINT_BED_DB);
    require(beds.size() == 2 && std::abs(beds[1]->getPosition().x - 216) < 1e-4,
            "Preview check requires the actual imported second plate");
    auto* bridge = SlicingPreviewBridge::instance();
    for (const auto& bed : beds) {
        for (const auto scope : {ToolpathPreviewSourceScope::Plate, ToolpathPreviewSourceScope::SingleModel,
                                ToolpathPreviewSourceScope::ImportedGCode, ToolpathPreviewSourceScope::Calibration}) {
            auto output = std::make_shared<SliceOutputRecord>();
            output->setArtifacts(path, false, {}, false, bridge->currentOutput());
            output->name = "Plate preview"; output->sourceScope = scope;
            output->printBedId = bed->getDBInstanceID();
            const auto result = publishToolpathResult(output, prepared, bridge->beginOutputTask());
            require(result.accepted && result.preview &&
                    result.preview->getTransformMatrix().isApprox(bed->getTransformMatrix(), 1e-5f),
                    "Plate-local toolpath preview does not have its print bed display transform");
            require(bytes(path) == source, "Preview display positioning changed the G-code coordinates");
            require(DocumentManager::instance()->getOwner(result.preview->getDBInstanceID()) == bed->getDBInstanceID(),
                    "Preview was assigned to a different print bed");
            bridge->clearPreview();
        }
    }
    std::cout << "preview placement: both imported plates/all source scopes; G-code bytes unchanged\n";
}

void checkPlateMaterialSynchronization() {
    importAndWait("complete-project.3mf", "project");
    auto* bridge = SliceSettingsBridge::instance();
    const auto beds = objects<PrintBedDB>(TypeID::PRINT_BED_DB);
    require(beds.size() == 2, "Material regression requires both imported plates");
    std::vector<QJsonObject> expected;
    for (const auto& bed : beds) {
        auto semantics = QJsonDocument::fromJson(QByteArray::fromStdString(bed->getProjectSemantics())).object();
        auto settings = semantics["settings"].toObject();
        settings["sparse_infill_density"] = "23%";
        semantics["settings"] = settings;
        expected.push_back(semantics);
        settings["filament_map"] = "1";
        settings["filament_map_mode"] = "Auto For Flush";
        semantics["settings"] = settings;
        bed->setProjectSemantics(QJsonDocument(semantics).toJson(QJsonDocument::Compact).toStdString());
    }
    std::vector<std::string> partSemantics;
    const auto parts = objects<ModelPartDB>(TypeID::MODEL_PART_DB);
    for (const auto& part : parts) partSemantics.push_back(part->getProjectSemantics());
    require(bridge->selectMachine("P3", "0.4"), "Cannot switch imported project to P3");
    // Check immediately, without draining the queued platform-geometry signal:
    // material synchronization must precede publication of the new revision.
    const auto snapshot = GPlatform::SlicingBackend::instance().configSnapshot();
    require(snapshot && snapshot->value("filament_map") == std::optional<std::string>("1,2,3,4,5,6"),
            "P3 did not publish its six physical material bindings");
    for (size_t i = 0; i < beds.size(); ++i)
        require(QJsonDocument::fromJson(QByteArray::fromStdString(beds[i]->getProjectSemantics())).object() == expected[i],
                "Not all plate mappings were synchronized, or unrelated semantics changed");
    for (size_t i = 0; i < parts.size(); ++i)
        require(parts[i]->getProjectSemantics() == partSemantics[i], "Machine switch changed part/modifier settings");
    auto illegal = expected.front();
    auto illegalSettings = illegal["settings"].toObject();
    illegalSettings["filament_map"] = "7,2,3,4,5,6";
    illegalSettings["filament_map_mode"] = "Manual";
    illegal["settings"] = illegalSettings;
    const auto illegalPayload = QJsonDocument(illegal).toJson(QJsonDocument::Compact).toStdString();
    beds.front()->setProjectSemantics(illegalPayload);
    require(bridge->selectProcessPreset(bridge->selectedProcessPresetId()), "Cannot refresh current process preset");
    require(beds.front()->getProjectSemantics() == illegalPayload,
            "Unchanged material domain silently repaired an explicit illegal plate mapping");
    require(bridge->selectMachine("Flashforge AD5X", "0.4"), "Cannot return to ordinary machine");
    for (const auto& bed : beds) {
        auto semantics = QJsonDocument::fromJson(QByteArray::fromStdString(bed->getProjectSemantics())).object();
        auto settings = semantics["settings"].toObject();
        settings["filament_map"] = "1";
        settings["filament_map_mode"] = "Manual";
        semantics["settings"] = settings;
        bed->setProjectSemantics(QJsonDocument(semantics).toJson(QJsonDocument::Compact).toStdString());
    }
    for (const auto count : {4, 2}) {
        require(bridge->setFilamentSlotCount(count), "Cannot resize ordinary material slots");
        for (const auto& bed : beds) {
            const auto settings = QJsonDocument::fromJson(QByteArray::fromStdString(bed->getProjectSemantics())).object()["settings"].toObject();
            require(settings["filament_map"].toString() == (count == 4 ? "1,1,1,1" : "1,1") &&
                    settings["filament_map_mode"].toString() == "Manual" && settings["sparse_infill_density"].toString() == "23%",
                    "Slot resize failed to preserve and resize both manual plate mappings");
        }
    }
    std::cout << "all imported plates: synchronous P3 bindings, add/remove slots and local semantics preserved\n";
}

int main(int argc,char** argv) {
    qputenv("QT_QPA_PLATFORM","offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc,argv);
    app.setOrganizationName("GPlatformTests");
    app.setApplicationName("3MF-"+QUuid::createUuid().toString(QUuid::Id128));
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    const auto configDirectory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto cleanup=qScopeGuard([&]{QDir(configDirectory).removeRecursively();});
    int status=0;
    try {
        require(settings.isValid(),"Cannot create isolated 3MF integration settings");
        SettingsHelper::getInstance()->init(argv);
        registerApplicationModules();
        MenuData::getInstance()->loadMenusFromFile(QStringLiteral(THREE_MF_MENUS));
        QQmlEngine engine;
        auto* configuration=SliceSettingsBridge::instance();
        require(configuration->selectMachine("Flashforge AD5X","0.4"),"Cannot activate the initial real slicer configuration");
        auto definition=trans::TransDB::create<PrintBedTemplateDB>();
        auto bed=trans::TransDB::create<PrintBedDB>();
        bed->setTemplateDBId(definition->getDBInstanceID());
        // With no UI host, an omitted intent must request a decision and leave
        // the document intact; aliases must never silently append geometry.
        const auto initialRevision = GPlatform::SlicingBackend::instance().configurationRevision();
        for (const auto* action : {"file.import", "import_model", "model.library.import"}) {
            const auto warnings = importAndWait("shared-instances.3mf",{},action);
            require(warnings.contains("Confirm the import options to continue."),
                    "An interactive 3MF alias silently chose an import mode");
            require(objects<ModelObjectDB>(TypeID::MODEL_OBJECT_DB).empty() &&
                    objects<PrintBedDB>(TypeID::PRINT_BED_DB).size()==1 &&
                    GPlatform::SlicingBackend::instance().configurationRevision()==initialRevision,
                    "An undecided import modified the document or configuration");
        }
        importAndWait("shared-instances.3mf","geometry","model.library.import"); checkForeignInstances();
        require(ActionManager::getInstance()->triggerAction("edit.undo"),"Cannot undo shared 3MF append");
        require(objects<ModelInstanceDB>(TypeID::MODEL_INSTANCE_DB).empty() && objects<ModelObjectDB>(TypeID::MODEL_OBJECT_DB).empty(),
                "Undo left an imported shared model graph");
        require(ActionManager::getInstance()->triggerAction("edit.redo"),"Cannot redo shared 3MF append"); checkForeignInstances();
        importAndWait("complete-project.3mf","project","model.library.import"); checkProject(false);
        checkGeometryAnnotationLifetime();
        configuration->setSetting("layer_height",0.16);
        const auto edited=GPlatform::SlicingBackend::instance().configSnapshot();
        require(edited && edited->value("layer_height")==std::optional<std::string>("0.16"),
                "Normal settings Bridge did not edit the effective backend layer height");
        checkComplexAppendHistory();
        importAndWait("complete-project-unassigned.3mf","project"); checkProject(true);
        const auto reopened=GPlatform::SlicingBackend::instance().configSnapshot();
        require(reopened && reopened->value("layer_height")==std::optional<std::string>("0.2"),
                "Project replacement retained the previous backend layer-height edit");
        checkPreviewPlacement();
        AppInfo::getInstance()->setLocale("zh_CN");
        AppInfo::getInstance()->initializeTranslations();
        const auto warnings = importAndWait("complete-project-unknown.3mf", "project");
        require(warnings.contains(QString::fromUtf8("项不支持的设置")) &&
                warnings.contains(QString::fromUtf8("修改过的 G-code 或自定义打印机／耗材预设")) &&
                !warnings.contains("project.preset_gcode_warning"),
                "Unknown-setting summary hid the custom preset warning or showed a protocol code");
        checkPlateMaterialSynchronization();
        importAndWait("complete-project.3mf",{},"project.open"); checkProject(false);
        const auto beforeAppend = objects<ModelInstanceDB>(TypeID::MODEL_INSTANCE_DB).size();
        importAndWait("shared-instances.3mf","geometry","project.open");
        require(objects<ModelInstanceDB>(TypeID::MODEL_INSTANCE_DB).size()==beforeAppend+2,
                "Explicit geometry intent on project.open replaced the document");
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n'; status=1;}
    ActionManager::getInstance()->cleanupBeforeShutdown();
    ActionHandlerRegistry::instance().clearCache();
    return status;
}
