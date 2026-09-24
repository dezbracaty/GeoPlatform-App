#pragma once

#include <QMetaType>
#include <QString>
#include <QVariantMap>

namespace GPlatform::Printer {

struct PrinterDevice {
    QString deviceId;
    QString name;
    QString model;
    QString state;
    QString ip;
    bool online{true};
    int route{0};
};

struct PrinterStatus {
    QString deviceId;
    QString name;
    QString ip;
    QString state;
    QString jobId;
    QString fileName;
    QString errorCode;
    QString cameraStreamUrl;
    double progress{0.0};
    double nozzleTemperature{0.0};
    double targetNozzleTemperature{0.0};
    double bedTemperature{0.0};
    double targetBedTemperature{0.0};
    double estimatedTimeSeconds{0.0};
    double printDurationSeconds{0.0};
    int currentLayer{0};
    int totalLayers{0};
    bool cameraAvailable{false};
};



} // namespace GPlatform::Printer

Q_DECLARE_METATYPE(GPlatform::Printer::PrinterDevice)
Q_DECLARE_METATYPE(GPlatform::Printer::PrinterStatus)
