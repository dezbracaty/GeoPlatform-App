#include "GCodeSource.hpp"
#include <QFileInfo>
#include <QDateTime>

namespace GPlatform {
std::shared_ptr<const GCodeSource> GCodeSource::capture(
    const QString& path, qint64 expectedSize, qint64 expectedModified,
    const std::function<void(std::uint32_t, QByteArrayView)>& visitLine,
    const std::function<bool()>& isCancelled, const GCodeCaptureIO& io) {
    auto result = std::make_shared<GCodeSource>();
    const auto fail = [&](GCodeCaptureFailure kind, const QString& error) -> std::shared_ptr<const GCodeSource> {
        result->failure = kind;
        result->error = error;
        result->file.reset();
        result->lineStarts.clear();
        return result;
    };
    const auto cancelled = [&] { return isCancelled && isCancelled(); };
    const auto matches = [&] {
        const QFileInfo info(path);
        return info.isFile() && info.size() == expectedSize &&
            info.lastModified().toMSecsSinceEpoch() == expectedModified;
    };
    if (cancelled()) return fail(GCodeCaptureFailure::Cancelled, QStringLiteral("G-code capture cancelled"));
    if (!QFileInfo::exists(path)) return fail(GCodeCaptureFailure::SourceMissing, QStringLiteral("G-code source is missing."));
    if (!matches()) return fail(GCodeCaptureFailure::SourceChanged, QStringLiteral("G-code source has changed; reload the preview."));
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return fail(GCodeCaptureFailure::SourceOpen, input.errorString());
    try {
        result->file = io.create ? io.create() : std::make_shared<QTemporaryFile>();
        if (!result->file || (!result->file->isOpen() && !result->file->open()))
            return fail(GCodeCaptureFailure::SnapshotCreate, result->file ? result->file->errorString() : QStringLiteral("Unable to create G-code snapshot"));
        if (expectedSize > 0) result->lineStarts.push_back(0);
        QByteArray pending;
        std::uint32_t line = 1;
        while (!input.atEnd()) {
            if (cancelled()) return fail(GCodeCaptureFailure::Cancelled, QStringLiteral("G-code capture cancelled"));
            const auto read = io.read ? io.read(input, 1024 * 1024) : std::optional<QByteArray>(input.read(1024 * 1024));
            if (!read || input.error() != QFileDevice::NoError)
                return fail(GCodeCaptureFailure::SourceRead, QStringLiteral("Unable to read G-code source"));
            const auto& block = *read;
            const auto written = io.write ? io.write(*result->file, block) : result->file->write(block);
            if (written != block.size()) return fail(GCodeCaptureFailure::SnapshotWrite, QStringLiteral("Unable to write G-code snapshot"));
            for (qsizetype i = 0; i < block.size(); ++i) {
                if (block[i] == '\n' && result->byteSize + i + 1 < expectedSize)
                    result->lineStarts.push_back(result->byteSize + i + 1);
            }
            if (result->lineStarts.size() > io.maxIndexedLines)
                return fail(GCodeCaptureFailure::IndexLimit, QStringLiteral("G-code has too many lines to display."));
            if (visitLine) {
                pending.append(block);
                qsizetype begin = 0, end;
                while ((end = pending.indexOf('\n', begin)) >= 0) {
                    visitLine(line++, QByteArrayView(pending.constData() + begin, end - begin));
                    begin = end + 1;
                }
                pending.remove(0, begin);
            }
            result->byteSize += block.size();
        }
        if (visitLine && !pending.isEmpty()) visitLine(line, QByteArrayView(pending));
        if (cancelled()) return fail(GCodeCaptureFailure::Cancelled, QStringLiteral("G-code capture cancelled"));
        if (!matches() || result->byteSize != expectedSize)
            return fail(GCodeCaptureFailure::SourceChanged, QStringLiteral("G-code changed while reading; reload the preview."));
        if (!(io.flush ? io.flush(*result->file) : result->file->flush()))
            return fail(GCodeCaptureFailure::SnapshotFlush, QStringLiteral("Unable to flush G-code snapshot"));
        result->file->close();
        if (!matches()) return fail(GCodeCaptureFailure::SourceChanged, QStringLiteral("G-code changed while reading; reload the preview."));
    } catch (const std::bad_alloc&) { throw; }
    catch (const std::exception& error) {
        return fail(GCodeCaptureFailure::Internal, QString::fromUtf8(error.what()));
    }
    return result;
}
}
