#include <AppRenderingRegistration.hpp>
#include "FilamentRenderThread.hpp"
#include "DBRelationsRegistration.hpp"
#include <DocumentManager.hpp>
#include <DocumentManagerReadView.hpp>
#include <ManualSupportOrcaScaffold.hpp>
#include <MaterialDB.hpp>
#include "TestModelGraphFixture.hpp"
#include <ModelSurfaceColorCodec.hpp>
#include <ModelSurfaceColorDB.hpp>
#include <SlicingConfigDB.hpp>
#include <SurfaceColorRenderProjection.hpp>

#include <QGuiApplication>
#include <QOpenGLExtraFunctions>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QTimer>

#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    GPlatform::Rendering::registerAppRendering();
    QGuiApplication app(argc, argv);
    GPlatform::DBRelations::registerModule();

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);

    QOpenGLContext rootContext;
    rootContext.setFormat(format);
    if (!rootContext.create() || !rootContext.isValid()) {
        return 1;
    }

    QOffscreenSurface rootSurface;
    rootSurface.setFormat(rootContext.format());
    rootSurface.create();
    if (!rootSurface.isValid() || !rootContext.makeCurrent(&rootSurface)) {
        return 2;
    }

    auto* sharedContext = new QOpenGLContext();
    sharedContext->setFormat(rootContext.format());
    sharedContext->setShareContext(&rootContext);
    if (!sharedContext->create() || !sharedContext->isValid()) {
        delete sharedContext;
        return 3;
    }

    auto* renderSurface = new QOffscreenSurface();
    renderSurface->setFormat(sharedContext->format());
    renderSurface->create();
    if (!renderSurface->isValid()) {
        delete renderSurface;
        delete sharedContext;
        return 4;
    }

    rootContext.doneCurrent();

    const std::vector<GeomTriangle> nonPickableTriangles{
        GeomTriangle(Vector3(-1.5f, -1.5f, 0.0f),
                     Vector3(-0.05f, -1.5f, 0.0f),
                     Vector3(-0.05f, 1.5f, 0.0f)),
        GeomTriangle(Vector3(-1.5f, -1.5f, 0.0f),
                     Vector3(-0.05f, 1.5f, 0.0f),
                     Vector3(-1.5f, 1.5f, 0.0f))};
    const std::vector<GeomTriangle> pickableTriangles{
        GeomTriangle(Vector3(0.05f, -1.5f, 0.0f),
                     Vector3(1.5f, -1.5f, 0.0f),
                     Vector3(1.5f, 1.5f, 0.0f)),
        GeomTriangle(Vector3(0.05f, -1.5f, 0.0f),
                     Vector3(1.5f, 1.5f, 0.0f),
                     Vector3(0.05f, 1.5f, 0.0f))};
    const auto nonPickableGraph = createTestModelGraph(
        nonPickableTriangles, "non-pickable quad");
    const auto pickableGraph = createTestModelGraph(
        pickableTriangles, "pickable quad");
    if (!nonPickableGraph || !pickableGraph) {
        std::fprintf(stderr,
                     "failed to create relation-backed model graph: object=%d "
                     "part=%d geometry=%d material=%d instance=%d "
                     "partGeometry=%d partMaterial=%d\n",
                     nonPickableGraph.object && nonPickableGraph.object->isValid(),
                     nonPickableGraph.part && nonPickableGraph.part->isValid(),
                     nonPickableGraph.geometry && nonPickableGraph.geometry->isValid(),
                     nonPickableGraph.material && nonPickableGraph.material->isValid(),
                     nonPickableGraph.instance && nonPickableGraph.instance->isValid(),
                     nonPickableGraph.part && static_cast<bool>(nonPickableGraph.part->geometry()),
                     nonPickableGraph.part && static_cast<bool>(nonPickableGraph.part->getMaterial()));
        return 9;
    }
    auto nonPickableMesh = nonPickableGraph.instance;
    auto pickableMesh = pickableGraph.instance;
    auto material = nonPickableGraph.part->getMaterial();
    material->setDiffuseColor(Vector3(0.8f, 0.3f, 0.1f));
    pickableGraph.part->getMaterial()->copyValuesFrom(*material);

    ManualSupportOrcaScaffold surfacePainter;
    surfacePainter.load_mesh(
        {{0.05f, -1.5f, 0.0f}, {1.5f, -1.5f, 0.0f},
         {1.5f, 1.5f, 0.0f}, {0.05f, 1.5f, 0.0f}},
        {{0, 1, 2}, {0, 2, 3}},
        {{-1, -1, 1}, {0, -1, -1}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
    if (!surfacePainter.paint_triangle(0, {1.1f, -1.0f, 0.0f}, 1) ||
        !surfacePainter.paint_triangle(1, {0.4f, 1.0f, 0.0f}, 2)) {
        return 10;
    }
    std::string surfaceColorData;
    if (!ModelSurfaceColorCodec::encode(
            surfacePainter.snapshot_state(), &surfaceColorData)) {
        return 11;
    }
    auto surfaceColors = trans::TransDB::create<ModelSurfaceColorDB>();
    auto slicingConfig = trans::TransDB::create<GPlatform::SlicingConfigDB>();
    if (surfaceColors) {
        DocumentManager::instance()->attachOwnedChild(
            pickableGraph.part->getDBInstanceID(),
            surfaceColors->getDBInstanceID(),
            ModelPartDB::kSurfaceColorsRelation);
    }
    if (!surfaceColors || !slicingConfig ||
        pickableGraph.part->surfaceColors() != surfaceColors) {
        return 12;
    }
    slicingConfig->setFilaments({
        {0, {}, {}, {}, {}, QColor(QStringLiteral("#ffff0000")), 1.75},
        {1, {}, {}, {}, {}, QColor(QStringLiteral("#ff00ff00")), 1.75}});
    pickableMesh->setSlicingConfigDBId(slicingConfig->getDBInstanceID());
    auto document = std::make_shared<DocumentManagerReadView>(
        *DocumentManager::instance());
    GPlatform::Rendering::RendererInstanceContext rendererContext;
    rendererContext.windowId = DBInstanceID(4242);
    GPlatform::Rendering::FilamentRenderThread renderThread(
        sharedContext, QSize(64, 64), rendererContext, document);
    renderThread.setSurface(renderSurface);
    sharedContext->moveToThread(&renderThread);
    renderSurface->moveToThread(&renderThread);
    renderThread.moveToThread(&renderThread);

    int result = 5;
    bool receivedFrame = false;
    bool runtimeFilteredPickRequested = false;
    bool pickableHitRequested = false;
    bool receivedPickableHit = false;
    bool receivedRuntimeFilteredMiss = false;
    bool surfaceCommitRequested = false;
    bool renderedSurfaceColors = false;
    bool paletteUpdateRequested = false;
    bool renderedUpdatedPalette = false;
    QTimer frameHeartbeat;
    frameHeartbeat.setInterval(50);
    QObject::connect(
        &frameHeartbeat, &QTimer::timeout, &app,
        [&renderThread] { renderThread.requestFrame(); });
    const auto finishWhenComplete = [&]() {
        if (receivedFrame && receivedPickableHit &&
            receivedRuntimeFilteredMiss && renderedSurfaceColors &&
            renderedUpdatedPalette) {
            result = 0;
            frameHeartbeat.stop();
            renderThread.shutDown();
            app.quit();
        }
    };
    QObject::connect(
        &renderThread,
        &GPlatform::Rendering::FilamentRenderThread::textureReady,
        &app,
        [&](unsigned int textureId, const QSize& size) {
            if (textureId == 0 || size != QSize(64, 64)) {
                result = 6;
                renderThread.shutDown();
                app.quit();
                return;
            }
            receivedFrame = true;
            if (rootContext.makeCurrent(&rootSurface)) {
                auto* gl = rootContext.extraFunctions();
                GLuint framebuffer = 0;
                gl->glGenFramebuffers(1, &framebuffer);
                gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
                gl->glFramebufferTexture2D(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, textureId, 0);
                if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                    GL_FRAMEBUFFER_COMPLETE) {
                    std::vector<unsigned char> pixels(
                        static_cast<std::size_t>(size.width() * size.height() * 4));
                    gl->glReadPixels(
                        0, 0, size.width(), size.height(), GL_RGBA,
                        GL_UNSIGNED_BYTE, pixels.data());
                    std::size_t redPixels = 0;
                    std::size_t greenPixels = 0;
                    std::size_t bluePixels = 0;
                    std::size_t yellowPixels = 0;
                    for (std::size_t index = 0;
                         index + 3 < pixels.size(); index += 4) {
                        const auto red = pixels[index];
                        const auto green = pixels[index + 1];
                        const auto blue = pixels[index + 2];
                        redPixels += red > 40 && red > green * 3 / 2 &&
                            red > blue * 3 / 2;
                        greenPixels += green > 40 && green > red * 3 / 2 &&
                            green > blue * 3 / 2;
                        bluePixels += blue > 40 && blue > red * 3 / 2 &&
                            blue > green * 3 / 2;
                        yellowPixels += red > 40 && green > 40 &&
                            red > blue * 3 / 2 && green > blue * 3 / 2;
                    }
                    if (!surfaceCommitRequested) {
                        // Simulate the first completed paint stroke after the
                        // Filament scene is already running. These are raw
                        // ModelSurfaceColorDB notifications; the adapter must
                        // not translate them into a model-owner update.
                        surfaceCommitRequested = true;
                        surfaceColors->setSurfaceColorData(surfaceColorData);
                        surfaceColors->setSourceTriangleCount(
                            static_cast<int>(pickableTriangles.size()));
                        surfaceColors->setTopologyFingerprint(
                            ModelSurfaceColorCodec::topologyFingerprint(
                                pickableTriangles));
                        surfaceColors->setDataVersion(static_cast<int>(
                            ModelSurfaceColorCodec::DataVersion));
                        surfaceColors->setRevision(1);
                    } else if (!paletteUpdateRequested &&
                        redPixels >= 2 && greenPixels >= 2) {
                        renderedSurfaceColors = true;
                        paletteUpdateRequested = true;
                        auto filaments = slicingConfig->getFilaments();
                        filaments[0].color = QColor(QStringLiteral("#ff0000ff"));
                        filaments[1].color = QColor(QStringLiteral("#ffffff00"));
                        slicingConfig->setFilaments(filaments);
                    } else if (paletteUpdateRequested &&
                               bluePixels >= 2 && yellowPixels >= 2) {
                        renderedUpdatedPalette = true;
                    }
                }
                gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
                gl->glDeleteFramebuffers(1, &framebuffer);
                rootContext.doneCurrent();
            }
            // This test does not consume the published texture. Retire it
            // immediately so subsequent Pick-driven frames can reuse buffers.
            renderThread.discardFrame(textureId);
            if (!runtimeFilteredPickRequested && renderedSurfaceColors) {
                runtimeFilteredPickRequested = true;
                // Change Pickable only after the progressively loaded colored
                // entity is present. Submit the two assertions sequentially:
                // same-frame multi-query callbacks are nondeterministic on
                // some OpenGL drivers and are not part of this test's contract.
                nonPickableMesh->setPickable(false);
                GPlatform::Rendering::RenderPickRequest request;
                request.requestId = 1;
                request.normalizedPosition = QPointF(0.25, 0.5);
                request.detail = GPlatform::Rendering::PickDetail::Surface;
                renderThread.requestPick(request);
            } else if (receivedRuntimeFilteredMiss &&
                       !pickableHitRequested) {
                pickableHitRequested = true;
                GPlatform::Rendering::RenderPickRequest request;
                request.requestId = 2;
                request.normalizedPosition = QPointF(0.75, 0.5);
                request.detail = GPlatform::Rendering::PickDetail::Surface;
                renderThread.requestPick(request);
            }
            finishWhenComplete();
        },
        Qt::QueuedConnection);
    QObject::connect(
        &renderThread,
        &GPlatform::Rendering::FilamentRenderThread::pickCompleted,
        &app,
        [&](const GPlatform::Rendering::RenderPickResult& pick) {
            const bool pickableHit =
                pick.requestId == 2 &&
                pick.status == GPlatform::Rendering::PickStatus::Hit &&
                pick.payload.objectId == pickableMesh->getDBInstanceID() &&
                pick.payload.worldPosition.has_value();
            const bool runtimeFilteredMiss =
                pick.requestId == 1 &&
                pick.status == GPlatform::Rendering::PickStatus::Miss &&
                !pick.payload.objectId.isValid();
            if (pick.windowId != rendererContext.windowId ||
                (!pickableHit && !runtimeFilteredMiss)) {
                result = 9;
                renderThread.shutDown();
                app.quit();
                return;
            }
            if (pickableHit && !receivedPickableHit) {
                receivedPickableHit = true;
            } else if (runtimeFilteredMiss &&
                       !receivedRuntimeFilteredMiss) {
                receivedRuntimeFilteredMiss = true;
                // Cross a completed texture frame before issuing the second
                // assertion so render scheduling cannot race a pick callback.
                renderThread.requestFrame();
            }
            finishWhenComplete();
        },
        Qt::QueuedConnection);
    QObject::connect(
        &renderThread,
        &GPlatform::Rendering::FilamentRenderThread::initializationFailed,
        &app,
        [&]() {
            result = 7;
            app.quit();
        },
        Qt::QueuedConnection);

    QTimer::singleShot(15000, &app, [&]() {
        frameHeartbeat.stop();
        std::fprintf(
            stderr,
            "Filament Pick integration timeout: frame=%d "
            "initialHit=%d runtimeMiss=%d surfaceCommit=%d "
            "surfaceColors=%d palette=%d\n",
            receivedFrame, receivedPickableHit,
            receivedRuntimeFilteredMiss, surfaceCommitRequested,
            renderedSurfaceColors,
            renderedUpdatedPalette);
        result = 8;
        renderThread.shutDown();
        app.quit();
    });

    renderThread.start();
    frameHeartbeat.start();
    renderThread.requestFrame();
    app.exec();
    if (renderThread.isRunning()) {
        renderThread.shutDown();
        renderThread.wait(5000);
    }
    return result;
}
