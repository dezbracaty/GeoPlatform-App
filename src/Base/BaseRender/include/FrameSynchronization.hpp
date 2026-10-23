#pragma once

#include <QtGlobal>

#include <memory>

namespace GPlatform::Rendering {

using FrameId = quint64;

enum class CompletionStatus { Pending, Complete, Failed };

class IFrameCompletion {
public:
    virtual ~IFrameCompletion() = default;
    virtual CompletionStatus poll() noexcept = 0;
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
