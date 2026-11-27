#pragma once

#include <QDir>
#include <QStandardPaths>

namespace GPlatform {

inline QString userConfigDirectory()
{
    const auto configured = qEnvironmentVariable("GPLATFORM_CONFIG_ROOT");
    if (!configured.isEmpty())
        return QDir(configured).absolutePath();
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    const auto root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#else
    const auto root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
#endif
    return root.isEmpty() ? QString{} : QDir(root).filePath(QStringLiteral("GPlatform"));
}

} // namespace GPlatform
