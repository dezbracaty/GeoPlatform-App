#include "ProjectImportHandler.hpp"
#include "InteractionRegistration.hpp"
#include "AIDescriptorHelper.hpp"
#include "ModelCreationUtil.hpp"
#include "EnvironmentSwitchHandler.hpp"
#include <DocumentManager.hpp>
#include <UndoRedoManager.hpp>
#include <TransactionManager.hpp>
#include <ModelGraphUtil.hpp>
#include <ModelGeometryDB.hpp>
#include <ModelObjectDB.hpp>
#include <ModelPartDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelSurfaceColorDB.hpp>
#include <ModelSurfaceColorCodec.hpp>
#include <PrintBedDB.hpp>
#include <PrintBedTemplateDB.hpp>
#include <PrintBedPlacementUtil.hpp>
#include <MaterialDB.hpp>
#include <SlicingConfigDB.hpp>
#include <SliceSettingsBridge.hpp>
#include <SelectionBridge.hpp>
#include <SlicingPreviewBridge.hpp>
#include <CameraNavigationController.hpp>
#include <ActionManager.hpp>
#include <NotificationManager.h>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QUuid>
#include <QUrl>
#include <QScopeGuard>
#include <QtConcurrent/QtConcurrentRun>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkPolyData.h>
#include <algorithm>
#include <map>
#include <cmath>
#include <stdexcept>
#include <condition_variable>
#include <mutex>

REGISTER_INTERACTION_ACTION(ProjectImportHandler, "project.import", "project.open")
REGISTER_INTERACTION_ACTION(ProjectControlHandler, "plate.activate", "project.cancel")

namespace {
struct ProjectRuntime {
    bool busy{false};
    QString activeRequest;
    QPointer<ProjectImportHandler> importer;
};
ProjectRuntime& runtime() { static ProjectRuntime value; return value; }
QString text(const std::string& value) { return QString::fromStdString(value); }
QString fileHash(const QString& path) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read the source file");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input)) throw std::runtime_error("Cannot hash the source file");
    return QString::fromLatin1(hash.result().toHex());
}
// SDK diagnostics remain structured English data. Localize only the UI projection.
QString diagnosticText(const QString& code, const QString& context, const QString& detail = {}) {
    struct Message { const char* code; const char* source; };
    static const Message messages[] = {
        {"project.input", QT_TR_NOOP("The 3MF file cannot be read.")},
        {"project.archive", QT_TR_NOOP("The 3MF archive cannot be opened.")},
        {"project.gcode_job", QT_TR_NOOP("This file is a sliced print job. Open it in G-code preview instead.")},
        {"project.empty", QT_TR_NOOP("This 3MF contains no editable models.")},
        {"project.invalid_relationship", QT_TR_NOOP("The 3MF contains a missing or invalid model reference: %1")},
        {"project.unsupported_extension", QT_TR_NOOP("This 3MF feature cannot be fully restored: %1")},
        {"project.configuration_missing", QT_TR_NOOP("The current configuration is unavailable.")},
        {"project.obsolete_setting", QT_TR_NOOP("Retired project setting: %1")},
        {"project.unknown_setting_ignored", QT_TR_NOOP("Omitted unsupported project setting: %1")},
        {"project.settings_migrated", QT_TR_NOOP("Project settings were converted using Orca compatibility rules.")},
        {"project.geometry_fallback", QT_TR_NOOP("This 3MF can only be imported as model geometry; its project settings were not applied.")},
        {"project.newer_version", QT_TR_NOOP("This 3MF was created with a newer version. Some features may not be fully compatible. Version: %1")},
        {"project.bambu_version", QT_TR_NOOP("This 3MF was created by Bambu Studio. Some settings or features may differ from OrcaSlicer. Version: %1")},
        {"project.preset_substitutions", QT_TR_NOOP("Embedded presets were converted using Orca compatibility rules.")},
        {"project.preset_gcode_warning", QT_TR_NOOP("This 3MF contains modified G-code or customized printer/filament presets. Review them before printing.")},
        {"project.zero_volume_removed", QT_TR_NOOP("Objects with zero volume were removed: %1")},
        {"project.oversized_mesh_scaled", QT_TR_NOOP("An oversized model was scaled down to avoid coordinate overflow: %1")},
        {"project.preview_unavailable", QT_TR_NOOP("The print plate G-code has no drawable preview. Plate: %1")},
        {"project.material_reference", QT_TR_NOOP("The model references an invalid or unavailable filament slot: %1")},
        {"project.empty_volume", QT_TR_NOOP("An empty model volume was skipped: %1")},
        {"project.material_mapping", QT_TR_NOOP("The model material assignments could not be restored.")},
        {"project.unsupported_technology", QT_TR_NOOP("Only FFF printer settings can be opened as a project.")},
        {"project.decision_required", QT_TR_NOOP("Confirm the import options to continue.")},
        {"project.legacy_infill_rotation_fixed", QT_TR_NOOP("The incompatible legacy infill rotation setting was cleared.")},
        {"project.legacy_prime_tower_migrated", QT_TR_NOOP("The legacy prime tower settings were converted.")},
        {"project.machine_setting_inherited", QT_TR_NOOP("Restored a missing printer setting: %1")},
        {"project.legacy_plate_layout_migrated", QT_TR_NOOP("The legacy print plate layout was converted.")},
        {"project.geometry_settings_cleared", QT_TR_NOOP("Imported model geometry and material assignments; source process settings were not applied.")},
        {"project.invalid_configuration", QT_TR_NOOP("The project contains an invalid setting: %1")},
        {"project.read_failed", QT_TR_NOOP("The 3MF could not be read. Check that the file is complete and supported.")},
        {"project.prepare_failed", QT_TR_NOOP("The 3MF could not be prepared. The current project was kept.")},
        {"project.cancelled", QT_TR_NOOP("Project import cancelled.")},
        {"project.invalid_intent", QT_TR_NOOP("Choose a valid import mode.")},
        {"project.commit_failed", QT_TR_NOOP("The 3MF could not be added. The current project was kept.")},
        {"project.recovery_failed", QT_TR_NOOP("The import failed and the previous configuration could not be restored. Reopen the project before continuing.")},
        {"project.publication_warning", QT_TR_NOOP("The project was loaded, but some views could not be refreshed.")}
    };
    for (const auto& message : messages) {
        if (code == QLatin1String(message.code)) {
            auto result = QCoreApplication::translate("ProjectImportHandler", message.source);
            if (result.contains("%1")) return result.arg(context);
            if (code == QLatin1String("project.preset_gcode_warning") && !context.isEmpty())
                result += "\n" + context;
            return result;
        }
    }
    auto result = detail.isEmpty()
        ? QCoreApplication::translate("ProjectImportHandler", "3MF diagnostic: %1").arg(code)
        : detail;
    if (!context.isEmpty()) result += "\n" + context;
    return result;
}
QJsonArray diagnostics(const std::vector<libslicer::ProjectDiagnostic>& source) {
    QJsonArray result;
    for (const auto& item : source) result.append(QJsonObject{{"code", text(item.code)},
        {"severity", text(item.severity)}, {"context", text(item.context)}, {"message", text(item.message)}});
    return result;
}
template<class T> std::vector<std::shared_ptr<T>> objects(TypeID type) {
    std::vector<std::shared_ptr<T>> result;
    for (const auto& db : DocumentManager::instance()->getDBInstancesByType(type))
        if (auto value = std::dynamic_pointer_cast<T>(db)) result.push_back(value);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a->getDBInstanceID().getValue() < b->getDBInstanceID().getValue();
    });
    return result;
}
std::shared_ptr<GPlatform::SlicingConfigDB> documentConfig() {
    const auto configs = objects<GPlatform::SlicingConfigDB>(TypeID::SLICING_CONFIG_DB);
    if (configs.empty()) throw std::runtime_error("No project slicing configuration is available");
    return configs.front();
}
Transform transform(const std::array<double, 16>& input) {
    Transform result;
    Transform::Matrix4 matrix;
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
        matrix(row, column) = static_cast<float>(input[row * 4 + column]);
    if (!matrix.allFinite()) throw std::runtime_error("A project transform is not finite");
    result.setMatrix(matrix);
    return result;
}
struct PreparedPart {
    ModelGraphUtil::PartCreateInfo info;
    std::string semantics, annotations, fingerprint, colors;
};
PreparedPart preparePart(const libslicer::ProjectPart& source, int order) {
    if (!source.mesh) throw std::runtime_error("Project part has no indexed mesh");
    PreparedPart output;
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToFloat();
    for (const auto& p : source.mesh->vertices) points->InsertNextPoint(p.x, p.y, p.z);
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    std::vector<GeomTriangle> triangles;
    triangles.reserve(source.mesh->triangles.size());
    for (const auto& face : source.mesh->triangles) {
        if (face.vertex_a >= source.mesh->vertices.size() || face.vertex_b >= source.mesh->vertices.size() || face.vertex_c >= source.mesh->vertices.size())
            throw std::runtime_error("Project part contains an invalid vertex index");
        const vtkIdType ids[3]{face.vertex_a, face.vertex_b, face.vertex_c};
        cells->InsertNextCell(3, ids);
        const auto& a = source.mesh->vertices[face.vertex_a];
        const auto& b = source.mesh->vertices[face.vertex_b];
        const auto& c = source.mesh->vertices[face.vertex_c];
        triangles.emplace_back(Vector3(a.x,a.y,a.z), Vector3(b.x,b.y,b.z), Vector3(c.x,c.y,c.z));
    }
    auto geometry = vtkSmartPointer<vtkPolyData>::New();
    geometry->SetPoints(points); geometry->SetPolys(cells);
    std::string error;
    if (!ModelGeometryDB::validatePolyData(geometry, &error)) throw std::runtime_error(error);
    output.info.name = source.name; output.info.geometry = geometry;
    output.info.localTransform = transform(source.local_transform);
    output.info.orderIndex = order;
    output.info.defaultFilamentSlot = std::max(1, source.default_filament_slot);
    output.info.filamentBindingMode = source.explicit_filament_binding
        ? ModelFilamentBindingMode::ExplicitSlot : ModelFilamentBindingMode::InheritObject;
    switch (source.role) {
        case libslicer::ProjectPartRole::Model: output.info.role = ModelPartRole::Model; break;
        case libslicer::ProjectPartRole::NegativeVolume: output.info.role = ModelPartRole::NegativeVolume; break;
        case libslicer::ProjectPartRole::ParameterModifier: output.info.role = ModelPartRole::ParameterModifier; break;
        case libslicer::ProjectPartRole::SupportEnforcer: output.info.role = ModelPartRole::SupportEnforcer; break;
        case libslicer::ProjectPartRole::SupportBlocker: output.info.role = ModelPartRole::SupportBlocker; break;
        default: throw std::runtime_error("Unsupported project part role");
    }
    output.semantics = libslicer::encode_project_part_semantics(source);
    output.annotations = libslicer::encode_project_part_annotations(source);
    output.fingerprint = ModelSurfaceColorCodec::topologyFingerprint(triangles);
    if (!source.material_facets.empty()) {
        ManualSupportOrcaScaffold::TriangleSplittingData state;
        for (const auto& root : source.material_facets.roots)
            state.trianglesToSplit.push_back({int(root.triangle_index), int(root.bitstream_start_index)});
        for (auto bit : source.material_facets.bitstream) state.bitstream.push_back(bit != 0);
        state.update_used_states();
        if (!ModelSurfaceColorCodec::encode(state, &output.colors)) throw std::runtime_error("Unable to encode imported material painting");
    }
    return output;
}
std::shared_ptr<PrintBedTemplateDB> projectTemplate(const libslicer::ProjectMachine& machine) {
    auto value = trans::TransDB::create<PrintBedTemplateDB>();
    const auto& variant = machine.variant;
    value->setModelId(machine.model.id); value->setVariantId(variant.id);
    value->setVendor(machine.model.vendor_id); value->setPrinterModel(machine.model.name);
    value->setWidth(float(variant.printable_width)); value->setHeight(float(variant.printable_depth));
    value->setPrintHeight(float(variant.printable_height)); value->setThickness(0.08f);
    value->setBedModelPath(machine.model.bed_model_path); value->setBedTexturePath(machine.model.bed_texture_path);
    Polygon2 area;
    float minX = 0, minY = 0;
    if (!variant.printable_area.empty()) {
        minX = float(variant.printable_area.front().x); minY = float(variant.printable_area.front().y);
        for (const auto& p : variant.printable_area) {
            area.emplace_back(float(p.x), float(p.y));
            minX = std::min(minX,float(p.x)); minY = std::min(minY,float(p.y));
        }
    }
    value->setPrintableArea(area); value->setOrigin(Vector3(minX,minY,0));
    return value;
}
}

struct ProjectImportHandler::DecisionWait {
    std::mutex mutex;
    std::condition_variable ready;
    std::optional<libslicer::ProjectDecisionAnswer> answer;
};

ProjectImportHandler::ProjectImportHandler(QObject* parent) : StandardBackgroundHandler(parent) {
    connect(&m_watcher, &QFutureWatcher<libslicer::ProjectReadResult>::finished, this, &ProjectImportHandler::workerFinished);
}
ProjectImportHandler::~ProjectImportHandler() {
    cancelPendingRead();
    closeDecision();
    if (m_watcher.isRunning()) m_watcher.waitForFinished();
}
void ProjectImportHandler::cancelPendingRead() {
    if (m_decisionWait) {
        std::lock_guard lock(m_decisionWait->mutex);
        if (m_cancelled) m_cancelled->store(true);
        m_decisionWait->ready.notify_all();
    } else if (m_cancelled) m_cancelled->store(true);
}
void ProjectImportHandler::cancelImport() {
    if (m_terminal) return;
    cancelPendingRead();
    closeDecision();
    if (!m_watcher.isRunning()) {
        finish("cancelled","project.cancelled",tr("Project import cancelled."));
        if (getState()==State::Background) requestResumeFromBackground();
    }
}
bool ProjectImportHandler::onKeyPressEvent(QKeyEvent* event) {
    if (event && event->key()==Qt::Key_Escape && !m_terminal) { cancelImport(); return true; }
    return false;
}
const QHash<QString, QVariantMap>& ProjectImportHandler::aiDescriptorTable() const {
    static const auto result = [] {
        QHash<QString,QVariantMap> table;
        for (const auto* action : {"project.import", "project.open"}) {
            auto descriptor = makeDescriptor(action,"Import 3MF","Read a 3MF and append its models or open its project.","write",
                makeInputSchema({{"filePath","string",true},{"intent","string",false},{"printBedId","number",false},
                    {"confirmReplace","boolean",false},
                    {"requestId","string",false},{"acceptLegacyInfillRotationFix","boolean",false}}), {"3mf","project","import"});
            descriptor.insert("async",true); table.insert(action,descriptor);
        }
        return table;
    }(); return result;
}
std::optional<QVariantMap> ProjectImportHandler::getAIDescriptor(const QString& action) const {
    const auto& table = aiDescriptorTable();
    const auto found = table.constFind(action);
    return found==table.cend() ? std::nullopt : std::optional<QVariantMap>{*found};
}
bool ProjectImportHandler::onInitialize() {
    if (runtime().busy || m_watcher.isRunning()) {
        m_context->setError(ActionErrorCode::ContractValidation,tr("A project import is already in progress.")); return false;
    }
    const QUrl url = getParam("filePath").toUrl();
    m_path = url.isLocalFile() ? url.toLocalFile() : getParam("filePath").toString();
    const QFileInfo info(m_path);
    if (!info.isFile() || !info.isReadable()) {
        m_context->setError(ActionErrorCode::TargetNotFound,tr("The project file is not readable.")); return false;
    }
    m_parameters = getParams();
    m_intent = m_parameters.value("intent").toString();
    // project.open remains an explicit API shortcut; interactive UI uses project.import.
    if (m_intent.isEmpty() && getActionCode() == "project.open") m_intent = "project";
    if (!m_intent.isEmpty() && m_intent != "project" && m_intent != "geometry") {
        m_context->setError(ActionErrorCode::InvalidParams,tr("intent must be geometry or project")); return false;
    }
    const auto active = GPlatform::SlicingBackend::instance().activeConfigView();
    if (!active) { m_context->setError(ActionErrorCode::SystemUnavailable,tr("No active slicing configuration")); return false; }
    m_requestId = m_parameters.value("requestId").toString();
    if (m_requestId.isEmpty()) m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_revision = GPlatform::SlicingBackend::instance().configurationRevision();
    m_sourceHash = fileHash(m_path);
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    m_decisionWait = std::make_shared<DecisionWait>();
    m_terminal = false; m_pending = true;
    m_candidate = {}; m_completion.clear();
    runtime().busy = true; runtime().activeRequest = m_requestId; runtime().importer = this;

    return true;
}
void ProjectImportHandler::onBackgroundStarted() {
    if (m_intent.isEmpty()) {
        if (!showDecision("mode")) {
            finish("rejected","project.decision_required",tr("Choose whether to open this 3MF as a project or import its models."));
            // ActionManager installs its suspended slot after onSuspend returns.
            QTimer::singleShot(0,this,[this] { requestResumeFromBackground(); });
        }
    } else startRead();
}
void ProjectImportHandler::startRead() {
    closeDecision();
    libslicer::ProjectReadRequest request;
    request.path = m_path.toStdString();
    request.mode = m_intent == "project" ? libslicer::ProjectReadMode::Project : libslicer::ProjectReadMode::Geometry;
    request.accept_legacy_infill_rotation_fix = m_parameters.value("acceptLegacyInfillRotationFix").toBool();
    const auto cancelled = m_cancelled;
    const auto path = m_path;
    const auto hash = m_sourceHash;
    const auto wait = m_decisionWait;
    const auto parameters = m_parameters;
    m_watcher.setFuture(QtConcurrent::run([this,request,cancelled,path,hash,wait,parameters] {
        if (fileHash(path) != hash) throw std::runtime_error("Source file changed during project preparation");
        libslicer::SliceCallbacks callbacks; callbacks.is_cancelled = [cancelled] { return cancelled->load(); };
        const auto decisions = [this,cancelled,wait,parameters](const libslicer::ProjectDecision& decision) {
            using Kind = libslicer::ProjectDecisionKind;
            using Answer = libslicer::ProjectDecisionAnswer;
            if (decision.kind == Kind::LegacyInfillRotation && parameters.contains("acceptLegacyInfillRotationFix"))
                return parameters.value("acceptLegacyInfillRotationFix").toBool() ? Answer::Yes : Answer::No;
            std::unique_lock lock(wait->mutex);
            if (cancelled->load()) return Answer::Cancel;
            wait->answer.reset();
            // The SDK invokes this on its read thread. Only the modal decision
            // enters the GUI thread; it must not call back into the busy Backend.
            if (!QMetaObject::invokeMethod(this,[this,cancelled,wait,kind=decision.kind] {
                if (cancelled->load()) return;
                QString question;
                switch (kind) {
                    case Kind::LegacyInfillRotation: question = tr("Clear the legacy OrcaSlicer 2.3.1-alpha infill rotation template?"); break;
                    case Kind::ConvertMeters: question = tr("The object may be in meters. Scale to millimetres?"); break;
                    case Kind::ConvertInches: question = tr("The object may be in inches. Scale to millimetres?"); break;
                    case Kind::MultipartObject: question = tr("Load objects positioned at multiple heights as one object with multiple parts?"); break;
                    case Kind::ScaleOversizedInstance: question = tr("Scale the oversized object down to fit the print bed?"); break;
                }
                if (!showDecision("native",question)) {
                    std::lock_guard answerLock(wait->mutex);
                    wait->answer = Answer::Cancel;
                    wait->ready.notify_all();
                }
            },Qt::QueuedConnection)) return Answer::Cancel;
            wait->ready.wait(lock,[&] { return cancelled->load() || wait->answer.has_value(); });
            return cancelled->load() ? Answer::Cancel : *wait->answer;
        };
        auto result = GPlatform::SlicingBackend::instance().readProject(request,callbacks,decisions);
        if (fileHash(path) != hash) throw std::runtime_error("Source file changed while reading the project");
        return result;
    }));
}
void ProjectImportHandler::workerFinished() {
    if (m_terminal || getState() == State::Inactive) return;
    try { m_candidate = m_watcher.result(); }
    catch (const std::exception& e) { finish("failed","project.prepare_failed",QString::fromUtf8(e.what())); }
    catch (...) { finish("failed","project.prepare_failed",tr("Project preparation failed.")); }
    if (getState() == State::Background) requestResumeFromBackground();
    else if (!m_terminal) consumeCandidate();
}
void ProjectImportHandler::onBackgroundCompleted() { m_resuming = true; if (!m_terminal) consumeCandidate(); m_resuming = false; }
void ProjectImportHandler::consumeCandidate() {
    if (m_cancelled->load() || m_candidate.cancelled) { finish("cancelled","project.cancelled",tr("Project import cancelled.")); return; }
    if (!m_candidate || !m_candidate.scene) {
        QString message = tr("The 3MF could not be prepared.");
        if (!m_candidate.diagnostics.empty()) message = text(m_candidate.diagnostics.front().message);
        finish("failed",{},message); return;
    }
    if (m_candidate.scene->objects.empty()) { finish("rejected","project.empty",tr("This 3MF contains no editable models.")); return; }
    const bool replacesModels = m_candidate.replace_document &&
        (!objects<ModelObjectDB>(TypeID::MODEL_OBJECT_DB).empty() ||
         !DocumentManager::instance()->getDBInstancesByType(TypeID::GLB_DB).empty());
    if (replacesModels && !m_parameters.value("confirmReplace").toBool()) {
        if (!showDecision("replace")) finish("rejected","project.decision_required",tr("Confirm replacing the current project."));
        return;
    }
    commit();
}
void ProjectImportHandler::commit() {
    if (m_terminal) return;
    auto* document = DocumentManager::instance();
    auto& backend = GPlatform::SlicingBackend::instance();
    const bool opening = m_candidate.replace_document;
    const bool configurationChanged = m_candidate.configuration_changed;
    try {
        if (m_cancelled->load()) throw std::runtime_error("Project request was cancelled");
        if (TransactionManager::instance().isInTransaction()) throw std::runtime_error("A document edit is still in progress");
        const auto config = documentConfig();
        if (backend.configurationRevision() != m_revision) throw std::runtime_error("Slicing configuration changed; prepare the project again");
        std::optional<SliceSettingsBridge::PreparedConfiguration> preparedView;
        std::vector<GPlatform::SlicingConfigDB::Filament> filaments;
        if (configurationChanged) {
            if (!m_candidate.configuration.valid()) throw std::runtime_error("The project has no complete configuration");
            preparedView = SliceSettingsBridge::prepareConfiguration(m_candidate.configuration.view(),m_candidate.configuration.machine());
            filaments = SliceSettingsBridge::documentFilaments(m_candidate.configuration.view().filament_slots);
        }
        bool projectPalette = m_candidate.load_config;
        if (configurationChanged && !projectPalette) {
            const auto active = backend.configSnapshot();
            const auto imported = m_candidate.configuration.snapshot();
            for (const auto* key : {"filament_colour", "filament_multi_colour", "filament_colour_type"}) {
                if (!active || active->value(key) != imported.value(key)) {
                    projectPalette = true;
                    break;
                }
            }
        }
        std::vector<std::vector<PreparedPart>> preparedParts;
        std::vector<std::string> objectSemantics, plateSemantics;
        for (const auto& object : m_candidate.scene->objects) {
            std::vector<PreparedPart> parts;
            for (std::size_t index = 0; index < object.parts.size(); ++index) parts.push_back(preparePart(object.parts[index],int(index)));
            preparedParts.push_back(std::move(parts));
            objectSemantics.push_back(libslicer::encode_project_object_semantics(object));
        }
        for (const auto& plate : m_candidate.scene->plates) plateSemantics.push_back(libslicer::encode_project_plate_semantics(plate));
        auto currentBed = ModelCreationUtil::resolvePrintBed(m_parameters.value("printBedId"));
        if (!opening && !currentBed) throw std::runtime_error("The target print bed is unavailable");
        std::map<std::string,std::shared_ptr<PrintBedDB>> targets;
        if (!opening) for (const auto& plate : m_candidate.scene->plates)
            targets.emplace(plate.id, currentBed);
        const auto oldObjects = objects<ModelObjectDB>(TypeID::MODEL_OBJECT_DB);
        const auto oldBeds = objects<PrintBedDB>(TypeID::PRINT_BED_DB);
        const auto oldTemplates = objects<PrintBedTemplateDB>(TypeID::PRINT_BED_TEMPLATE_DB);
        const auto oldGlbs = document->getDBInstancesByType(TypeID::GLB_DB);
        QVariantList created;
        created.reserve(qsizetype(m_candidate.scene->objects.size()));
        // The normal document queue contains raw pointers. Keep the replaced
        // graph alive through commit/rollback and the corresponding queue flush.
        auto retainedObjects = document->getDBInstancesByType(TypeID::UNKNOWN);
        document->flushPendingChanges();
        // Factories may clean up a newly created graph before returning an
        // error. Retain those objects too, until rollback drains raw pointers.
        const auto retention = document->addChangeListener([&](const auto& change) {
            if (change.changeType == ChangeType::OBJECT_CREATED)
                if (auto object = document->getDBInstance(change.id)) retainedObjects.push_back(std::move(object));
        });
        const auto stopRetention = qScopeGuard([&] { document->removeChangeListener(retention); });
        TransactionGuard transaction(opening ? "Open 3MF Project" : "Import 3MF Models");
        bool durable = false, installed = false;
        libslicer::ConfigurationState previous;
        std::uint64_t installedRevision = m_revision;
        try {
            if (!TransactionManager::instance().isInTransaction())
                throw std::runtime_error("Unable to begin the project document transaction");
            auto definition = opening ? projectTemplate(m_candidate.configuration.machine()) : currentBed->getTemplateDB();
            float nextX = 0;
            std::shared_ptr<PrintBedDB> firstBed;
            for (std::size_t index = 0; index < m_candidate.scene->plates.size(); ++index) {
                const auto& source = m_candidate.scene->plates[index];
                auto bed = opening ? std::shared_ptr<PrintBedDB>{} : targets.at(source.id);
                if (!bed) {
                    bed = trans::TransDB::create<PrintBedDB>();
                    bed->setTemplateDBId(definition->getDBInstanceID());
                    bed->setPosition(Vector3(nextX,0,0)); nextX += definition->getWidth() * 1.2f;
                    bed->setDisplayName(source.name);
                    bed->setProjectSemantics(plateSemantics[index]);
                    if (auto material = bed->getMaterial()) material->setOpacity(1.0f);
                }
                targets[source.id] = bed;
                if (!firstBed) firstBed = bed;
            }
            if (opening && !firstBed) throw std::runtime_error("The project contains no usable print plate");
            for (std::size_t objectIndex = 0; objectIndex < m_candidate.scene->objects.size(); ++objectIndex) {
                const auto& source = m_candidate.scene->objects[objectIndex];
                auto& prepared = preparedParts[objectIndex];
                std::vector<ModelGraphUtil::PartCreateInfo> infos;
                for (auto& part : prepared) infos.push_back(std::move(part.info));
                auto graph = ModelGraphUtil::createObject({source.name,m_path.toStdString(),"3mf",true},std::move(infos));
                if (!graph || graph.parts.size() != source.parts.size()) throw std::runtime_error(graph.error.empty() ? "Cannot construct project model" : graph.error);
                graph.object->setDefaultFilamentSlot(source.default_filament_slot);
                graph.object->setProjectSemantics(objectSemantics[objectIndex]);
                for (std::size_t partIndex = 0; partIndex < graph.parts.size(); ++partIndex) {
                    auto& part = graph.parts[partIndex];
                    const auto& data = prepared[partIndex];
                    part->setProjectSemantics(data.semantics); part->setSurfaceAnnotations(data.annotations);
                    part->setAnnotationsTopologyFingerprint(data.fingerprint);
                    part->setAnnotationsSourceTriangleCount(int(source.parts[partIndex].mesh->triangles.size()));
                    if (part->getRole()!=static_cast<int>(ModelPartRole::Model))
                        if (const auto material = part->getMaterial()) material->setOpacity(0.35f);
                    if (!data.colors.empty()) {
                        auto colors = trans::TransDB::create<ModelSurfaceColorDB>();
                        if (!document->attachOwnedChild(part->getDBInstanceID(),colors->getDBInstanceID(),ModelPartDB::kSurfaceColorsRelation))
                            throw std::runtime_error("Cannot attach project material painting");
                        colors->setSurfaceColorData(data.colors); colors->setTopologyFingerprint(data.fingerprint);
                        colors->setSourceTriangleCount(int(source.parts[partIndex].mesh->triangles.size()));
                        colors->setDataVersion(int(ModelSurfaceColorCodec::DataVersion)); colors->setRevision(1);
                    }
                }
                for (const auto& instance : source.instances) {
                    auto placement = transform(instance.transform);
                    std::shared_ptr<PrintBedDB> bed;
                    if (!instance.plate_id.empty()) {
                        const auto target = targets.find(instance.plate_id);
                        if (target == targets.end()) throw std::runtime_error("Project instance references an unknown source plate");
                        bed = target->second;
                        auto matrix = placement.getMatrix();
                        const auto offset = PrintBedPlacementUtil::sceneOffset(*bed);
                        matrix(0,3) += offset.x; matrix(1,3) += offset.y; matrix(2,3) += offset.z;
                        placement.setMatrix(matrix);
                    }
                    std::string error;
                    auto output = ModelGraphUtil::createInstance(graph.object->getDBInstanceID(),
                        {placement,bed ? bed->getDBInstanceID() : DBInstanceID{},instance.printable,int(created.size()),config->getDBInstanceID()},&error);
                    if (!output) throw std::runtime_error(error.empty() ? "Cannot construct project instance" : error);
                    output->setDisplayName(instance.name.empty() ? source.name : instance.name);
                    created.push_back(QString::number(output->getDBInstanceID().getValue()));
                }
            }
            if (opening) {
                config->setDefaultFilamentSlot(1);
                config->setActivePrintBedDBId(firstBed->getDBInstanceID());
                for (const auto& old : oldObjects)
                    if (!document->unregisterDBInstance(old->getDBInstanceID())) throw std::runtime_error("Cannot remove the previous project graph");
                for (const auto& old : oldGlbs)
                    if (!document->unregisterDBInstance(old->getDBInstanceID())) throw std::runtime_error("Cannot remove the previous project graph");
                for (const auto& old : oldBeds)
                    if (!document->unregisterDBInstance(old->getDBInstanceID())) throw std::runtime_error("Cannot remove the previous project graph");
                for (const auto& old : oldTemplates)
                    if (!document->unregisterDBInstance(old->getDBInstanceID())) throw std::runtime_error("Cannot remove the previous project graph");
            }
            if (configurationChanged) {
                // Only configuration CAS/install holds the backend mutex. DB
                // setters and their synchronous observers always run unlocked.
                auto lease = backend.beginProjectCommit(m_revision);
                if (!lease) throw std::runtime_error("Slicing is busy or the configuration changed");
                auto applied = lease.install(m_candidate.configuration);
                if (!applied) throw std::runtime_error(applied.diagnostics.empty() ? "Cannot activate project configuration" : applied.diagnostics.front().message);
                previous = std::move(applied.previous); installedRevision = applied.revision; installed = true;
                lease.release();
                // The palette is a backend configuration projection, as for
                // normal material changes; model undo must not desynchronize it.
                TransientUpdateGuard projection;
                config->setFilaments(filaments);
                config->setActiveRevision(installedRevision);
            } else {
                // Geometry append must also reject a stale configuration.
                auto lease = backend.beginProjectCommit(m_revision);
                if (!lease) throw std::runtime_error("Slicing is busy or the configuration changed");
            }
            // Preserve the existing synchronous transaction notifications.
            auto& history = TransactionManager::instance();
            const auto previousCommitId = history.getUndoTransactionSummary().id;
            QString publicationWarning;
            try { transaction.commit(); }
            catch (...) {
                // TransDB may already have appended the transaction before an
                // observer throws. An installed history entry cannot be rolled back.
                if (history.getUndoTransactionSummary().id == previousCommitId) throw;
                durable = true;
                try { throw; }
                catch (const std::exception& error) { publicationWarning = QString::fromUtf8(error.what()); }
                catch (...) { publicationWarning = tr("Project publication failed."); }
                transaction.commit(); // Finish the moved-from current transaction.
            }
            durable = true;
            m_completion.insert("objectIds",created);
            if (!created.empty()) { m_completion.insert("objectId",created.front()); m_completion.insert("modelId",created.front()); }
            if (configurationChanged)
                SliceSettingsBridge::instance()->installCommittedConfiguration(
                    std::move(*preparedView), projectPalette ? std::optional<bool>(true) : std::nullopt);
            if (opening) document->getUndoRedoManager()->clear();
            if (opening) {
                SelectionBridge::instance()->clearSelection();
                SlicingPreviewBridge::instance()->clearAllDBReferences();
            }
            document->flushPendingChanges();
            if (configurationChanged) SliceSettingsBridge::instance()->publishCommittedConfiguration();
            else SliceSettingsBridge::instance()->markNeedsReslice();
            // Publish the terminal outcome only after the editor transition and
            // camera update, so their failures retain a publication diagnostic.
            EnvironmentSwitchHandler::switchEnvironment("normal");
            if (ActionManager::getInstance()->currentEnvironment() != QStringLiteral("normal"))
                throw std::runtime_error(tr("Project publication failed.").toStdString());
            if (auto camera = CameraNavigationController::currentCamera()) CameraNavigationController::fitScene(camera);
            finish("succeeded",publicationWarning.isEmpty() ? QString{} : "project.publication_warning",publicationWarning);
        } catch (...) {
            QString failure = tr("Project publication failed.");
            try { throw; } catch (const std::exception& error) { failure = QString::fromUtf8(error.what()); } catch (...) {}
            if (durable) {
                // Refresh failures cannot undo an already installed transaction.
                // Limit tolerance to this import boundary, never ordinary observers.
                try { document->flushPendingChanges(); }
                catch (const std::exception& error) { failure += "\n" + QString::fromUtf8(error.what()); }
                catch (...) { failure += "\n" + tr("Project publication failed."); }
                finish("succeeded","project.publication_warning",failure);
            } else {
                // Rollback can unregister every newly created child before the
                // queued property notifications are drained.
                std::uint64_t restoredRevision = m_revision;
                bool restoredContent = true;
                if (installed) {
                    try {
                        auto lease = backend.beginProjectCommit(installedRevision);
                        if (!lease) throw std::runtime_error("Cannot restore the previous configuration");
                        const auto restored = lease.install(previous);
                        restoredContent = bool(restored);
                        if (restored) restoredRevision = restored.revision;
                    } catch (...) { restoredContent = false; }
                }
                transaction.rollback();
                if (installed && restoredContent) {
                    TransientUpdateGuard derived;
                    config->setFilaments(SliceSettingsBridge::documentFilaments(previous.view().filament_slots));
                    config->setActiveRevision(restoredRevision);
                }
                document->rebuildRelationsFromReferenceFields();
                document->flushPendingChanges();
                finish("failed",restoredContent ? "project.commit_failed" : "project.recovery_failed",failure);
            }
        }
    } catch (const std::exception& e) { finish("failed","project.commit_failed",QString::fromUtf8(e.what())); }
}

bool ProjectImportHandler::showDecision(const QString& stage, const QString& question) {
    closeDecision(); m_dialogStage = stage;
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) return false;
    QQuickWindow* window = nullptr;
    for (auto* candidate : QGuiApplication::allWindows()) {
        auto* quick = qobject_cast<QQuickWindow*>(candidate);
        if (quick && quick->isVisible() && qmlEngine(quick)) { window = quick; if (quick->isActive()) break; }
    }
    if (!window) return false;
    QString warning;
    for (const auto& item : m_candidate.diagnostics) {
        warning += diagnosticText(text(item.code),text(item.context),text(item.message));
        warning += "\n";
    }
    QQmlComponent component(qmlEngine(window),QUrl("qrc:/qt/qml/GPlatform/qml/dialogs/ThreeMfImportDialog.qml"));
    auto* dialog = component.createWithInitialProperties({{"hostItem",QVariant::fromValue(window->contentItem())},
        {"stage",stage},{"fileName",QFileInfo(m_path).fileName()},
        {"questionText",question},{"warningText",warning}});
    if (!dialog) { LOG_ERROR("3MF dialog failed: {}",component.errorString().toStdString()); return false; }
    dialog->setParent(this); QQmlEngine::setObjectOwnership(dialog,QQmlEngine::CppOwnership); m_dialog = dialog;
    connect(dialog,SIGNAL(decision(QVariant)),this,SLOT(resolveDecision(QVariant)));
    connect(window,&QObject::destroyed,dialog,[this] { resolveDecision(QVariantMap{{"action","cancel"}}); });
    QMetaObject::invokeMethod(dialog,"open"); return true;
}
void ProjectImportHandler::closeDecision() {
    if (auto* dialog = m_dialog.data()) {
        m_dialog.clear(); disconnect(dialog,nullptr,this,nullptr);
        QMetaObject::invokeMethod(dialog,"close"); dialog->deleteLater();
    }
}
void ProjectImportHandler::resolveDecision(QVariant decision) {
    if (m_terminal || !m_dialog) return;
    const auto values = decision.toMap(); const auto action = values.value("action").toString();
    const auto stage = m_dialogStage; closeDecision();
    if (stage == "native") {
        if (action == "cancel") cancelImport();
        else {
            std::lock_guard lock(m_decisionWait->mutex);
            m_decisionWait->answer = action == "yes"
                ? libslicer::ProjectDecisionAnswer::Yes : libslicer::ProjectDecisionAnswer::No;
            m_decisionWait->ready.notify_all();
        }
        return;
    }
    if (action == "cancel") { m_cancelled->store(true); finish("cancelled","project.cancelled",tr("Project import cancelled.")); }
    else if (stage == "mode") {
        if (action != "geometry" && action != "project") { finish("rejected","project.invalid_intent",tr("Choose a valid import mode.")); }
        else { m_intent = action; startRead(); return; }
    } else if (stage == "replace") {
        m_parameters.insert("confirmReplace",true); consumeCandidate();
    }
    if (getState() == State::Background) requestResumeFromBackground();
}
void ProjectImportHandler::finish(QString status,QString code,QString message) {
    if (m_terminal) return;
    m_terminal = true; m_pending = false;
    auto report = diagnostics(m_candidate.diagnostics);
    if (!code.isEmpty()) report.append(QJsonObject{{"code",code},{"severity",status=="succeeded" ? "warning" : "error"},
        {"context",m_path},{"message",message}});
    const QString observedIntent = m_intent.isEmpty() ? "undecided" : m_intent;
    QJsonArray ignoredSettings;
    if (status=="succeeded") for (const auto& key : m_candidate.ignored_settings) ignoredSettings.append(text(key));
    const QString compatibilityStatus = status!="succeeded" ? "not_applied" : ignoredSettings.isEmpty() ? "complete" : "partial";
    runtime().busy = false; runtime().activeRequest.clear();
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        QStringList messages;
        for (const auto& entry : report) {
            const auto diagnostic = entry.toObject();
            if (diagnostic.value("severity").toString() != "info") {
                const auto localized = diagnosticText(diagnostic.value("code").toString(),
                    diagnostic.value("context").toString(),diagnostic.value("message").toString());
                if (!messages.contains(localized)) messages.push_back(localized);
            }
        }
        if (compatibilityStatus=="partial") {
            messages.prepend(tr("%1 unsupported settings were omitted. Review the project before printing.").arg(ignoredSettings.size()));
        }
        if (!messages.isEmpty()) NotificationManager::instance()->showWarning(
            compatibilityStatus=="partial" ? tr("3MF opened with unsupported settings omitted")
                : status == "succeeded" ? tr("3MF opened with diagnostics") : tr("3MF import"), messages.join("\n"));
    }
    m_completion.insert("success",status=="succeeded"); m_completion.insert("committed",status=="succeeded");
    m_completion.insert("cancelled",status=="cancelled"); m_completion.insert("status",status);
    m_completion.insert("requestId",m_requestId); m_completion.insert("intent",observedIntent);
    m_completion.insert("compatibilityStatus",compatibilityStatus); m_completion.insert("ignoredSettings",ignoredSettings.toVariantList());
    m_completion.insert("configurationDisposition", status=="succeeded" && m_candidate.load_config ? "project" :
        status=="succeeded" && m_candidate.configuration_changed ? "palette" : "preserved");
    m_completion.insert("diagnostics",report.toVariantList()); m_completion.insert("errorMessage",message);
    if (m_context) m_context->setResult(m_completion);
    if (getState() == State::Active && !m_resuming) {
        completeInvocation(m_context,m_completion);
        setActive(false);
    }
}
void ProjectImportHandler::onCleanup() {
    closeDecision();
    cancelPendingRead();
    if (runtime().busy && runtime().activeRequest == m_requestId) finish("cancelled","project.cancelled",tr("Project import cancelled."));
    m_candidate = {};
}

const QHash<QString,QVariantMap>& ProjectControlHandler::aiDescriptorTable() const {
    static const QHash<QString,QVariantMap> descriptors{
        {"project.cancel",makeDescriptor("project.cancel","Cancel Project Import","Cancel the current project preparation or decision.","write",makeInputSchema({}),{"project","cancel"})},
        {"plate.activate",makeDescriptor("plate.activate","Activate Print Plate","Select an existing project print plate.","write",
            makeInputSchema({{"printBedId","number",true}}),{"plate","select"})}};
    return descriptors;
}
void ProjectControlHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context); setActive(false);
    try {
        if (context->getActionCode()=="project.cancel") {
            if (runtime().importer && runtime().busy) runtime().importer->cancelImport();
            context->setResult(QVariantMap{{"cancelRequested",true}}); return;
        }
        if (runtime().busy) throw std::runtime_error("Project import has not reached its terminal published state");
        if (context->getActionCode()=="plate.activate") {
            const auto params = context->getParams();
            const auto id = params.value("printBedId").toULongLong();
            if (!DocumentManager::instance()->getDB<PrintBedDB>(DBInstanceID(id))) throw std::runtime_error("The requested print plate does not exist");
            TransientUpdateGuard derived;
            documentConfig()->setActivePrintBedDBId(DBInstanceID(id));
            context->setResult(QVariantMap{{"printBedId",QString::number(id)}}); return;
        }
    } catch (const std::exception& error) { context->setError(ActionErrorCode::ContractValidation,QString::fromUtf8(error.what())); }
}
