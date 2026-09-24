#pragma once

#include <TempDBScope.hpp>

#include <QString>

#include <memory>

class GlbDB;
struct GlbImportResult;

/**
 * Owns the temporary DB graph for one GLB preview generation job.
 * The DB is never made visible in a business renderer and is destroyed as
 * soon as the preview image has been produced.
 */
class ModelPreviewDBSession final {
public:
    ModelPreviewDBSession() = default;
    ~ModelPreviewDBSession();

    ModelPreviewDBSession(const ModelPreviewDBSession&) = delete;
    ModelPreviewDBSession& operator=(const ModelPreviewDBSession&) = delete;

    std::shared_ptr<GlbDB> publish(
        const QString& filePath,
        GlbImportResult result);
    std::shared_ptr<const GlbDB> db() const noexcept { return m_glb; }

private:
    static QString normalizedPath(const QString& filePath);

    TempDBScope m_scope;
    std::shared_ptr<GlbDB> m_glb;
};
