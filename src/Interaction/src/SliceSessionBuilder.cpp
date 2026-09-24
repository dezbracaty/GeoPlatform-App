#include "SliceSessionBuilder.hpp"

#include "ModelSurfaceColorCodec.hpp"
#include "ModelSurfaceColorDB.hpp"
#include "SlicingBackend.hpp"
#include "SliceVertexWelder.hpp"

#include <DocumentManager.hpp>
#include <Geometry.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelGeometryDB.hpp>
#include "ModelFilamentSlotResolver.hpp"
#include <ModelPartDB.hpp>
#include <SlicingConfigDB.hpp>
#include <vtkCell.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <QtGlobal>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

class SliceSessionBuilder::CapturedRequest final {
public:
    struct SurfaceColors {
        std::string data;
        std::string topologyFingerprint;
        int sourceTriangleCount{0};
        int revision{0};
    };

    struct Volume {
        ModelGeometryDB::ReadHandle geometry;
        std::array<double, 16> localTransform{};
        std::uint64_t meshRevision{0};
        int defaultFilamentSlot{1};
        libslicer::SliceVolumeRole role{
            libslicer::SliceVolumeRole::ModelPart};
        std::optional<SurfaceColors> surfaceColors;
    };

    struct Model {
        std::string name;
        std::array<double, 16> transform{};
        std::uint64_t modelId{0};
        std::uint64_t meshRevision{0};
        std::vector<Volume> volumes;
        std::vector<std::string> supportEnforcerPaths;
    };

    GPlatform::SlicingProfileSnapshot profile;
    std::vector<Model> models;
    bool generateGcode3mf{true};
    bool beltSupportDebugOnly{false};
    std::string beltSupportDebugOutputPath;
};

namespace {

std::atomic_uint64_t g_sessionSequence{0};

std::shared_ptr<ModelSurfaceColorDB> surfaceColorsFor(
    DocumentManager& document, const DBInstanceID& partId) {
    const auto part = document.getDB<ModelPartDB>(partId);
    return part ? part->surfaceColors() : nullptr;
}

bool cancelled(const std::function<bool()>& isCancelled) {
    return isCancelled && isCancelled();
}

std::optional<std::string> localTopologyFingerprint(
    const ModelGeometryDB::ReadHandle& geometry, std::string* error) {
    vtkPolyData* polyData = geometry
        ? const_cast<vtkPolyData*>(&geometry.polyData())
        : nullptr;
    if (!polyData || !polyData->GetPoints() ||
        polyData->GetNumberOfCells() <= 0) {
        if (error) *error = "Model has no valid topology for surface colors";
        return std::nullopt;
    }

    std::vector<GeomTriangle> triangles;
    triangles.reserve(static_cast<std::size_t>(polyData->GetNumberOfCells()));
    for (vtkIdType cellId = 0; cellId < polyData->GetNumberOfCells(); ++cellId) {
        vtkCell* cell = polyData->GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() != 3) {
            if (error) *error = "Surface color topology contains a non-triangle cell";
            return std::nullopt;
        }
        double raw[3][3];
        for (int corner = 0; corner < 3; ++corner) {
            polyData->GetPoint(cell->GetPointId(corner), raw[corner]);
        }
        triangles.emplace_back(
            Vector3(static_cast<float>(raw[0][0]),
                    static_cast<float>(raw[0][1]),
                    static_cast<float>(raw[0][2])),
            Vector3(static_cast<float>(raw[1][0]),
                    static_cast<float>(raw[1][1]),
                    static_cast<float>(raw[1][2])),
            Vector3(static_cast<float>(raw[2][0]),
                    static_cast<float>(raw[2][1]),
                    static_cast<float>(raw[2][2])));
    }
    return ModelSurfaceColorCodec::topologyFingerprint(triangles);
}

bool appendGeometry(const SliceSessionBuilder::CapturedRequest::Volume& volume,
                    libslicer::SliceVolumeInput* output,
                    const std::function<bool()>& isCancelled,
                    std::string* error) {
    if (!output || !error) return false;
    vtkPolyData* polyData = volume.geometry
        ? const_cast<vtkPolyData*>(&volume.geometry.polyData())
        : nullptr;
    if (!polyData || !polyData->GetPoints() ||
        polyData->GetNumberOfPoints() <= 0 || polyData->GetNumberOfCells() <= 0) {
        *error = "Model has no valid indexed mesh snapshot";
        return false;
    }
    if (static_cast<unsigned long long>(polyData->GetNumberOfPoints()) >
        std::numeric_limits<std::uint32_t>::max()) {
        *error = "Model exceeds the supported vertex index range";
        return false;
    }

    const vtkIdType pointCount = polyData->GetNumberOfPoints();
    std::vector<std::uint32_t> sourcePointToVertex(
        static_cast<std::size_t>(pointCount));
    GPlatform::SliceVertexWelder weldedVertices(
        static_cast<std::size_t>(pointCount));
    Eigen::Matrix4d local = Eigen::Matrix4d::Identity();
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            local(row, column) = volume.localTransform[
                static_cast<std::size_t>(row * 4 + column)];
        }
    }

    // Imported triangle data may store three independent VTK points per face.
    // The libslicer in-memory API accepts an indexed TriangleMesh and
    // therefore requires shared positions to reference shared vertex indices.
    // Weld exact float positions here while preserving cell order, so facet
    // painting remains indexed by the original triangle order.
    for (vtkIdType pointId = 0; pointId < pointCount; ++pointId) {
        if ((pointId & 0x3fffu) == 0 && cancelled(isCancelled)) {
            *error = "Slice session preparation was cancelled";
            return false;
        }
        double point[3];
        polyData->GetPoint(pointId, point);
        const Eigen::Vector4d transformed = local * Eigen::Vector4d(
            point[0], point[1], point[2], 1.0);
        const libslicer::SliceVertex vertex{
            static_cast<float>(transformed.x()),
            static_cast<float>(transformed.y()),
            static_cast<float>(transformed.z())};
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z)) {
            *error = "Slice input contains a non-finite vertex";
            return false;
        }
        sourcePointToVertex[static_cast<std::size_t>(pointId)] =
            weldedVertices.indexFor(vertex);
    }
    output->vertices = weldedVertices.takeVertices();
    output->triangles.reserve(static_cast<std::size_t>(polyData->GetNumberOfCells()));
    for (vtkIdType cellId = 0; cellId < polyData->GetNumberOfCells(); ++cellId) {
        if ((cellId & 0x3fffu) == 0 && cancelled(isCancelled)) {
            *error = "Slice session preparation was cancelled";
            return false;
        }
        vtkCell* cell = polyData->GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() != 3) {
            *error = "Slice input contains a non-triangle cell";
            return false;
        }
        const vtkIdType pointA = cell->GetPointId(0);
        const vtkIdType pointB = cell->GetPointId(1);
        const vtkIdType pointC = cell->GetPointId(2);
        if (pointA < 0 || pointB < 0 || pointC < 0 ||
            pointA >= pointCount || pointB >= pointCount || pointC >= pointCount) {
            *error = "Slice input contains an invalid triangle index";
            return false;
        }
        output->triangles.push_back({
            sourcePointToVertex[static_cast<std::size_t>(pointA)],
            sourcePointToVertex[static_cast<std::size_t>(pointB)],
            sourcePointToVertex[static_cast<std::size_t>(pointC)]});
    }
    return true;
}

} // namespace

SliceSessionBuilder::CaptureResult SliceSessionBuilder::capture(const Request& request) {
    CaptureResult result;
    if (request.modelIds.empty()) {
        result.error = "Slice session has no model";
        return result;
    }

    auto profile = GPlatform::SlicingBackend::instance().activeProfileSnapshot();
    if (!profile || !profile->valid()) {
        result.error = "No active slicing profile is available";
        return result;
    }
    const std::size_t filamentCount = profile->selection.filament_preset_ids.size();
    if (filamentCount == 0) {
        result.error = "The active slicing profile has no filament slots";
        return result;
    }
    auto* document = DocumentManager::instance();
    if (!document) {
        result.error = "DocumentManager is not available";
        return result;
    }

    auto captured = std::make_shared<CapturedRequest>();
    captured->profile = std::move(*profile);
    captured->generateGcode3mf = request.generateGcode3mf;
    captured->beltSupportDebugOnly =
        qEnvironmentVariableIntValue("GPLATFORM_BELT_SUPPORT_AUDIT") != 0;
    captured->beltSupportDebugOutputPath =
        qEnvironmentVariable("GPLATFORM_BELT_SUPPORT_AUDIT_PATH").toStdString();
    captured->models.reserve(request.modelIds.size());

    bool supportTargetFound = request.supportEnforcerPaths.empty();
    for (const auto& modelId : request.modelIds) {
        const auto db = document->getDBInstance(modelId);
        const auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(db);
        if (!instance || !instance->isValid() || !instance->getPrintable()) {
            result.error = "Slice session contains an invalid model";
            return result;
        }
        const auto slicingConfig =
            document->getDB<GPlatform::SlicingConfigDB>(
                instance->getSlicingConfigDBId());

        CapturedRequest::Model model;
        model.name = db->getDisplayName();
        model.modelId = modelId.getValue();
        const auto matrix = instance->getTransform().getMatrix();
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                model.transform[static_cast<std::size_t>(row * 4 + column)] =
                    static_cast<double>(matrix(row, column));
            }
        }

        const auto appendVolume = [&](ModelGeometryDB* geometry,
                                      const Transform& localTransform,
                                      int defaultSlot,
                                      libslicer::SliceVolumeRole role,
                                      std::optional<CapturedRequest::SurfaceColors> colors)
            -> bool {
            auto readHandle = geometry
                ? geometry->read()
                : ModelGeometryDB::ReadHandle{};
            auto* source = readHandle
                ? const_cast<vtkPolyData*>(&readHandle.polyData())
                : nullptr;
            if (!source || !source->GetPoints() ||
                source->GetNumberOfPoints() <= 0 ||
                source->GetNumberOfCells() <= 0 || defaultSlot <= 0 ||
                static_cast<std::size_t>(defaultSlot) > filamentCount) {
                return false;
            }
            CapturedRequest::Volume volume;
            volume.geometry = std::move(readHandle);
            const auto& local = localTransform.getMatrix();
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                    volume.localTransform[static_cast<std::size_t>(
                        row * 4 + column)] = local(row, column);
                }
            }
            volume.meshRevision = volume.geometry.revision();
            volume.defaultFilamentSlot = defaultSlot;
            volume.role = role;
            volume.surfaceColors = std::move(colors);
            model.meshRevision = std::max(
                model.meshRevision, volume.meshRevision);
            model.volumes.push_back(std::move(volume));
            return true;
        };

        const auto instanceParts = instance->parts();
        for (std::size_t partIndex = 0; partIndex < instanceParts.size();
             ++partIndex) {
            const auto& part = instanceParts[partIndex];
            if (!part || !part->hasValidMesh()) continue;
            const auto role = static_cast<ModelPartRole>(part->getRole());
            const auto sliceRole = role == ModelPartRole::SupportEnforcer
                ? libslicer::SliceVolumeRole::SupportEnforcer
                : role == ModelPartRole::SupportBlocker
                    ? libslicer::SliceVolumeRole::SupportBlocker
                    : libslicer::SliceVolumeRole::ModelPart;
            std::optional<CapturedRequest::SurfaceColors> colors;
            if (auto state = surfaceColorsFor(
                    *document, part->getDBInstanceID())) {
                colors = CapturedRequest::SurfaceColors{
                    state->getSurfaceColorData(),
                    state->getTopologyFingerprint(),
                    state->getSourceTriangleCount(), state->getRevision()};
            }
            const auto filament = GPlatform::Slicing::resolveModelFilamentSlot(
                *part, slicingConfig.get(), filamentCount);
            if (!appendVolume(part->geometry().get(), part->getLocalTransform(),
                              filament.effectiveSlot, sliceRole,
                              std::move(colors))) {
                result.error = "Model part has no valid indexed mesh snapshot";
                return result;
            }
        }
        if (model.volumes.empty()) {
            result.error = "Model instance has no printable part";
            return result;
        }
        if (modelId == request.supportTargetModelId) {
            model.supportEnforcerPaths = request.supportEnforcerPaths;
            supportTargetFound = true;
        }
        captured->models.push_back(std::move(model));
    }

    if (!supportTargetFound) {
        result.error = "Manual support target is not part of the slice session";
        return result;
    }
    result.request = std::move(captured);
    return result;
}

SliceSessionBuilder::Result SliceSessionBuilder::build(
    const CapturedRequest& captured,
    const std::function<bool()>& isCancelled) {
    Result result;
    if (!captured.profile.valid() || captured.models.empty()) {
        result.error = "Captured slice request is incomplete";
        return result;
    }
    const std::size_t filamentCount =
        captured.profile.selection.filament_preset_ids.size();
    if (filamentCount == 0) {
        result.error = "The active slicing profile has no filament slots";
        return result;
    }

    auto session = std::make_shared<GPlatform::SliceSession>();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    session->id = "slice-" + std::to_string(milliseconds) + "-" +
        std::to_string(++g_sessionSequence);
    session->profileRevision = captured.profile.revision;
    session->request.config = captured.profile.config;
    session->request.generate_gcode_3mf = captured.generateGcode3mf;
    session->request.belt_support_debug_only = captured.beltSupportDebugOnly;
    session->request.belt_support_debug_output_path =
        captured.beltSupportDebugOutputPath;
    if (session->request.belt_support_debug_only)
        session->request.generate_gcode_3mf = false;
    session->request.center_on_build_plate = false;
    session->request.objects.reserve(captured.models.size());
    session->sources.reserve(captured.models.size());

    for (const auto& model : captured.models) {
        if (cancelled(isCancelled)) {
            result.error = "Slice session preparation was cancelled";
            return result;
        }
        libslicer::SliceObjectInput object;
        object.name = model.name;
        object.transform = model.transform;
        object.support_enforcer_paths = model.supportEnforcerPaths;

        int surfaceRevision = 0;
        for (const auto& capturedVolume : model.volumes) {
            if (capturedVolume.surfaceColors) {
                const auto& colors = *capturedVolume.surfaceColors;
                const auto localFingerprint = localTopologyFingerprint(
                    capturedVolume.geometry, &result.error);
                if (!localFingerprint ||
                    colors.sourceTriangleCount != static_cast<int>(
                        const_cast<vtkPolyData&>(
                            capturedVolume.geometry.polyData())
                            .GetNumberOfCells()) ||
                    colors.topologyFingerprint != *localFingerprint) {
                    if (result.error.empty()) {
                        result.error =
                            "Surface colors do not match the model topology";
                    }
                    return result;
                }
            }
            libslicer::SliceVolumeInput volume;
            volume.default_filament_slot = capturedVolume.defaultFilamentSlot;
            volume.role = capturedVolume.role;
            if (!appendGeometry(capturedVolume, &volume, isCancelled,
                                &result.error)) {
                return result;
            }
            if (capturedVolume.surfaceColors) {
                const auto& colors = *capturedVolume.surfaceColors;
                ManualSupportOrcaScaffold::TriangleSplittingData state;
                if (!ModelSurfaceColorCodec::decode(colors.data, &state)) {
                    result.error = "Unable to decode model surface colors";
                    return result;
                }
                for (std::size_t label = filamentCount + 1;
                     label < state.usedStates.size(); ++label) {
                    if (state.usedStates[label]) {
                        result.error = "Surface color label references a missing filament slot";
                        return result;
                    }
                }
                volume.facet_labels.roots.reserve(
                    state.trianglesToSplit.size());
                for (const auto& root : state.trianglesToSplit) {
                    if (root.triangleIdx < 0 || root.bitstreamStartIdx < 0 ||
                        static_cast<std::size_t>(root.triangleIdx) >=
                            volume.triangles.size() ||
                        static_cast<std::size_t>(root.bitstreamStartIdx) >=
                            state.bitstream.size()) {
                        result.error = "Surface color state references invalid topology";
                        return result;
                    }
                    volume.facet_labels.roots.push_back({
                        static_cast<std::uint32_t>(root.triangleIdx),
                        static_cast<std::uint32_t>(root.bitstreamStartIdx)});
                }
                volume.facet_labels.bitstream.reserve(state.bitstream.size());
                for (const bool bit : state.bitstream) {
                    volume.facet_labels.bitstream.push_back(bit ? 1u : 0u);
                }
                surfaceRevision = colors.revision;
            }
            object.volumes.push_back(std::move(volume));
        }
        session->request.objects.push_back(std::move(object));
        session->sources.push_back({
            model.modelId,
            model.meshRevision,
            surfaceRevision});
    }

    if (!session->valid()) {
        result.error = "Constructed slice session is incomplete";
        return result;
    }
    result.session = std::move(session);
    return result;
}

SliceSessionBuilder::Result SliceSessionBuilder::build(const Request& request) {
    auto captured = capture(request);
    if (!captured) {
        return {nullptr, std::move(captured.error)};
    }
    return build(*captured.request);
}
