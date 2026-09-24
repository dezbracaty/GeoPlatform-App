#pragma once

#include "GraphicsContextHandle.hpp"
#include "GraphicsRuntime.hpp"
#include "IRenderPickCapability.hpp"
#include "PresentableFrame.hpp"
#include "RendererInstanceContext.hpp"

#include <QObject>
#include <QSize>
#include <QString>

#include <memory>
#include <optional>

class FPSMonitor;
class IDocumentReadView;

namespace GPlatform::Rendering {

enum class RenderVisualMode {
    Standard,
    Wireframe,
    Simplified,
    Highlight
};

struct RendererSessionCreateInfo {
    QSize physicalSize;
    FPSMonitor* fpsMonitor{nullptr};
    RendererInstanceContext rendererContext;
    GraphicsRuntimeProfile graphicsRuntime;
    std::shared_ptr<IDocumentReadView> document;
    // Factory creation is atomic: a non-null Session has consumed this
    // transferred Context and is immediately graphics-ready. On failure the
    // caller retains responsibility for releasing the native handle.
    GraphicsContextHandle graphicsContext;
};

class IRendererSession : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~IRendererSession() override = default;

    virtual GraphicsApi graphicsApi() const noexcept = 0;
    virtual bool isGraphicsContextValid() const noexcept = 0;
    virtual bool createRenderSurface() = 0;

    virtual void start() = 0;
    virtual bool isRunning() const noexcept = 0;
    virtual bool shutdownBlocking(
        unsigned long waitMs, unsigned long terminateWaitMs) = 0;

    virtual void requestResize(const QSize& physicalSize) = 0;
    virtual void requestRender() = 0;
    // Called by the Qt Scene Graph thread at presentation time. The concrete
    // backend Session owns its Published storage and may return a frame only
    // when it can be sampled without waiting.
    virtual std::optional<PresentableFrame> tryAcquireReadyFrame() = 0;
    virtual void acknowledgeFrameAcquired(FrameId frameId) = 0;
    virtual void discardFrame(FrameId frameId) = 0;
    virtual void releaseFrame(const FrameRelease& release) = 0;
    // Backends that recycle a texture while Qt may still sample it require a
    // consumer-side GPU completion. Backends whose acquire handshake already
    // protects the retired texture avoid that per-frame synchronization cost.
    virtual bool requiresConsumerCompletion() const noexcept = 0;
    virtual void setSuspended(bool suspended) = 0;
    virtual bool isFrameReady() const noexcept = 0;

    virtual void setVisualMode(RenderVisualMode mode) = 0;
    virtual void setDebugName(const QString& name) = 0;
    virtual int runtimeId() const noexcept = 0;

    // Optional per-View capability. Its lifetime never exceeds this Session.
    virtual std::shared_ptr<IRenderPickCapability> pickCapability() const noexcept {
        return {};
    }

public slots:
    virtual void requestShutdown() = 0;

signals:
    // Notification only; it intentionally carries no frame data. The
    // authoritative frame remains in the concrete backend Session until
    // tryAcquireReadyFrame() succeeds.
    void frameReady();
    void framePreparationStarted();
    void framePreparationFinished();
};

} // namespace GPlatform::Rendering
