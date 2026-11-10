#pragma once

#include <libslicer/Library.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace GPlatform {

struct SlicingProfileSnapshot {
    std::uint64_t revision{0};
    libslicer::ResolvedSelection selection;
    libslicer::ConfigSnapshot config;

    bool valid() const noexcept { return revision != 0 && config.valid(); }
};

struct SliceSessionSourceRevision {
    std::uint64_t modelId{0};
    std::uint64_t meshRevision{0};
    int surfaceColorRevision{0};
};

// Immutable value captured at the user-visible slice boundary. Worker code
// consumes this snapshot and never reads the live document or QML bridges.
struct SliceSession {
    std::string id;
    std::uint64_t profileRevision{0};
    std::vector<SliceSessionSourceRevision> sources;
    libslicer::SliceRequest request;

    bool valid() const noexcept {
        return !id.empty() && profileRevision != 0 &&
            !sources.empty() && !request.objects.empty() && request.config.valid();
    }
};

} // namespace GPlatform
