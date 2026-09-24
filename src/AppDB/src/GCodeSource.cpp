#include "GCodeSource.hpp"
#include <QFileInfo>
#include <QDateTime>
#include <limits>

namespace GPlatform {
    std::shared_ptr<const GCodeSource> GCodeSource::capture(
        const QString& path, qint64 expectedSize, qint64 expectedModified) {
        auto result = std::make_shared<GCodeSource>();
        const auto matches = [&] {
            const QFileInfo info(path);
            return info.exists() && info.size() == expectedSize &&
                   info.lastModified().toMSecsSinceEpoch() == expectedModified;
        };
        QFile input(path);
        if (!matches() || !input.open(QIODevice::ReadOnly)) {
            result->error = QStringLiteral("G-code source is missing or has changed; reload the preview.");
            return result;
        }
        result->file = std::make_shared<QTemporaryFile>();
        if (!result->file->open()) {
            result->error = result->file->errorString();
            return result;
        }
        if (expectedSize > 0)
            result->lineStarts.push_back(0);
        while (!input.atEnd()) {
            const QByteArray block = input.read(1024 * 1024);
            if (block.isEmpty() && input.error() != QFileDevice::NoError) {
                result->error = input.errorString();
                return result;
            }
            if (result->file->write(block) != block.size()) {
                result->error = result->file->errorString();
                return result;
            }
            for (qsizetype i = 0; i < block.size(); ++i)
                if (block[i] == '\n' && result->byteSize + i + 1 < expectedSize)
                    result->lineStarts.push_back(result->byteSize + i + 1);
            result->byteSize += block.size();
        }
        if (!result->file->flush()) {
            result->error = result->file->errorString();
            return result;
        }
        result->file->close();
        if (!matches() || result->byteSize != expectedSize)
            result->error = QStringLiteral("G-code changed while reading; reload the preview.");
        if (result->lineStarts.size() > std::size_t(std::numeric_limits<int>::max()))
            result->error = QStringLiteral("G-code has too many lines to display.");
        return result;
    }
}
