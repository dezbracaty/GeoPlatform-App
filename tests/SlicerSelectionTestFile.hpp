#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

// Tests point the backend at a temporary directory before opening it. Writing
// fixtures directly also lets the tests exercise deleted/invalid presets.
inline QString slicerSelectionTestPath()
{
    return qEnvironmentVariable("GPLATFORM_SLICER_CONFIG_DIR") + QStringLiteral("/selection.conf");
}

inline QJsonObject readSlicerSelectionTestFile()
{
    QFile input(slicerSelectionTestPath());
    return input.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(input.readAll()).object() : QJsonObject{};
}

inline bool writeSlicerSelectionTestFile(const QJsonObject& data)
{
    QSaveFile output(slicerSelectionTestPath());
    if (!output.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(data).toJson();
    return output.write(bytes) == bytes.size() && output.commit();
}
