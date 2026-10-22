#pragma once
#include <QString>
#include <QByteArrayView>
#include <functional>
#include <cstdint>
#include <QTemporaryFile>
#include <memory>
#include <vector>
#include <limits>
#include <optional>
#include <QFile>

namespace GPlatform {
    enum class GCodeCaptureFailure {
        None, SourceMissing, SourceOpen, SourceRead, SourceChanged,
        SnapshotCreate, SnapshotWrite, SnapshotFlush, IndexLimit, Cancelled, Internal
    };
    struct GCodeCaptureIO {
        std::function<std::shared_ptr<QTemporaryFile>()> create;
        std::function<qint64(QTemporaryFile&, const QByteArray&)> write;
        std::function<bool(QTemporaryFile&)> flush;
        std::function<std::optional<QByteArray>(QFile&, qint64)> read;
        std::size_t maxIndexedLines = std::size_t(std::numeric_limits<int>::max());
    };
    // An immutable private copy of the exact source version used by a preview.
    // Owned by the preview, shared with readers; never rereads a changed original.
    struct GCodeSource {
        std::shared_ptr<QTemporaryFile> file;
        std::vector<qint64> lineStarts;
        qint64 byteSize{0};
        QString error;
        GCodeCaptureFailure failure{GCodeCaptureFailure::None};
        bool available() const { return failure == GCodeCaptureFailure::None && error.isEmpty() && bool(file); }
        bool permitsOriginalFallback() const {
            return failure == GCodeCaptureFailure::SnapshotCreate || failure == GCodeCaptureFailure::SnapshotWrite ||
                failure == GCodeCaptureFailure::SnapshotFlush || failure == GCodeCaptureFailure::IndexLimit;
        }
        static std::shared_ptr<const GCodeSource> capture(
            const QString& path, qint64 expectedSize, qint64 expectedModified,
            const std::function<void(std::uint32_t, QByteArrayView)>& visitLine = {},
            const std::function<bool()>& isCancelled = {}, const GCodeCaptureIO& io = {});
    };
}
