#pragma once

#include <QString>

class IModelPreviewService {
public:
    virtual ~IModelPreviewService() = default;

    virtual QString requestPreview(const QString& filePath,
                                   const QString& sourceThumbnailUrl = {}) = 0;
    virtual QString previewUrl(const QString& filePath) const = 0;
    virtual void invalidatePreview(const QString& filePath) = 0;
    virtual void pruneCache() = 0;
};
