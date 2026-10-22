#include "SliceOutputRecord.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>

namespace GPlatform {
TemporaryPrintOutput::TemporaryPrintOutput(QString filePath)
    : path(QFileInfo(filePath).canonicalFilePath().isEmpty() ? QFileInfo(filePath).absoluteFilePath() : QFileInfo(filePath).canonicalFilePath()) {}
TemporaryPrintOutput::~TemporaryPrintOutput() { if (!path.isEmpty()) QFile::remove(path); }

QString SliceOutputRecord::fileIdentity(const QString& path) {
    if (path.isEmpty()) return {};
    QFileInfo info(path);
    auto result = info.canonicalFilePath();
    if (result.isEmpty()) result = QDir::cleanPath(info.absoluteFilePath());
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}
bool SliceOutputRecord::readable(const QString& path) {
    if (path.isEmpty() || !QFileInfo(path).isFile()) return false;
    QFile file(path);
    return file.open(QIODevice::ReadOnly);
}
std::shared_ptr<TemporaryPrintOutput> SliceOutputRecord::leaseFor(const QString& path) const {
    const auto identity = fileIdentity(path);
    if (identity.isEmpty()) return {};
    for (const auto* artifact : {&gcode, &package})
        if (artifact->lease && fileIdentity(artifact->lease->path) == identity) return artifact->lease;
    return {};
}
void SliceOutputRecord::setArtifacts(const QString& gcodePath, bool temporaryGcode,
    const QString& packagePath, bool temporaryPackage, const std::shared_ptr<const SliceOutputRecord>& previous) {
    const auto artifact = [&](const QString& path, bool owned) {
        SliceOutputArtifact result;
        result.path = path;
        result.lease = previous ? previous->leaseFor(path) : nullptr;
        if (!result.lease) result.lease = leaseFor(path);
        if (!result.lease && owned && !path.isEmpty()) result.lease = std::make_shared<TemporaryPrintOutput>(path);
        return result;
    };
    gcode = artifact(gcodePath, temporaryGcode);
    package = artifact(packagePath, temporaryPackage);
}
bool SliceOutputRecord::validate(QString* error) {
    if (!readable(gcode.path)) {
        if (error) *error = QStringLiteral("The generated G-code output is missing or unreadable: %1").arg(gcode.path);
        return false;
    }
    if (!package.path.isEmpty() && !readable(package.path)) {
        warnings << QStringLiteral("The optional G-code 3MF output is unavailable: %1").arg(package.path);
        package.path.clear(); // Keep its lease until this record releases it.
    }
    return true;
}
}
