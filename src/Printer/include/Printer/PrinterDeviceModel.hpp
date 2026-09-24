#pragma once

#include "PrinterTypes.hpp"

#include <QAbstractListModel>

namespace GPlatform::Printer {

class PrinterDeviceModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        DeviceIdRole = Qt::UserRole + 1,
        NameRole,
        ModelRole,
        IpRole,
        OnlineRole,
        StateRole,
        AvailabilitySectionRole,
        RouteRole,
    };

    explicit PrinterDeviceModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replaceDevices(const QList<PrinterDevice>& devices);
    const PrinterDevice* findById(const QString& deviceId) const;

private:
    QList<PrinterDevice> m_devices;
};

} // namespace GPlatform::Printer
