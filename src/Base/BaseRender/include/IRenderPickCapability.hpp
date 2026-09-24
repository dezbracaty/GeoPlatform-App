#pragma once

#include "RenderPickTypes.hpp"

#include <QObject>

#include <cstdint>

namespace GPlatform::Rendering {

/**
 * Optional, Session-owned asynchronous picking capability.
 *
 * Implementations enqueue requests onto their private renderer thread. No
 * native renderer object or backend-specific picker type may cross this API.
 */
class IRenderPickCapability : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~IRenderPickCapability() override = default;

    virtual PickFeatures capabilities() const noexcept = 0;
    virtual void requestPick(const RenderPickRequest& request) = 0;
    virtual void cancelPick(std::uint64_t requestId) = 0;

signals:
    void pickCompleted(const GPlatform::Rendering::RenderPickResult& result);
};

} // namespace GPlatform::Rendering
