#pragma once
#include "RendererRole.hpp"

#include "IRenderPickCapability.hpp"

#include <BaseID.hpp>

#include <QSizeF>

#include <cstdint>
#include <memory>

namespace GPlatform::Rendering {

struct RenderViewAttachment {
    DBInstanceID viewId{INVALID_DB_ID};
    std::uint64_t generation{0};
    QSizeF logicalSize;
    std::shared_ptr<IRenderPickCapability> pickCapability;
    RendererRole role{RendererRole::Compatibility};
};

/**
 * Application-composition port for renderer-owned View services.
 *
 * Render Host publishes Session lifecycle through this neutral interface.
 * Interaction may implement the sink, but Render Host never depends on the
 * Interaction module. Implementations must accept calls from the Qt scene
 * graph thread and marshal state to their owning thread when necessary.
 */
class IRenderViewLifecycleSink {
public:
    virtual ~IRenderViewLifecycleSink() = default;

    virtual void attachRenderView(RenderViewAttachment attachment) = 0;
    virtual void updateRenderViewSize(DBInstanceID viewId,
                                      std::uint64_t generation,
                                      const QSizeF& logicalSize) = 0;
    virtual void detachRenderView(DBInstanceID viewId,
                                  std::uint64_t generation) = 0;
};

} // namespace GPlatform::Rendering
