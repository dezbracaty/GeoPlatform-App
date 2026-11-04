#include <ActorDB.hpp>
#include <ActorDBSync.hpp>
#include <AppRenderingRegistration.hpp>
#include <CameraDB.hpp>
#include <DocumentManager.hpp>
#include <DocumentManagerReadView.hpp>
#include <RendererBackendCatalog.hpp>
#include <VtkDBSyncFactory.hpp>
#include <vtkCubeSource.h>
#include <vtkPolyDataMapper.h>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QTimer>
#include <atomic>
#include <iostream>

using namespace GPlatform::Rendering;
namespace {
constexpr TypeID kConsumerType = static_cast<TypeID>(0x7f000002);
std::atomic<float> renderedExtent{0};
std::atomic<bool> wrongThread{false};

// Neither this DB nor its adapter is known to the already-built SDK.
class ConsumerActorDB final : public ActorDB {
public:
    TypeID getTypeID() const override { return kConsumerType; }
    bool needsVTKSync() const override { return true; }
    std::shared_ptr<AutoRegisterDB> clone() const override { return {}; }
    BoundingBox localBounds() const override {
        const float half = getExtent() * .5f;
        return {{-half,-half,-half},{half,half,half},true};
    }
    std::optional<PickHit> intersectPickRay(const Vector3&, const Vector3&, float) const override {
        return std::nullopt;
    }
    FIELD_PROPERTY_SIMPLE(ConsumerActorDB, float, Extent, notifyGeometryChange)
    void initializeSubActorProperties() override {
        setPropertySafe(PROP_Extent(), 2.0f);
    }
};
class ConsumerActorSync final : public ActorDBSync {
protected:
    bool isGeometryProperty(std::string_view name) const override { return name == "Extent"; }
    void createActor() override {
        auto source = vtkSmartPointer<vtkCubeSource>::New();
        m_source = source;
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(source->GetOutputPort());
        m_actor = vtkSmartPointer<vtkActor>::New();
        m_actor->SetMapper(mapper);
        updateGeometry({});
    }
    void updateGeometry(const std::string&) override {
        if (!isOnRenderThread()) wrongThread = true;
        const auto db = documentDB<ConsumerActorDB>(m_id);
        auto* source = vtkCubeSource::SafeDownCast(m_source);
        if (!db || !source) return;
        source->SetXLength(db->getExtent());
        source->SetYLength(db->getExtent());
        source->SetZLength(db->getExtent());
        source->Update();
        renderedExtent = db->getExtent();
    }
};
}

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    registerAppRendering();
    registerCompiledRendererBackends();
    auto& syncs = VtkDBSyncFactory::getInstance();
    if (!syncs.registerDBSync(trans::TypeID(static_cast<uint32_t>(kConsumerType)),
            [] { return std::make_shared<ConsumerActorSync>(); }, RendererRoleSets::Prepare)) return 1;
    auto actor = AutoRegisterDB::create<ConsumerActorDB>();
    auto camera = AutoRegisterDB::create<CameraDB>();
    camera->setPosition({0,-6,4}); camera->setTarget({0,0,0}); camera->setUpVector({0,0,1});

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4,1); format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24); format.setStencilBufferSize(8);
    QOpenGLContext root;
    root.setFormat(format);
    if (!root.create()) return 2;
    auto* context = new QOpenGLContext;
    context->setFormat(root.format()); context->setShareContext(&root);
    if (!context->create()) { delete context; return 3; }
    auto surface = std::make_shared<QOffscreenSurface>();
    surface->setFormat(context->format()); surface->create();
    if (!surface->isValid()) { delete context; return 4; }

    RendererSessionCreateInfo info;
    info.physicalSize = {96,96};
    info.rendererContext.role = RendererRole::Prepare;
    info.rendererContext.windowId = DBInstanceID(5353);
    info.rendererContext.cameraId = camera->getDBInstanceID();
    info.document = std::make_shared<DocumentManagerReadView>(*DocumentManager::instance());
    const auto device = reinterpret_cast<DeviceIdentity>(root.shareGroup());
    if (!GraphicsRuntime::instance().configureApi(GraphicsApi::OpenGL) ||
        !GraphicsRuntime::instance().bindDevice(GraphicsApi::OpenGL, device)) {
        delete context; return 13;
    }
    info.graphicsRuntime = GraphicsRuntime::instance().profile();
    info.graphicsContext = GraphicsContextHandle::transferred(
        GraphicsApi::OpenGL, device, reinterpret_cast<quintptr>(context));
    info.nativeRenderSurface = reinterpret_cast<quintptr>(surface.get());
    info.renderSurfaceOwner = surface;
    auto factory = RendererBackendCatalog::instance().find(u"vtk");
    auto session = factory ? factory->createSession(info) : nullptr;
    if (!session) { delete context; return 5; }
    auto picker = session->pickCapability();
    if (!picker) { session->shutdownBlocking(5000,0); return 6; }
    int result = 7;
    bool finished = false, firstPickSent = false, updateSent = false, secondPickSent = false;
    auto finish = [&](int code) {
        if (finished) return;
        finished = true; result = code; application.quit();
    };
    QObject::connect(session.get(), &IRendererSession::renderingFailed, &application,
        [&](const QString& error) { std::cerr << error.toStdString() << '\n'; finish(8); });
    QObject::connect(session.get(), &IRendererSession::frameReady, &application, [&] {
        if (finished) return;
        auto frame = session->tryAcquireReadyFrame();
        if (!frame) return;
        if (!frame->hasTexture()) { finish(9); return; }
        // This check never samples the texture, so discard its lease immediately.
        session->discardFrame(frame->frameId);
        if (!firstPickSent || (updateSent && renderedExtent == 3.0f && !secondPickSent)) {
            RenderPickRequest request;
            request.requestId = firstPickSent ? 2 : 1;
            request.normalizedPosition = {.5,.5}; request.detail = PickDetail::Surface;
            if (firstPickSent) secondPickSent = true;
            firstPickSent = true;
            picker->requestPick(request);
        }
    }, Qt::QueuedConnection);
    QObject::connect(picker.get(), &IRenderPickCapability::pickCompleted, &application,
        [&](const RenderPickResult& pick) {
            if (finished) return;
            if (pick.status != PickStatus::Hit || pick.payload.objectId != actor->getDBInstanceID()
                || wrongThread) { finish(10); return; }
            if (pick.requestId == 1) {
                updateSent = true;
                actor->setExtent(3.0f);
                session->requestRender();
            } else if (pick.requestId == 2 && renderedExtent == 3.0f) {
                finish(0);
            }
        }, Qt::QueuedConnection);
    QTimer::singleShot(15000, &application, [&] { finish(11); });
    session->start(); session->requestRender(); application.exec();
    if (!session->shutdownBlocking(5000,0)) return 12;
    if (result) std::cerr << "Consumer DBSync check failed: " << result << '\n';
    return result;
}
