#include <AppRenderingRegistration.hpp>
#include "renderThread.hpp"
#include "VtkBackendRegistration.hpp"

#include <CameraDB.hpp>
#include <ModelGraphRelations.hpp>
#include "TestModelGraphFixture.hpp"
#include <DocumentManager.hpp>
#include <DocumentManagerReadView.hpp>
#include <FPSMonitor.hpp>
#include <TempDBScope.hpp>
#include <SnapPreviewDB.hpp>
#include <TransientPolyDataActorDB.hpp>
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkMapper.h>
#include <QOpenGLFunctions>

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QTimer>

#include <memory>
#include <iostream>

class ReadbackCompleted final : public GPlatform::Rendering::IFrameCompletion {
public:
    GPlatform::Rendering::CompletionStatus poll() noexcept override {
        return GPlatform::Rendering::CompletionStatus::Complete;
    }
    bool wait() noexcept override { return true; }
};

int main(int argc, char** argv) {
    GPlatform::Rendering::registerAppRendering();
    QGuiApplication app(argc, argv);
    const bool firstFrameOnly = app.arguments().contains("--first-frame");
    GPlatform::Rendering::registerVtkBackendModule();

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 1);
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

    ModelGraphRelations::registerRelations();
    const auto cubeGraph = createTestBoxModel();
    if (!cubeGraph) return 15;
    const auto cube = cubeGraph.instance;
    auto camera = trans::TransDB::create<CameraDB>();
    // A translated marker is outside VTK's default camera. The first frame
    // must already use the document camera, without a second frame to repair it.
    const float focusX = firstFrameOnly ? 100.0f : 0.0f;
    camera->setPosition(Vector3(focusX, -4.0f, 3.0f));
    camera->setTarget(Vector3(focusX, 0.0f, 0.0f));
    camera->setUpVector(Vector3(0.0f, 0.0f, 1.0f));

    TempDBScope hintScope;
    auto hint = hintScope.create<SnapPreviewDB>();
    hint->setPreview(DBInstanceID(4343), {{{focusX - 1, 0, 0}, {focusX + 1, 0, 0}}});
    hint->setVisible(true);
    // A solid green candidate lies fully inside the cube. It must be hidden
    // normally and visible when the owner requests an Orca-style gizmo pass.
    auto overlay = hintScope.create<TransientPolyDataActorDB>();
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->InsertNextPoint(-1, 0, -1);
    points->InsertNextPoint(1, 0, -1);
    points->InsertNextPoint(0, 0, 1);
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    const vtkIdType triangle[3]{0, 1, 2};
    cells->InsertNextCell(3, triangle);
    auto geometry = vtkSmartPointer<vtkPolyData>::New();
    geometry->SetPoints(points);
    geometry->SetPolys(cells);
    overlay->setPickable(false);
    overlay->setGeometry(geometry);
    overlay->getMaterial()->setColor({0, 1, 0});
    overlay->getMaterial()->setDiffuseColor({0, 1, 0});
    overlay->getMaterial()->setLighting(false);
    const int originalOffsetMode=vtkMapper::GetResolveCoincidentTopology();

    auto document = std::make_shared<DocumentManagerReadView>(
        *DocumentManager::instance());
    GPlatform::Rendering::RendererInstanceContext rendererContext;
    rendererContext.role = GPlatform::Rendering::RendererRole::Prepare;
    rendererContext.windowId = DBInstanceID(4343);
    rendererContext.cameraId = camera->getDBInstanceID();
    FPSMonitor fpsMonitor;
    auto renderThread = RenderThread::create(
        QSize(96, 96), &fpsMonitor, rendererContext, document);
    renderThread->setcontext(sharedContext);
    renderThread->setsurface(renderSurface);
    const auto surfaceOwner = std::shared_ptr<QOffscreenSurface>(renderSurface);
    renderThread->setSurfaceOwner(surfaceOwner);
    sharedContext->moveToThread(renderThread.get());
    renderThread->moveToThread(renderThread.get());

    bool receivedFrame = false;
    bool receivedPick = false;
    bool pickRequested = false;
    int result = 5;
    int previewPhase = 0;
    bool finished = false;
    const auto finish = [&](int code) {
        if (finished) return;
        // Queued texture notifications can arrive after shutdown has destroyed
        // the shared textures. Only inspect frames while the check is active.
        finished = true;
        result = code;
        QMetaObject::invokeMethod(renderThread.get(), "shutDown", Qt::QueuedConnection);
        app.quit();
    };
    const auto finishWhenComplete = [&] {
        if (!receivedFrame || !receivedPick || previewPhase != 0) {
            return;
        }
        previewPhase = 1;
        hint->setPreview(DBInstanceID(4344), {{{-1, 0, 0}, {1, 0, 0}}});
        renderThread->requestRender();
    };
    QObject::connect(
        renderThread.get(), &RenderThread::textureReady, &app,
        [&]() {
            if (finished) return;
            auto frame = renderThread->frameExchange()->acquire();
            if (!frame || !frame->hasTexture() || frame->pixelSize != QSize(96, 96)) {
                finish(6);
                return;
            }
            const auto textureId = static_cast<GLuint>(frame->nativeTexture);
            renderThread->frameExchange()->installed(frame->frameId);
            // The hint runs through the interior of the cube. Its overlay must
            // remain visible while the normal surface picker still hits the cube.
            bool hintVisible=false, overlayVisible=false;
            if (rootContext.makeCurrent(&rootSurface)) {
                auto* gl=rootContext.functions();
                GLuint framebuffer=0;gl->glGenFramebuffers(1,&framebuffer);
                gl->glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
                gl->glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textureId,0);
                unsigned char pixels[5*5*4]{};
                gl->glReadPixels(46,46,5,5,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
                for (int i=0;i<25;++i) {
                    hintVisible |= pixels[i*4]>220 && pixels[i*4+1]<100 && pixels[i*4+2]<100;
                    overlayVisible |= pixels[i*4]<100 && pixels[i*4+1]>220 && pixels[i*4+2]<100;
                }
                gl->glBindFramebuffer(GL_FRAMEBUFFER,0);gl->glDeleteFramebuffers(1,&framebuffer);
                rootContext.doneCurrent();
            }
            if (vtkMapper::GetResolveCoincidentTopology()!=originalOffsetMode) {
                std::cerr<<"View hint was obscured or changed global mapper state\n";
                finish(12);return;
            }
            // glReadPixels above synchronously completed the test's only GPU
            // read of this texture. Return that lease through the real exchange.
            renderThread->frameExchange()->release({frame->frameId, std::make_shared<ReadbackCompleted>()});
            renderThread->wakeFrameProgress();
            renderThread->onFrameAcquired();
            if (firstFrameOnly) {
                if (frame->frameId != 1) {
                    std::cerr << "First-frame check missed frame 1\n";
                    finish(17);
                    return;
                }
                if (!hintVisible)
                    std::cerr << "First published frame did not use the document camera\n";
                finish(hintVisible ? 0 : 16);
                return;
            }
            if (previewPhase != 0) {
                if (previewPhase == 1 && !hintVisible) {
                    // Owner changes must take effect without recreating the DB.
                    previewPhase = 2;
                    hint->setPreview(DBInstanceID(4343), {{{-1, 0, 0}, {1, 0, 0}}});
                } else if (previewPhase == 2 && hintVisible) {
                    previewPhase = 3;
                    hint->setPreview(DBInstanceID(4343), {});
                } else if (previewPhase == 3 && !hintVisible) {
                    if (overlayVisible) {
                        std::cerr << "Ordinary transient geometry bypassed scene depth\n";
                        finish(13); return;
                    }
                    previewPhase = 4;
                    overlay->setDrawOnTop(true);
                } else if (previewPhase == 4 && overlayVisible) {
                    previewPhase = 5;
                    overlay->setDrawOnTop(false);
                } else if (previewPhase == 5 && !overlayVisible) {
                    finish(0);
                    return;
                }
                renderThread->requestRender();
                return;
            }
            receivedFrame = hintVisible;
            if (!pickRequested) {
                pickRequested = true;
                GPlatform::Rendering::RenderPickRequest pickRequest;
                pickRequest.requestId = 1;
                pickRequest.normalizedPosition = QPointF(0.5, 0.5);
                pickRequest.detail = GPlatform::Rendering::PickDetail::Surface;
                renderThread->requestPick(pickRequest);
                renderThread->requestRender();
            }
            finishWhenComplete();
        }, Qt::QueuedConnection);
    QObject::connect(
        renderThread.get(), &RenderThread::pickCompleted, &app,
        [&](const GPlatform::Rendering::RenderPickResult& pick) {
            if (finished) return;
            if (pick.requestId != 1 ||
                pick.status != GPlatform::Rendering::PickStatus::Hit ||
                pick.payload.objectId != cube->getDBInstanceID() ||
                pick.payload.partId != cubeGraph.part->getDBInstanceID() ||
                pick.windowId != rendererContext.windowId) {
                std::cerr << "VTK Pick mismatch request=" << pick.requestId
                          << " status=" << static_cast<int>(pick.status)
                          << " object=" << pick.payload.objectId.getValue()
                          << " expected=" << cube->getDBInstanceID().getValue()
                          << " window=" << pick.windowId.getValue() << '\n';
                finish(7);
                return;
            }
            receivedPick = true;
            finishWhenComplete();
        }, Qt::QueuedConnection);

    QTimer::singleShot(15000, &app, [&] {
        std::cerr << "Snap render check timed out at phase " << previewPhase << " frame=" << receivedFrame << " pick=" << receivedPick << "\n";
        finish(8);
    });

    renderThread->start();
    renderThread->requestRender();
    app.exec();
    if (renderThread->isRunning()) {
        QMetaObject::invokeMethod(
            renderThread.get(), "shutDown", Qt::QueuedConnection);
        renderThread->wait(5000);
    }
    return result;
}
