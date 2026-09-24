#pragma once

#include <memory>

namespace GPlatform::Rendering {
class IRenderViewLifecycleSink;
}

namespace GPlatform::Interaction { class SnapService; }

class PickService;

// Composition-root hook: installs Interaction implementations behind the
// BaseInteraction/BaseRender ports before QML creates render items.
void registerInteractionRuntime();

std::shared_ptr<GPlatform::Rendering::IRenderViewLifecycleSink>
interactionRenderViewLifecycleSink();
PickService& interactionPickService();
const GPlatform::Interaction::SnapService& interactionSnapService();

// Starts process-wide default interaction handlers after module registration.
// Renderer backends must never own this lifecycle responsibility.
void startDefaultInteractionHandlers();
