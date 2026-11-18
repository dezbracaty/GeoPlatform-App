#include "ModelFileExportHandler.hpp"
#include "InteractionRegistration.hpp"
#include "ModelFileController.hpp"
#include "ModelMeshSnapshot.hpp"
#include "ModelCreationUtil.hpp"
#include "ModelSurfaceColorCodec.hpp"
#include "AIDescriptorHelper.hpp"
#include <SlicingBackend.hpp>
#include <DocumentManager.hpp>
#include <ModelObjectDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelPartDB.hpp>
#include <ModelSurfaceColorDB.hpp>
#include <FiberMaskDB.hpp>
#include <PrintBedPlacementUtil.hpp>
#include <SlicingConfigDB.hpp>
#include <NotificationManager.h>
#include <QtConcurrent/QtConcurrentRun>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <vtkPolyData.h>
#include <algorithm>
#include <map>
#include <set>

REGISTER_INTERACTION_ACTION(ModelFileExportHandler, "model.export", "project.save")
namespace {
std::array<double,16> matrixValues(const Transform::Matrix4& matrix) {
    std::array<double,16> out{};
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)out[row*4+col]=matrix(row,col);
    return out;
}
libslicer::ProjectPartRole projectRole(int role) {
    switch(ModelPartRole(role)) {
    case ModelPartRole::Model:return libslicer::ProjectPartRole::Model;
    case ModelPartRole::NegativeVolume:return libslicer::ProjectPartRole::NegativeVolume;
    case ModelPartRole::ParameterModifier:return libslicer::ProjectPartRole::ParameterModifier;
    case ModelPartRole::SupportEnforcer:return libslicer::ProjectPartRole::SupportEnforcer;
    case ModelPartRole::SupportBlocker:return libslicer::ProjectPartRole::SupportBlocker;
    }
    throw std::runtime_error("Unknown model part role");
}
}
struct ModelFileExportHandler::Snapshot {
    struct Part {
        libslicer::ProjectPart data;
        ModelGeometryDB::ReadHandle geometry;
        std::string colors, colorFingerprint, annotationFingerprint;
        int colorTriangleCount{0}, annotationTriangleCount{0};
        bool hasAnnotations{false};
    };
    libslicer::ProjectWriteRequest request;
    std::vector<std::vector<Part>> parts;
    bool individual{false}, saveProject{false};
};
ModelFileExportHandler::ModelFileExportHandler(QObject* parent):StandardBackgroundHandler(parent) {
    connect(&m_worker,&QFutureWatcher<QVariantMap>::finished,this,[this]{requestResumeFromBackground();});
}
ModelFileExportHandler::~ModelFileExportHandler() {
    if(m_cancelled)m_cancelled->store(true);
    if(m_worker.isRunning())m_worker.waitForFinished();
}
std::optional<QVariantMap> ModelFileExportHandler::getAIDescriptor(const QString& code) const {
    auto descriptor=makeDescriptor(code,"Export editable models","Write selected models, a plate, or the complete editable project as STL or 3MF.","write",
        makeInputSchema({{"filePath","string",true},{"format","string",false},{"scope","string",false},
            {"modelIds","array",false},{"individualFiles","boolean",false},{"printBedId","string",false}}),{"file","export","3mf","stl"});
    descriptor.insert("async",true);return descriptor;
}
bool ModelFileExportHandler::onInitialize() {
    try {
        const auto params=getParams();
        auto captured=std::make_shared<Snapshot>();
        captured->saveProject=getActionCode()=="project.save";
        const QString scope=captured->saveProject ? "project" : params.value("scope","selection").toString();
        if(scope!="project" && scope!="plate" && scope!="selection" && scope!="owner")throw std::runtime_error("Unknown export scope");
        const QString format=params.value("format","3mf").toString().toLower();
        if(format!="stl" && format!="3mf")throw std::runtime_error("Export format must be STL or 3MF");
        if(captured->saveProject && format!="3mf")throw std::runtime_error("Editable projects must be saved as 3MF");
        captured->request.format=format=="stl" ? libslicer::ModelFileFormat::Stl : libslicer::ModelFileFormat::ThreeMf;
        captured->individual=params.value("individualFiles").toBool();
        if(captured->individual && format!="stl")throw std::runtime_error("Individual files are supported for STL export only");
        const auto input=params.value("filePath").toString();const QUrl url(input);
        if(!url.scheme().isEmpty() && !url.isLocalFile() && !QFileInfo(input).isAbsolute())throw std::runtime_error("Only local export paths are supported");
        QString path=url.isLocalFile()?url.toLocalFile():input;
        if(path.isEmpty())throw std::runtime_error("An export path is required");
        if(!captured->individual && QFileInfo(path).suffix().isEmpty())path+='.'+format;
        if(!captured->individual && QFileInfo(path).suffix().compare(format,Qt::CaseInsensitive)!=0)throw std::runtime_error("File extension does not match export format");
        captured->request.path=QFileInfo(path).absoluteFilePath().toStdString();
        auto snapshot=GPlatform::SlicingBackend::instance().configSnapshot();
        if(!snapshot)throw std::runtime_error("No active slicing configuration is available");
        captured->request.config=*snapshot;
        if (auto config=DocumentManager::instance()->getDB<GPlatform::SlicingConfigDB>(ModelCreationUtil::activeSlicingConfigId()))
            captured->request.scene.default_filament_slot=config->getDefaultFilamentSlot();
        auto* document=DocumentManager::instance();
        auto ids=params.contains("modelIds") ? params.value("modelIds").toList() : ModelFileController::instance()->exportTargets(scope);
        if(ids.empty())throw std::runtime_error("There are no models to export");
        const auto activeBed=ModelCreationUtil::resolvePrintBed(params.value("printBedId"));
        std::map<DBInstanceID,std::size_t> objects,plates;
        if(scope=="project") {
            // Empty plates are also authored project state.
            auto all=document->getDBInstancesByType(TypeID::PRINT_BED_DB);
            std::sort(all.begin(),all.end(),[](const auto& a,const auto& b){return a->getDBInstanceID().getValue()<b->getDBInstanceID().getValue();});
            for(const auto& db:all)if(auto bed=std::dynamic_pointer_cast<PrintBedDB>(db)) {
                auto plate=format=="3mf"?libslicer::decode_project_plate_semantics(bed->getProjectSemantics()):libslicer::ProjectPlate{};
                plate.id=std::to_string(bed->getDBInstanceID().getValue());plate.name=bed->getDisplayName();
                if(activeBed && activeBed==bed)captured->request.scene.active_plate_index=plates.size();
                plates.emplace(bed->getDBInstanceID(),plates.size());captured->request.scene.plates.push_back(std::move(plate));
            }
        }
        std::set<DBInstanceID> seen;
        for(const auto& value:ids) {
            const DBInstanceID id(value.toULongLong());
            if(!seen.insert(id).second)continue;
            const auto instance=document->getDB<ModelInstanceDB>(id);
            const auto object=instance?instance->object():nullptr;
            const auto bed=instance?document->getDB<PrintBedDB>(instance->getParentPrintBedDBId()):nullptr;
            if(!instance || !object)throw std::runtime_error("An export model is unavailable");
            if(!bed && instance->getParentPrintBedDBId().isValid())throw std::runtime_error("The assigned export print bed is unavailable");
            if(scope=="plate" && (!activeBed || bed!=activeBed))throw std::runtime_error("Export model is not on the requested plate");
            if(bed && !plates.count(bed->getDBInstanceID())) {
                auto plate=format=="3mf"?libslicer::decode_project_plate_semantics(bed->getProjectSemantics()):libslicer::ProjectPlate{};
                plate.id=std::to_string(bed->getDBInstanceID().getValue());plate.name=bed->getDisplayName();
                plates.emplace(bed->getDBInstanceID(),plates.size());captured->request.scene.plates.push_back(std::move(plate));
            }
            auto found=objects.find(object->getDBInstanceID());
            std::size_t oi;
            if(found==objects.end()) {
                oi=captured->request.scene.objects.size();objects.emplace(object->getDBInstanceID(),oi);
                auto source=format=="3mf"?libslicer::decode_project_object_semantics(object->getProjectSemantics()):libslicer::ProjectObject{};
                source.id=std::to_string(object->getDBInstanceID().getValue());source.name=object->getDisplayName();
                source.default_filament_slot=object->getDefaultFilamentSlot();
                std::vector<Snapshot::Part> parts;
                for(const auto& part:object->parts()) {
                    if(format=="stl" && part->getRole()!=int(ModelPartRole::Model) && part->getRole()!=int(ModelPartRole::NegativeVolume))continue;
                    const auto geometry=part->geometry();
                    if(!geometry)throw std::runtime_error("Export part has no geometry");
                    Snapshot::Part out;out.geometry=geometry->read();
                    if(format=="3mf")out.data=libslicer::decode_project_part_semantics(part->getProjectSemantics(),part->getSurfaceAnnotations());
                    out.data.id=std::to_string(part->getDBInstanceID().getValue());out.data.name=part->getDisplayName();
                    out.data.role=projectRole(part->getRole());out.data.local_transform=matrixValues(part->getLocalTransform().getMatrix());
                    out.data.default_filament_slot=part->getDefaultFilamentSlot();
                    out.data.inherit_project_filament=part->getFilamentBindingMode()==int(ModelFilamentBindingMode::InheritProjectDefault);
                    out.data.explicit_filament_binding=part->getFilamentBindingMode()==int(ModelFilamentBindingMode::ExplicitSlot);
                    if(format=="3mf")if(auto colors=part->surfaceColors()) {
                        out.colors=colors->getSurfaceColorData();out.colorFingerprint=colors->getTopologyFingerprint();out.colorTriangleCount=colors->getSourceTriangleCount();
                    }
                    if(format=="3mf" && !part->getSurfaceAnnotations().empty()) {
                        out.hasAnnotations=true;
                        out.annotationFingerprint=part->getAnnotationsTopologyFingerprint();out.annotationTriangleCount=part->getAnnotationsSourceTriangleCount();
                    }
                    parts.push_back(std::move(out));
                }
                if(parts.empty())throw std::runtime_error("Export model has no parts");
                captured->request.scene.objects.push_back(std::move(source));captured->parts.push_back(std::move(parts));
            } else oi=found->second;
            libslicer::ProjectInstance placement;placement.id=std::to_string(id.getValue());placement.name=instance->getDisplayName();
            if(bed)placement.plate_id=std::to_string(bed->getDBInstanceID().getValue());
            placement.transform=matrixValues(bed?PrintBedPlacementUtil::instancePlateMatrix(*instance):instance->getTransformMatrix());
            placement.printable=instance->getPrintable();
            if(format=="3mf") {
                std::vector<std::shared_ptr<FiberMaskDB>> masks;
                for(auto maskId:document->getOwnedChildren(id,FiberMaskDB::kOwnerRelation)) {
                    const auto mask=document->getDB<FiberMaskDB>(maskId);
                    if(!mask)throw std::runtime_error("Export fiber mask is unavailable");
                    masks.push_back(mask);
                }
                std::sort(masks.begin(),masks.end(),[](const auto& a,const auto& b){return FiberMaskDB::precedes(*a,*b);});
                for(const auto& mask:masks) {
                    libslicer::SliceFiberMaskInput input;input.id=std::to_string(mask->getDBInstanceID().getValue());input.name=mask->getDisplayName();
                    input.shape=libslicer::FiberMaskShape(mask->getShape());const auto d=mask->getDimensions();input.dimensions_mm={d.x,d.y,d.z};
                    input.mask_to_object=matrixValues(mask->getTransformMatrix());input.enabled=mask->getEnabled();
                    const auto overrides=QJsonDocument::fromJson(QByteArray::fromStdString(mask->getParameterOverridesJson()));
                    if(!overrides.isObject())throw std::runtime_error("Fiber mask parameters are not a JSON object");
                    const auto settings=overrides.object();
                    for(auto it=settings.begin();it!=settings.end();++it) {
                        if(!it.value().isString())throw std::runtime_error("Fiber mask parameter must be a serialized string");
                        input.settings.emplace_back(it.key().toStdString(),it.value().toString().toStdString());
                    }
                    placement.fiber_masks.push_back(std::move(input));
                }
            }
            captured->request.scene.objects[oi].instances.push_back(std::move(placement));
        }
        m_snapshot=std::move(captured);m_cancelled=std::make_shared<std::atomic_bool>(false);return true;
    } catch(const std::exception& error) {
        m_context->setError(ActionErrorCode::InvalidParams,QString::fromUtf8(error.what()));
        return false;
    }
}
void ModelFileExportHandler::onBackgroundStarted() {
    const auto snapshot=m_snapshot;const auto cancelled=m_cancelled;
    m_worker.setFuture(QtConcurrent::run([snapshot,cancelled] {
        QVariantList files,warnings;
        try {
            auto request=snapshot->request;
            const auto check=[&]{if(cancelled->load())throw std::runtime_error("Export cancelled");};
            for(std::size_t oi=0;oi<snapshot->parts.size();++oi)for(const auto& part:snapshot->parts[oi]) {
                check();std::string error;libslicer::SliceVolumeInput volume;
                if(!ModelMeshSnapshot::appendGeometry(part.geometry,&volume,[&]{return cancelled->load();},&error))throw std::runtime_error(error);
                if(!part.colors.empty() || part.hasAnnotations) {
                    const auto fingerprint=ModelMeshSnapshot::topologyFingerprint(part.geometry,&error);
                    if(!fingerprint)throw std::runtime_error(error);
                    const int triangles=int(volume.triangles.size());
                    if((!part.colors.empty() && (*fingerprint!=part.colorFingerprint || triangles!=part.colorTriangleCount)) ||
                       (part.hasAnnotations && (*fingerprint!=part.annotationFingerprint || triangles!=part.annotationTriangleCount)))
                        throw std::runtime_error("Surface painting or annotations do not match the model topology");
                }
                auto out=part.data;auto mesh=std::make_shared<libslicer::ProjectMesh>();
                mesh->vertices.reserve(volume.vertices.size());mesh->triangles.reserve(volume.triangles.size());
                for(const auto& v:volume.vertices)mesh->vertices.push_back({v.x,v.y,v.z});
                for(const auto& t:volume.triangles)mesh->triangles.push_back({t.vertex_a,t.vertex_b,t.vertex_c});
                out.mesh=std::move(mesh);
                if(!part.colors.empty()) {
                    ManualSupportOrcaScaffold::TriangleSplittingData state;
                    if(!ModelSurfaceColorCodec::decode(part.colors,&state))throw std::runtime_error("Cannot decode model painting");
                    for(const auto& root:state.trianglesToSplit) {
                        if(root.triangleIdx<0 || root.bitstreamStartIdx<0)throw std::runtime_error("Invalid painted facet index");
                        out.material_facets.roots.push_back({std::uint32_t(root.triangleIdx),std::uint32_t(root.bitstreamStartIdx)});
                    }
                    for(bool bit:state.bitstream)out.material_facets.bitstream.push_back(bit?1u:0u);
                }
                request.scene.objects[oi].parts.push_back(std::move(out));
            }
            std::vector<libslicer::ProjectWriteRequest> jobs;
            if(snapshot->individual) {
                const QDir directory(QString::fromStdString(request.path));
                if(!directory.exists())throw std::runtime_error("Export folder does not exist");
                for(const auto& object:request.scene.objects)for(const auto& instance:object.instances) {
                    libslicer::ProjectWriteRequest job;job.config=request.config;job.format=request.format;
                    job.scene.plates=request.scene.plates;job.scene.default_filament_slot=request.scene.default_filament_slot;
                    auto single=object;single.instances={instance};job.scene.objects.push_back(std::move(single));
                    QString name=QString::fromStdString(instance.name.empty()?object.name:instance.name);
                    name.replace(QRegularExpression("[^\\p{L}\\p{N}_. -]"),"_");if(name.isEmpty())name="model";name=name.left(64);
                    job.path=directory.filePath(name+"-"+QString::fromStdString(instance.id)+".stl").toStdString();
                    if(QFileInfo::exists(QString::fromStdString(job.path)))throw std::runtime_error("An individual STL already exists; choose another folder");
                    jobs.push_back(std::move(job));
                }
            } else jobs.push_back(std::move(request));
            libslicer::SliceCallbacks callbacks;callbacks.is_cancelled=[cancelled]{return cancelled->load();};
            for(const auto& job:jobs) {
                check();const auto result=GPlatform::SlicingBackend::instance().writeProject(job,callbacks);
                for(const auto& diagnostic:result.diagnostics)if(diagnostic.severity=="warning")warnings.append(QString::fromStdString(diagnostic.message));
                if(!result)throw std::runtime_error(result.diagnostics.empty()?"Model export failed":result.diagnostics.back().message);
                files.append(QString::fromStdString(result.path));
            }
            return QVariantMap{{"success",true},{"files",files},{"warnings",warnings}};
        } catch(const std::exception& error) {
            return QVariantMap{{"success",false},{"cancelled",cancelled->load()},{"files",files},{"warnings",warnings},{"errorMessage",QString::fromUtf8(error.what())}};
        }
    }));
}
void ModelFileExportHandler::onBackgroundCompleted() {
    m_result=m_worker.result();
    m_context->setResult(m_result);
    if(m_result.value("success").toBool()) {
        if(m_snapshot->saveProject)ModelFileController::instance()->setProjectPath(QString::fromStdString(m_snapshot->request.path));
        if(!m_result.value("warnings").toList().empty())NotificationManager::instance()->showWarning(tr("Export completed with warnings"),m_result.value("warnings").toStringList().join("\n"));
        else NotificationManager::instance()->showSuccess(tr("Export complete"),m_result.value("files").toStringList().join("\n"));
    } else if(!m_result.value("cancelled").toBool()) {
        m_context->setError(ActionErrorCode::Internal,m_result.value("errorMessage").toString());
        NotificationManager::instance()->showWarning(tr("Export models"),m_result.value("errorMessage").toString());
    }
}
