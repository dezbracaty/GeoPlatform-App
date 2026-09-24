#pragma once
#include <QString>
#include <QTemporaryFile>
#include <memory>
#include <vector>

namespace GPlatform {
    // An immutable private copy of the exact source version used by a preview.
    // Owned by the preview, shared with readers; never rereads a changed original.
    struct GCodeSource {
        std::shared_ptr<QTemporaryFile> file;
        std::vector<qint64> lineStarts;
        qint64 byteSize{0};
        QString error;
        static std::shared_ptr<const GCodeSource> capture(
            const QString& path, qint64 expectedSize, qint64 expectedModified);
    };
}
