#pragma once

#include <QVariantMap>
#include <QString>

namespace GPlatform::MachineGCodeUtil {

QString defaultMachineStartGCode();
QString defaultMachineEndGCode();

// 写入 machine_start_gcode 的标记（幂等）
QString primeLineDirectiveMarker();
QString ensurePrimeLineDirective(const QString& machineStartGCode);
bool hasPrimeLineDirective(const QString& machineStartGCode);

// 写入 machine_end_gcode 的复位打印头指令（幂等）
QString endResetDirectiveMarker();
QString ensureEndResetDirective(const QString& machineEndGCode);
bool hasEndResetDirective(const QString& machineEndGCode);

// 在最终 GCode 文件中按“首个打印点”注入起始线
bool injectPrimeLineIntoFile(const QString& gcodePath,
                             const QVariantMap& settings,
                             QString* errorMessage = nullptr);

} // namespace GPlatform::MachineGCodeUtil
