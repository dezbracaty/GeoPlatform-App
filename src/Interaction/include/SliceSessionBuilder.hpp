#pragma once

#include "SliceSession.hpp"

#include <BaseID.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class SliceSessionBuilder final {
public:
    class CapturedRequest;

    struct Request {
        std::vector<DBInstanceID> modelIds;
        DBInstanceID supportTargetModelId;
        std::vector<std::string> supportEnforcerPaths;
        bool generateGcode3mf{true};
    };

    struct Result {
        std::shared_ptr<const GPlatform::SliceSession> session;
        std::string error;

        explicit operator bool() const noexcept {
            return static_cast<bool>(session);
        }
    };

    struct CaptureResult {
        std::shared_ptr<const CapturedRequest> request;
        std::string error;

        explicit operator bool() const noexcept {
            return static_cast<bool>(request);
        }
    };

    // Fast document-thread phase. Captures scalar DB state and retains an
    // immutable shallow VTK snapshot; it never expands the mesh into slicer
    // vertex/triangle arrays.
    static CaptureResult capture(const Request& request);

    // CPU-heavy phase. Safe to run on a worker because CapturedRequest owns
    // values only and never reaches back into DocumentManager or a live DB.
    static Result build(const CapturedRequest& request,
                        const std::function<bool()>& isCancelled = {});

    // Convenience path for tests and non-interactive callers.
    static Result build(const Request& request);
};
