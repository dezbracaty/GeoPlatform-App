#pragma once

#include <QtGlobal>

#include <memory>

namespace GPlatform::Rendering {

using FrameId = quint64;

class IFrameCompletion {
public:
    virtual ~IFrameCompletion() = default;
    virtual bool wait() noexcept = 0;
};

using FrameCompletion = std::shared_ptr<IFrameCompletion>;

struct FrameRelease {
    FrameId frameId{0};
    FrameCompletion consumerCompletion;

    [[nodiscard]] bool isValid() const noexcept {
        return frameId != 0 && consumerCompletion != nullptr;
    }
};

} // namespace GPlatform::Rendering
