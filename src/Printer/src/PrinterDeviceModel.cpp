#include "Printer/PrinterDeviceModel.hpp"

#include <algorithm>

namespace GPlatform::Printer {

PrinterDeviceModel::PrinterDeviceModel(QObject* parent)
    : QAbstractListModel(parent) {}

int PrinterDeviceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_devices.size();
}

QVariant PrinterDeviceModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size()) {
        return {};
    }

    const auto& device = m_devices.at(index.row());
    switch (role) {
    case DeviceIdRole: return device.deviceId;
    case NameRole: return device.name;
    case ModelRole: return device.model;
    case IpRole: return device.ip;
    case OnlineRole: return device.online;
    case StateRole: return device.state;
    case AvailabilitySectionRole: return device.online ? QStringLiteral("online")
                                                       : QStringLiteral("offline");
    case RouteRole: return device.route;
    default: return {};
    }
}

QHash<int, QByteArray> PrinterDeviceModel::roleNames() const {
    return {
        {DeviceIdRole, "deviceId"},
        {NameRole, "name"},
        {ModelRole, "model"},
        {IpRole, "ip"},
        {OnlineRole, "online"},
        {StateRole, "state"},
        {AvailabilitySectionRole, "availabilitySection"},
        {RouteRole, "route"},
    };
}

void PrinterDeviceModel::replaceDevices(const QList<PrinterDevice>& devices) {
    beginResetModel();
    m_devices = devices;
    std::stable_sort(m_devices.begin(), m_devices.end(),
                     [](const PrinterDevice& left, const PrinterDevice& right) {
        if (left.online != right.online) {
            return left.online;
        }
        return left.name.localeAwareCompare(right.name) < 0;
    });
    endResetModel();
}

const PrinterDevice* PrinterDeviceModel::findById(const QString& deviceId) const {
    for (const auto& device : m_devices) {
        if (device.deviceId == deviceId) {
            return &device;
        }
    }
    return nullptr;
}

} // namespace GPlatform::Printer
