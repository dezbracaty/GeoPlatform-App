#pragma once
#include "ToolpathPreviewTypes.hpp"
#include <QString>
#include <QStringList>
#include <memory>
#include <atomic>

namespace GPlatform {
// Only this lease deletes an App-generated file. Records and in-flight imports
// share it; preview DBs borrow their paths and never acquire a second deleter.
class TemporaryPrintOutput {
public:
    explicit TemporaryPrintOutput(QString path);
    ~TemporaryPrintOutput();
    TemporaryPrintOutput(const TemporaryPrintOutput&) = delete;
    TemporaryPrintOutput& operator=(const TemporaryPrintOutput&) = delete;
    const QString path;
};

struct SliceOutputArtifact {
    QString path;
    std::shared_ptr<TemporaryPrintOutput> lease;
};

struct SliceOutputRecord {
    SliceOutputArtifact gcode, package;
    QString name;
    ToolpathPreviewSourceScope sourceScope = ToolpathPreviewSourceScope::ImportedGCode;
    DBInstanceID printBedId{INVALID_DB_ID}, sourceModelId{INVALID_DB_ID};
    int displayModelId = -1;
    ToolpathPreviewStats stats;
    bool geometryAvailable = false;
    QString previewError;
    QStringList warnings;
    // Calibration outputs retain their native task identity. A shared validity
    // flag invalidates all cached plates without touching worker-owned vectors.
    QString calibrationTaskId;
    int calibrationPlateIndex = -1;
    std::shared_ptr<const std::atomic_bool> calibrationValidity;

    std::shared_ptr<TemporaryPrintOutput> leaseFor(const QString& path) const;
    void setArtifacts(const QString& gcodePath, bool temporaryGcode,
        const QString& packagePath, bool temporaryPackage,
        const std::shared_ptr<const SliceOutputRecord>& previous = {});
    bool validate(QString* error = nullptr);
    static bool readable(const QString& path);
    static QString fileIdentity(const QString& path);
};
}
