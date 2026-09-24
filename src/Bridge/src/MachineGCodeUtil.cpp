#include "MachineGCodeUtil.hpp"

#include <QFile>
#include <QLocale>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {

constexpr const char* kPrimeDirectiveMarker = "; GPLATFORM_PRIME_LINE_FROM_FIRST_POINT";
constexpr const char* kPrimeBlockBegin = "; GPLATFORM_PRIME_LINE_BEGIN";
constexpr const char* kPrimeBlockEnd = "; GPLATFORM_PRIME_LINE_END";
constexpr const char* kEndResetDirectiveMarker = "; GPLATFORM_END_RESET_HEAD";
constexpr double kPi = 3.14159265358979323846;

double parseDoubleLoose(const QVariant& value, double fallback) {
    bool ok = false;
    double parsed = value.toDouble(&ok);
    if (ok) {
        return parsed;
    }

    QString text = value.toString().trimmed();
    text.remove(QChar(0x00B0));
    text.remove("mm", Qt::CaseInsensitive);
    text = text.trimmed();

    const QRegularExpression re("[-+]?\\d*\\.?\\d+");
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch()) {
        return fallback;
    }

    parsed = match.captured(0).toDouble(&ok);
    return ok ? parsed : fallback;
}

QString formatNumber(double value, int maxDecimals) {
    QString text = QLocale::c().toString(value, 'f', maxDecimals);
    while (text.endsWith('0') && text.contains('.')) {
        text.chop(1);
    }
    if (text.endsWith('.')) {
        text.chop(1);
    }
    if (text.isEmpty()) {
        return "0";
    }
    return text;
}

bool parseAxisValue(const QStringList& tokens, QChar axis, double* outValue) {
    if (outValue == nullptr) {
        return false;
    }

    const QChar target = axis.toUpper();
    for (int i = 1; i < tokens.size(); ++i) {
        const QString token = tokens.at(i).trimmed();
        if (token.size() < 2 || token.at(0).toUpper() != target) {
            continue;
        }
        bool ok = false;
        const double parsed = token.mid(1).toDouble(&ok);
        if (ok) {
            *outValue = parsed;
            return true;
        }
    }
    return false;
}

struct FirstExtrusionPoint {
    int lineIndex{-1};
    double startX{0.0};
    double x{0.0};
    double y{0.0};
    double z{0.0};
    bool relativeXY{false};
    bool relativeE{false};
};

bool locateFirstExtrusionPoint(const QStringList& lines, FirstExtrusionPoint* result) {
    if (result == nullptr) {
        return false;
    }

    double currentX = 0.0;
    double currentY = 0.0;
    double currentZ = 0.0;
    double currentE = 0.0;

    bool relativeXY = false; // G90 by default
    bool relativeE = false;  // treat as M82 default

    for (int idx = 0; idx < lines.size(); ++idx) {
        QString code = lines.at(idx);
        const int commentPos = code.indexOf(';');
        if (commentPos >= 0) {
            code = code.left(commentPos);
        }
        code = code.trimmed();
        if (code.isEmpty()) {
            continue;
        }

        const QStringList tokens = code.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (tokens.isEmpty()) {
            continue;
        }

        const QString command = tokens.first().toUpper();
        if (command == "G90") {
            relativeXY = false;
            continue;
        }
        if (command == "G91") {
            relativeXY = true;
            continue;
        }
        if (command == "M82") {
            relativeE = false;
            continue;
        }
        if (command == "M83") {
            relativeE = true;
            continue;
        }

        if (command == "G92") {
            double value = 0.0;
            if (parseAxisValue(tokens, 'X', &value)) {
                currentX = value;
            }
            if (parseAxisValue(tokens, 'Y', &value)) {
                currentY = value;
            }
            if (parseAxisValue(tokens, 'Z', &value)) {
                currentZ = value;
            }
            if (parseAxisValue(tokens, 'E', &value)) {
                currentE = value;
            }
            continue;
        }

        if (command != "G0" && command != "G1") {
            continue;
        }

        double xValue = 0.0;
        double yValue = 0.0;
        double zValue = 0.0;
        double eValue = 0.0;
        const bool hasX = parseAxisValue(tokens, 'X', &xValue);
        const bool hasY = parseAxisValue(tokens, 'Y', &yValue);
        const bool hasZ = parseAxisValue(tokens, 'Z', &zValue);
        const bool hasE = parseAxisValue(tokens, 'E', &eValue);

        const double nextX = hasX ? (relativeXY ? currentX + xValue : xValue) : currentX;
        const double nextY = hasY ? (relativeXY ? currentY + yValue : yValue) : currentY;
        const double nextZ = hasZ ? (relativeXY ? currentZ + zValue : zValue) : currentZ;
        const double nextE = hasE ? (relativeE ? currentE + eValue : eValue) : currentE;

        const bool hasXYMove = hasX || hasY;
        bool isPositiveExtrusion = false;
        if (hasE) {
            if (relativeE) {
                isPositiveExtrusion = eValue > 1e-6;
            } else {
                isPositiveExtrusion = (nextE - currentE) > 1e-6;
            }
        }

        if (hasXYMove && isPositiveExtrusion) {
            result->lineIndex = idx;
            result->startX = currentX;
            result->x = nextX;
            result->y = nextY;
            result->z = nextZ;
            result->relativeXY = relativeXY;
            result->relativeE = relativeE;
            return true;
        }

        currentX = nextX;
        currentY = nextY;
        currentZ = nextZ;
        currentE = nextE;
    }

    return false;
}

} // namespace

namespace GPlatform::MachineGCodeUtil {

QString defaultMachineStartGCode() {
    return QStringLiteral("G90\nM82\nG92 X0 Y0 Z0 E0\n") + primeLineDirectiveMarker();
}

QString defaultMachineEndGCode() {
    return QStringLiteral("M104 S0\nM140 S0\n") + endResetDirectiveMarker() +
           QStringLiteral("\nG28 ; reset print head\nM84");
}

QString primeLineDirectiveMarker() {
    return QString::fromUtf8(kPrimeDirectiveMarker);
}

bool hasPrimeLineDirective(const QString& machineStartGCode) {
    return machineStartGCode.contains(primeLineDirectiveMarker(), Qt::CaseInsensitive);
}

QString ensurePrimeLineDirective(const QString& machineStartGCode) {
    if (hasPrimeLineDirective(machineStartGCode)) {
        return machineStartGCode;
    }

    const QString marker = primeLineDirectiveMarker();
    const QString trimmed = machineStartGCode.trimmed();
    if (trimmed.isEmpty()) {
        return marker;
    }
    return machineStartGCode + "\n" + marker;
}

QString endResetDirectiveMarker() {
    return QString::fromUtf8(kEndResetDirectiveMarker);
}

bool hasEndResetDirective(const QString& machineEndGCode) {
    return machineEndGCode.contains(endResetDirectiveMarker(), Qt::CaseInsensitive);
}

QString ensureEndResetDirective(const QString& machineEndGCode) {
    if (hasEndResetDirective(machineEndGCode)) {
        return machineEndGCode;
    }

    QStringList lines = machineEndGCode.split('\n', Qt::KeepEmptyParts);
    QStringList snippet;
    snippet << endResetDirectiveMarker();
    snippet << "G28 ; reset print head";

    bool inserted = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith("M84", Qt::CaseInsensitive)) {
            lines.insert(i, snippet.at(1));
            lines.insert(i, snippet.at(0));
            inserted = true;
            break;
        }
    }

    if (!inserted) {
        if (!lines.isEmpty() && !lines.last().isEmpty()) {
            lines.push_back(QString());
        }
        lines.append(snippet);
    }

    return lines.join('\n');
}

bool injectPrimeLineIntoFile(const QString& gcodePath,
                             const QVariantMap& settings,
                             QString* errorMessage) {
    QFile file(gcodePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QString("cannot open file: %1").arg(gcodePath);
        }
        return false;
    }

    QStringList lines;
    QTextStream in(&file);
    while (!in.atEnd()) {
        lines.push_back(in.readLine());
    }
    file.close();

    for (const QString& line : lines) {
        if (line.contains(QString::fromUtf8(kPrimeBlockBegin))) {
            return true;
        }
    }

    FirstExtrusionPoint firstPoint;
    if (!locateFirstExtrusionPoint(lines, &firstPoint)) {
        if (errorMessage) {
            *errorMessage = "first extrusion point not found";
        }
        return false;
    }

    if (firstPoint.lineIndex < 0 || firstPoint.lineIndex > lines.size()) {
        if (errorMessage) {
            *errorMessage = "invalid insert position";
        }
        return false;
    }

    const double primeLength = std::abs(firstPoint.x - firstPoint.startX);
    if (primeLength < 0.05) {
        return true;
    }

    const double lineWidth = parseDoubleLoose(settings.value("line_width"), 0.4);
    const double layerHeight = parseDoubleLoose(settings.value("layer_height"), 0.2);
    const double filamentDiameter = parseDoubleLoose(settings.value("material_diameter"), 1.75);
    const double printSpeed = parseDoubleLoose(settings.value("speed_print"), 35.0);

    const double filamentArea = kPi * std::pow(filamentDiameter * 0.5, 2.0);
    const double depositedArea = std::max(0.01, lineWidth) * std::max(0.01, layerHeight);
    const double primeE = std::max(0.02, (primeLength * depositedArea) / std::max(0.01, filamentArea));
    const double primeFeed = std::max(300.0, printSpeed * 60.0);
    const double travelFeed = std::max(1200.0, primeFeed * 2.0);

    QStringList snippet;
    snippet << QString::fromUtf8(kPrimeBlockBegin);
    snippet << "G90";
    snippet << QString("G1 X%1 Y%2 Z%3 F%4")
                   .arg(formatNumber(firstPoint.startX, 4))
                   .arg(formatNumber(firstPoint.y, 4))
                   .arg(formatNumber(firstPoint.z, 4))
                   .arg(formatNumber(travelFeed, 0));
    snippet << "M83";
    snippet << QString("G1 X%1 Y%2 E%3 F%4")
                   .arg(formatNumber(firstPoint.x, 4))
                   .arg(formatNumber(firstPoint.y, 4))
                   .arg(formatNumber(primeE, 5))
                   .arg(formatNumber(primeFeed, 0));
    snippet << (firstPoint.relativeE ? "M83" : "M82");
    snippet << "G92 E0";
    if (firstPoint.relativeXY) {
        snippet << "G91";
    }
    snippet << QString::fromUtf8(kPrimeBlockEnd);

    QStringList out;
    out.reserve(lines.size() + snippet.size());
    for (int i = 0; i < firstPoint.lineIndex; ++i) {
        out.push_back(lines.at(i));
    }
    out.append(snippet);
    for (int i = firstPoint.lineIndex; i < lines.size(); ++i) {
        out.push_back(lines.at(i));
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QString("cannot write file: %1").arg(gcodePath);
        }
        return false;
    }

    QTextStream writer(&file);
    for (int i = 0; i < out.size(); ++i) {
        writer << out.at(i);
        if (i + 1 < out.size()) {
            writer << '\n';
        }
    }
    file.close();
    return true;
}

} // namespace GPlatform::MachineGCodeUtil
