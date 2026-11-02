#pragma once

#include <QFileInfo>

enum class ImportFileFormat { Unsupported, Stl, GCode };

// Product import policy only; callers validate local paths and readability.
inline ImportFileFormat classifyImportFile(const QString& path) {
    const QString suffix = QFileInfo(path).suffix();
    if (suffix.compare(QStringLiteral("stl"), Qt::CaseInsensitive) == 0)
        return ImportFileFormat::Stl;
    if (suffix.compare(QStringLiteral("gcode"), Qt::CaseInsensitive) == 0)
        return ImportFileFormat::GCode;
    return ImportFileFormat::Unsupported;
}
