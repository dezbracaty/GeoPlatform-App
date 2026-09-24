#pragma once

#include "IRendererSession.hpp"
#include "RendererBackendDescriptor.hpp"

#include <memory>

namespace GPlatform::Rendering {

class IRendererBackendFactory {
public:
    virtual ~IRendererBackendFactory() = default;

    virtual RendererBackendDescriptor descriptor() const = 0;
    virtual std::shared_ptr<IRendererSession> createSession(
        const RendererSessionCreateInfo& createInfo) const = 0;
};

} // namespace GPlatform::Rendering
