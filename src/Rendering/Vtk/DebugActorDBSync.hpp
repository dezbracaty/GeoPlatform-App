#pragma once

#include "TransientPolyDataActorDBSync.hpp"

/**
 * Keeps the established DebugActorDB registration while reusing the generic
 * transient PolyData rendering implementation.
 */
class DebugActorDBSync final : public TransientPolyDataActorDBSync {};
