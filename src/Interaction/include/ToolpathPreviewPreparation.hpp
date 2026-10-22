#pragma once
#include <ToolpathPreviewTypes.hpp>
#include <GCodeSource.hpp>
#include <QStringList>
#include <functional>
#include <libslicer/Toolpath.hpp>
#include <libslicer/Library.hpp>

namespace GPlatform {
using PreviewCancelCheck = std::function<bool()>;
enum class FiberSourceMarkerKind { Begin, Cut, TailBegin, Depleted, FinishBegin, Finish, End, Role };
struct FiberSourceMarker {
    std::uint32_t line = 0;
    FiberSourceMarkerKind kind;
    std::uint8_t displayRole = 0;
    bool supportedVersion = false;
};
struct CapturedToolpathSource {
    std::shared_ptr<const GCodeSource> source;
    std::vector<FiberSourceMarker> markers;
};
struct PreparedToolpathPreview {
    ToolpathPreviewData data;
    QStringList warnings;
    QString error;
    QString sourceError;
    bool geometryAvailable = false;
    ToolpathAnnotationStatus annotationStatus = ToolpathAnnotationStatus::NotApplicable;
    bool cancelled = false;
};
CapturedToolpathSource captureToolpathSource(const QString& path, const PreviewCancelCheck& isCancelled = {},
    const GCodeCaptureIO& io = {});
PreparedToolpathPreview prepareToolpathPreview(const libslicer::ToolpathPreview& preview,
    CapturedToolpathSource source, const PreviewCancelCheck& isCancelled = {});
PreparedToolpathPreview prepareImportedToolpath(const QString& path,
    const std::function<libslicer::GCodePreviewResult(const std::string&)>& load,
    const PreviewCancelCheck& isCancelled = {}, const GCodeCaptureIO& io = {});
}
