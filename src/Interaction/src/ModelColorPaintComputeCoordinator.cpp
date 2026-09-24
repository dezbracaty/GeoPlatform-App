#include "ModelColorPaintComputeCoordinator.hpp"

#include "DocumentManager.hpp"
#include "Foundation/Log.h"
#include "ModelInstanceDB.hpp"
#include "ModelGeometryDB.hpp"
#include "ModelGraphUtil.hpp"
#include "ModelPartDB.hpp"
#include "ModelSurfaceColorCodec.hpp"
#include "ModelSurfaceColorDB.hpp"
#include "TransactionManager.hpp"
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <cmath>
#include <set>
#include <transdb.h>

struct ModelColorPaintWorkerState {
    struct Session {
        std::uint64_t meshRevision{0};
        int revision{-1};
        std::string topologyFingerprint;
        std::shared_ptr<const OrcaIndexedMesh> topology;
        ManualSupportOrcaScaffold engine;
    };

    std::unordered_map<std::uint64_t, std::unique_ptr<Session>> sessions;
};

namespace {

bool paintStatesEqual(const ManualSupportOrcaScaffold::TriangleSplittingData& lhs,
                      const ManualSupportOrcaScaffold::TriangleSplittingData& rhs) {
    if (lhs.bitstream != rhs.bitstream || lhs.leafLabels != rhs.leafLabels ||
        lhs.trianglesToSplit.size() != rhs.trianglesToSplit.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.trianglesToSplit.size(); ++i) {
        if (lhs.trianglesToSplit[i].triangleIdx != rhs.trianglesToSplit[i].triangleIdx ||
            lhs.trianglesToSplit[i].bitstreamStartIdx !=
                rhs.trianglesToSplit[i].bitstreamStartIdx) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<ModelSurfaceColorDB> stateForPart(DocumentManager& document,
                                                  const DBInstanceID& partId) {
    const auto part = document.getDB<ModelPartDB>(partId);
    return part ? part->surfaceColors() : nullptr;
}

std::shared_ptr<ModelColorStrokeResult> computeStroke(
    const ModelColorStrokeRequest& request,
    ModelColorPaintWorkerState& workerState) {
    auto result = std::make_shared<ModelColorStrokeResult>();
    result->token = request.token;
    result->modelId = request.modelId;
    result->partId = request.partId;
    result->meshRevision = request.meshRevision;
    result->baseRevision = request.baseRevision;
    QElapsedTimer timer;
    timer.start();

    if (!request.topology || request.samples.empty()) {
        result->error = QStringLiteral("Missing stroke topology or samples");
        result->computeMs = timer.elapsed();
        return result;
    }

    const auto modelValue = request.modelId.getValue();
    auto& session = workerState.sessions[modelValue];
    const bool canReuse = session &&
        session->meshRevision == request.meshRevision &&
        session->revision == request.baseRevision &&
        session->topologyFingerprint == request.topologyFingerprint &&
        session->topology == request.topology;
    result->reusedWorkerSession = canReuse;
    if (!canReuse) {
        auto replacement = std::make_unique<ModelColorPaintWorkerState::Session>();
        replacement->meshRevision = request.meshRevision;
        replacement->revision = request.baseRevision;
        replacement->topologyFingerprint = request.topologyFingerprint;
        replacement->topology = request.topology;
        replacement->engine.load_mesh(request.topology->vertices,
                                      request.topology->triangles,
                                      request.topology->neighbors,
                                      request.topology->faceNormals);
        if (!replacement->engine.has_mesh()) {
            result->error = QStringLiteral("Unable to initialize surface-color worker mesh");
            result->computeMs = timer.elapsed();
            return result;
        }
        replacement->engine.restore_state_snapshot(request.baseState);
        session = std::move(replacement);
    }
    auto& engine = session->engine;
    engine.set_cancellation_check([cancellation = request.cancellation] {
        return cancellation && cancellation->load(std::memory_order_relaxed);
    });
    engine.set_triangle_splitting_enabled(true);

    bool consumed = false;
    for (const auto& sample : request.samples) {
        if (request.cancellation &&
            request.cancellation->load(std::memory_order_relaxed)) {
            workerState.sessions.erase(modelValue);
            result->error = QStringLiteral("Stroke computation cancelled");
            result->computeMs = timer.elapsed();
            return result;
        }
        if (sample.tool == ModelColorPaintTool::GapFill) {
            consumed |= engine.fill_small_gaps(sample.gapArea);
            continue;
        }
        if (sample.facetId < 0) continue;
        // Each captured point is an independent projected stamp. Resetting only
        // pointer-session state avoids interpolation through missing ray hits;
        // formal geometry remains accumulated in the worker engine.
        engine.reset_session();
        const auto paintState = sample.erase
            ? ManualSupportOrcaScaffold::NoLabel : request.labelId;
        if (sample.tool == ModelColorPaintTool::Triangle) {
            consumed |= engine.paint_triangle(
                sample.facetId, sample.paintHit, paintState);
        } else if (sample.tool == ModelColorPaintTool::Fill) {
            consumed |= engine.paint_fill(
                sample.facetId, sample.paintHit, paintState,
                sample.smartFillAngleDeg, sample.edgeDetection);
        } else if (sample.tool == ModelColorPaintTool::HeightRange) {
            consumed |= engine.paint_height_range(
                sample.paintHit.z(), sample.heightRange, paintState, true);
        } else {
            if (sample.radiusWorld <= 0.0f) continue;
            engine.set_cursor_type(sample.cursorType);
            engine.set_cursor_radius(sample.radiusWorld);
            engine.set_active_paint_state(paintState);
            engine.set_screen_projection(sample.screenProjection);
            engine.set_projection_seed_facets(sample.projectionSeedFacets);
            engine.update_external_hit({true, sample.screenPosition, 0, sample.facetId,
                                        sample.paintHit, sample.cameraPaintPosition});
            consumed |= engine.on_mouse_left_down({sample.screenPosition,
                                                   sample.erase, false, false});
        }
    }
    const qint64 paintMs = timer.elapsed();
    if (request.cancellation &&
        request.cancellation->load(std::memory_order_relaxed)) {
        workerState.sessions.erase(modelValue);
        result->error = QStringLiteral("Stroke computation cancelled");
        result->computeMs = timer.elapsed();
        return result;
    }
    if (consumed) {
        const auto& last = request.samples.back();
        engine.on_mouse_left_up({last.screenPosition, last.erase, false, false});
        result->state = engine.serialized_cache();
    } else {
        result->state = request.baseState;
    }
    const qint64 stateMs = timer.elapsed() - paintMs;
    result->changed = !paintStatesEqual(request.baseState, result->state);
    const auto allLeaves = engine.collect_all_leaf_triangles();
    const qint64 collectMs = timer.elapsed() - paintMs - stateMs;

    std::set<ManualSupportOrcaScaffold::FacetLabelId> labels;
    for (const auto& leaf : allLeaves) {
        if (leaf.state == ManualSupportOrcaScaffold::NoLabel) continue;
        ++result->paintedFacetCount;
        labels.insert(leaf.state);
        result->paintedSurfaceArea += 0.5 * QVector3D::crossProduct(
            leaf.vertices[1] - leaf.vertices[0],
            leaf.vertices[2] - leaf.vertices[0]).length();
    }
    result->usedColorCount = static_cast<int>(labels.size());
    const qint64 metricsMs = timer.elapsed() - paintMs - stateMs - collectMs;
    if (result->changed) {
        auto projection = std::make_shared<SurfaceColorRenderProjectionResult>(
            packSurfaceColorRenderProjection(
                allLeaves, request.topology->sourceFacetToIndexedFacet,
                request.localFromPaint));
        if (!projection->valid) {
            workerState.sessions.erase(modelValue);
            result->error = QString::fromStdString(projection->error);
            result->computeMs = timer.elapsed();
            return result;
        }
        result->renderProjection = std::move(projection);
    }
    const qint64 projectionMs =
        timer.elapsed() - paintMs - stateMs - collectMs - metricsMs;
    if (result->changed &&
        !ModelSurfaceColorCodec::encode(result->state, &result->encodedState)) {
        workerState.sessions.erase(modelValue);
        result->error = QStringLiteral("Unable to encode computed surface colors");
        result->computeMs = timer.elapsed();
        return result;
    }
    result->computeMs = timer.elapsed();
    const qint64 encodeMs = result->computeMs - paintMs - stateMs - collectMs -
        metricsMs - projectionMs;
    LOG_INFO(
        "ModelColorPaintComputeCoordinator: phases model={} token={} paintMs={} stateMs={} collectMs={} metricsMs={} projectionMs={} encodeMs={} leaves={} triangleStorage={}/{} invalidTriangles={} vertexStorage={} freeVertices={}",
        request.modelId.getValue(), request.token, paintMs, stateMs, collectMs,
        metricsMs, projectionMs, encodeMs, allLeaves.size(),
        engine.triangle_storage_size(), engine.triangle_storage_capacity(),
        engine.invalid_triangle_count(), engine.vertex_storage_size(),
        engine.free_vertex_count());
    result->success = true;
    session->revision = request.baseRevision + (result->changed ? 1 : 0);
    return result;
}

std::shared_ptr<ModelColorGapPreviewResult> computeGapPreview(
    const ModelColorGapPreviewRequest& request) {
    auto result = std::make_shared<ModelColorGapPreviewResult>();
    result->token = request.token;
    result->modelId = request.modelId;
    result->partId = request.partId;
    QElapsedTimer timer;
    timer.start();
    if (!request.topology || request.gapArea <= 0.0f) {
        result->success = true;
        result->computeMs = timer.elapsed();
        return result;
    }

    ManualSupportOrcaScaffold engine;
    engine.load_mesh(request.topology->vertices,
                     request.topology->triangles,
                     request.topology->neighbors,
                     request.topology->faceNormals);
    if (!engine.has_mesh()) {
        result->error = QStringLiteral("Unable to initialize gap-fill preview mesh");
        result->computeMs = timer.elapsed();
        return result;
    }
    engine.restore_state_snapshot(request.baseState);
    result->candidates = engine.collect_gap_fill_preview(request.gapArea);
    result->computeMs = timer.elapsed();
    result->success = true;
    return result;
}

} // namespace

ModelColorPaintComputeCoordinator* ModelColorPaintComputeCoordinator::instance() {
    static auto* coordinator = new ModelColorPaintComputeCoordinator();
    return coordinator;
}

ModelColorPaintComputeCoordinator::ModelColorPaintComputeCoordinator(QObject* parent)
    : QObject(parent),
      m_workerContext(new QObject()),
      m_workerState(std::make_unique<ModelColorPaintWorkerState>()) {
    m_workerContext->moveToThread(&m_workerThread);
    m_workerThread.setObjectName(QStringLiteral("ModelColorPaintWorker"));
    m_workerThread.start();
}

ModelColorPaintComputeCoordinator::~ModelColorPaintComputeCoordinator() {
    if (m_workerContext) {
        QMetaObject::invokeMethod(m_workerContext, "deleteLater", Qt::QueuedConnection);
        m_workerContext = nullptr;
    }
    m_workerThread.quit();
    m_workerThread.wait();
}

std::uint64_t ModelColorPaintComputeCoordinator::submit(ModelColorStrokeRequest request) {
    if (!request.modelId.isValid() || !request.topology || request.samples.empty()) return 0;
    const auto modelValue = request.modelId.getValue();
    if (m_pendingTokens.count(modelValue)) return 0;
    request.token = m_nextToken++;
    request.generation = m_generations[modelValue];
    request.cancellation = std::make_shared<std::atomic_bool>(false);
    m_pendingTokens[modelValue] = request.token;
    m_activeCancellations[modelValue] = request.cancellation;
    auto sharedRequest = std::make_shared<ModelColorStrokeRequest>(std::move(request));
    QPointer<ModelColorPaintComputeCoordinator> that(this);
    auto* workerState = m_workerState.get();
    QMetaObject::invokeMethod(m_workerContext, [that, sharedRequest, workerState] {
        auto result = computeStroke(*sharedRequest, *workerState);
        if (!that) return;
        QMetaObject::invokeMethod(that, [that, sharedRequest, result] {
            if (that) that->finishOnGuiThread(sharedRequest, result);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return sharedRequest->token;
}

std::uint64_t ModelColorPaintComputeCoordinator::submitGapPreview(
    ModelColorGapPreviewRequest request) {
    if (!request.modelId.isValid() || !request.topology) return 0;
    const auto modelValue = request.modelId.getValue();
    request.token = m_nextToken++;
    request.generation = ++m_gapPreviewGenerations[modelValue];
    m_latestGapPreviewTokens[modelValue] = request.token;
    auto sharedRequest = std::make_shared<ModelColorGapPreviewRequest>(std::move(request));
    QPointer<ModelColorPaintComputeCoordinator> that(this);
    QMetaObject::invokeMethod(m_workerContext, [that, sharedRequest] {
        auto result = computeGapPreview(*sharedRequest);
        if (!that) return;
        QMetaObject::invokeMethod(that, [that, sharedRequest, result] {
            if (!that) return;
            const auto modelValue = sharedRequest->modelId.getValue();
            const auto latest = that->m_latestGapPreviewTokens.find(modelValue);
            if (latest == that->m_latestGapPreviewTokens.end() ||
                latest->second != sharedRequest->token ||
                that->m_gapPreviewGenerations[modelValue] != sharedRequest->generation) {
                return;
            }
            that->m_latestGapPreviewTokens.erase(latest);
            emit that->gapPreviewFinished(result);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return sharedRequest->token;
}

void ModelColorPaintComputeCoordinator::invalidateGapPreview(
    const DBInstanceID& modelId) {
    if (!modelId.isValid()) return;
    const auto modelValue = modelId.getValue();
    ++m_gapPreviewGenerations[modelValue];
    m_latestGapPreviewTokens.erase(modelValue);
}

void ModelColorPaintComputeCoordinator::invalidateModel(const DBInstanceID& modelId) {
    if (!modelId.isValid()) return;
    const auto modelValue = modelId.getValue();
    ++m_generations[modelValue];
    const auto cancellation = m_activeCancellations.find(modelValue);
    if (cancellation != m_activeCancellations.end()) {
        cancellation->second->store(true, std::memory_order_relaxed);
        m_activeCancellations.erase(cancellation);
    }
    m_pendingTokens.erase(modelValue);
    auto* workerState = m_workerState.get();
    QMetaObject::invokeMethod(m_workerContext, [workerState, modelValue] {
        workerState->sessions.erase(modelValue);
    }, Qt::QueuedConnection);
    invalidateGapPreview(modelId);
}

bool ModelColorPaintComputeCoordinator::hasPending(const DBInstanceID& modelId) const {
    return modelId.isValid() && m_pendingTokens.count(modelId.getValue()) > 0;
}

void ModelColorPaintComputeCoordinator::finishOnGuiThread(
    std::shared_ptr<ModelColorStrokeRequest> request,
    std::shared_ptr<ModelColorStrokeResult> result) {
    QElapsedTimer guiTimer;
    guiTimer.start();
    const auto modelValue = request->modelId.getValue();
    const auto pending = m_pendingTokens.find(modelValue);
    if (pending != m_pendingTokens.end() && pending->second == request->token) {
        m_pendingTokens.erase(pending);
    }
    const auto cancellation = m_activeCancellations.find(modelValue);
    if (cancellation != m_activeCancellations.end() &&
        cancellation->second == request->cancellation) {
        m_activeCancellations.erase(cancellation);
    }

    auto fail = [&](const QString& error) {
        result->success = false;
        result->error = error;
        auto* workerState = m_workerState.get();
        QMetaObject::invokeMethod(m_workerContext, [workerState, modelValue] {
            workerState->sessions.erase(modelValue);
        }, Qt::QueuedConnection);
        emit strokeFinished(result);
    };
    if (m_generations[modelValue] != request->generation) {
        fail(QStringLiteral("Stroke result invalidated by a newer model state"));
        return;
    }
    if (!result->success) {
        emit strokeFinished(result);
        return;
    }

    auto* document = DocumentManager::instance();
    auto mesh = document
        ? document->getDB<ModelInstanceDB>(request->modelId)
        : nullptr;
    auto part = document
        ? document->getDB<ModelPartDB>(request->partId)
        : nullptr;
    if (!document || !mesh || !part ||
        part->object() != mesh->object() ||
        !part->geometry() ||
        part->geometry()->revision() != request->meshRevision) {
        fail(QStringLiteral("Model geometry changed while applying the stroke"));
        return;
    }
    auto state = stateForPart(*document, request->partId);
    const int currentRevision = state ? state->getRevision() : 0;
    if (currentRevision != request->baseRevision) {
        fail(QStringLiteral("Surface-color revision changed while applying the stroke"));
        return;
    }

    if (result->changed) {
        {
            TransactionGuard guard("Model Surface Color Stroke");
            const auto unique =
                ModelGraphUtil::ensureUniqueObjectForInstance(request->modelId);
            const DBInstanceID targetPartId = unique.mapPart(request->partId);
            if (!unique || !targetPartId.isValid()) {
                guard.rollback();
                fail(QString::fromStdString(
                    unique.error.empty()
                        ? "Unable to resolve the editable model Part"
                        : unique.error));
                return;
            }
            result->partId = targetPartId;
            state = stateForPart(*document, targetPartId);
            if (!state) state = trans::TransDB::create<ModelSurfaceColorDB>();
            if (!state) {
                guard.rollback();
                fail(QStringLiteral("Unable to create model surface-color data"));
                return;
            }
            const auto targetPart = document->getDB<ModelPartDB>(targetPartId);
            if (targetPart && targetPart->surfaceColors() != state &&
                !document->attachOwnedChild(
                    targetPartId, state->getDBInstanceID(),
                    ModelPartDB::kSurfaceColorsRelation)) {
                guard.rollback();
                fail(QStringLiteral("Unable to attach model surface-color data"));
                return;
            }
            if (!targetPart || targetPart->surfaceColors() != state) {
                guard.rollback();
                fail(QStringLiteral("Unable to attach model surface-color data"));
                return;
            }
            state->setSurfaceColorData(result->encodedState);
            state->setSourceTriangleCount(request->sourceTriangleCount);
            state->setTopologyFingerprint(request->topologyFingerprint);
            state->setDataVersion(static_cast<int>(ModelSurfaceColorCodec::DataVersion));
            storeSurfaceColorRenderProjection(
                makeSurfaceColorRenderProjectionKey(
                    request->topologyFingerprint,
                    result->encodedState,
                    request->sourceTriangleCount),
                result->renderProjection);
            state->setRevision(currentRevision + 1);
        }
        result->committedRevision = currentRevision + 1;
    } else {
        result->committedRevision = currentRevision;
    }
    LOG_INFO("ModelColorPaintComputeCoordinator: stroke model={} token={} changed={} reusedWorker={} computeMs={} guiCommitMs={} roots={} leaves={}",
             modelValue, request->token, result->changed, result->reusedWorkerSession,
             result->computeMs, guiTimer.elapsed(),
             result->state.trianglesToSplit.size(), result->state.leafLabels.size());
    emit strokeFinished(result);
}
