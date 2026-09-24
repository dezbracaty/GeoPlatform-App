#pragma once

#include <IRenderViewLifecycleSink.hpp>
#include <PickTypes.hpp>

#include <QMetaType>
#include <QMutex>
#include <QObject>
#include <QPoint>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>

enum class PickChannel : std::uint8_t {
    Hover,
    Click,
    Cell,
    ToolPreview,
    ToolStroke
};

enum class PickDelivery : std::uint8_t {
    OneShot,
    LatestOnly
};

struct PickSnapshot {
    std::uint64_t requestId{0};
    DBInstanceID viewId{INVALID_DB_ID};
    std::uint64_t viewGeneration{0};
    std::uint64_t viewportRevision{0};
    PickChannel channel{PickChannel::Hover};
    GPlatform::Rendering::PickDetail detail{
        GPlatform::Rendering::PickDetail::Object};
    GPlatform::Rendering::PickStatus status{
        GPlatform::Rendering::PickStatus::Miss};
    QPoint screenPosition;
    PickResult result;

    bool isReusable() const noexcept {
        return status == GPlatform::Rendering::PickStatus::Hit ||
               status == GPlatform::Rendering::PickStatus::Miss;
    }
};

Q_DECLARE_METATYPE(PickChannel)
Q_DECLARE_METATYPE(PickSnapshot)

/**
 * View-scoped Pick router for asynchronous renderer requests, blocking widget
 * requests, and synchronous CPU model queries.
 *
 * Every consumer submits its own semantic detail and channel. There is no
 * process-wide aggregation of Handler requirements. The service keeps at most
 * one completed snapshot per View/channel and one outstanding request for a
 * LatestOnly channel.
 */
class PickService final
    : public QObject,
      public GPlatform::Rendering::IRenderViewLifecycleSink {
    Q_OBJECT

public:
    explicit PickService(QObject* parent = nullptr);

    void attachRenderView(
        GPlatform::Rendering::RenderViewAttachment attachment) override;
    void updateRenderViewSize(DBInstanceID viewId,
                              std::uint64_t generation,
                              const QSizeF& logicalSize) override;
    void detachRenderView(DBInstanceID viewId,
                          std::uint64_t generation) override;

    std::uint64_t requestForView(
        DBInstanceID viewId,
        const QPoint& screenPosition,
        PickChannel channel,
        GPlatform::Rendering::PickDetail detail,
        PickDelivery delivery = PickDelivery::OneShot);

    /**
     * Synchronous CPU triangle Pick for interactions that must decide whether
     * to consume the current input event. The call never builds or repairs
     * acceleration data; it only reads each Part's eagerly maintained BVH state.
     */
    GPlatform::Rendering::RenderPickResult pickCpuForView(
        DBInstanceID viewId,
        const QPoint& screenPosition,
        GPlatform::Rendering::PickDetail detail =
            GPlatform::Rendering::PickDetail::Object) const;

    /** Exact-position blocking access to the existing renderer Pick path. */
    GPlatform::Rendering::RenderPickResult pickRendererForViewBlocking(
        DBInstanceID viewId,
        const QPoint& screenPosition,
        GPlatform::Rendering::PickDetail detail,
        int timeoutMilliseconds = 250);

    std::optional<PickSnapshot> cachedForView(
        DBInstanceID viewId,
        PickChannel channel,
        GPlatform::Rendering::PickDetail detail,
        const QPoint& screenPosition,
        int toleranceLogicalPixels = 0) const;

    std::optional<PickSnapshot> latestForView(
        DBInstanceID viewId,
        PickChannel channel,
        GPlatform::Rendering::PickDetail detail) const;

    GPlatform::Rendering::PickFeatures capabilitiesForView(
        DBInstanceID viewId) const;
    bool supportsFeature(
        DBInstanceID viewId,
        GPlatform::Rendering::PickFeature feature) const;
    GPlatform::Rendering::RendererRole roleForView(DBInstanceID viewId) const;
    void cancel(std::uint64_t requestId);

signals:
    void pickCompleted(const PickSnapshot& snapshot);

private:
    struct ViewRegistration {
        GPlatform::Rendering::RendererRole role{GPlatform::Rendering::RendererRole::Compatibility};
        std::uint64_t generation{0};
        std::uint64_t viewportRevision{1};
        QSizeF logicalSize;
        std::shared_ptr<GPlatform::Rendering::IRenderPickCapability> capability;
        QMetaObject::Connection completionConnection;
        std::map<PickChannel, std::uint64_t> latestOutstanding;
    };

    struct PendingRequest {
        DBInstanceID viewId{INVALID_DB_ID};
        std::uint64_t viewGeneration{0};
        std::uint64_t viewportRevision{0};
        PickChannel channel{PickChannel::Hover};
        GPlatform::Rendering::PickDetail detail{
            GPlatform::Rendering::PickDetail::Object};
        QPoint screenPosition;
        std::shared_ptr<GPlatform::Rendering::IRenderPickCapability> capability;
    };

    using CacheKey = std::pair<DBInstanceID, PickChannel>;

    static bool supports(
        GPlatform::Rendering::PickFeatures features,
        GPlatform::Rendering::PickDetail detail) noexcept;
    void completeUnsupported(const PendingRequest& pending,
                             std::uint64_t requestId);
    void onRenderPickCompleted(
        const GPlatform::Rendering::RenderPickResult& result);

    mutable QMutex m_mutex;
    std::map<DBInstanceID, ViewRegistration> m_views;
    std::map<std::uint64_t, PendingRequest> m_pending;
    std::map<CacheKey, PickSnapshot> m_cache;
    std::uint64_t m_nextRequestId{1};
};
