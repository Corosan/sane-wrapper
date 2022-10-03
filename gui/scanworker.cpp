#include "scanworker.h"

#include <exception>

ScanWorker::ScanWorker()
    : m_saneLib(vg_sane::lib::instance()) {
    qRegisterMetaType<vg_sane::device_infos_t>();
    qRegisterMetaType<vg_sane::device_opts_t>();
    qRegisterMetaType<std::string>();
}

void ScanWorker::getDeviceInfos() {
    try {
        emit gotDeviceInfos(m_saneLib->get_device_infos());
    } catch (const std::exception& e) {
        emit errorHappened(tr("unable to get device infos: %1").arg(e.what()));
    }
}
/*
void ScanWorker::openDevice(std::string name) {
    try {
        m_currentDevice = m_saneLib->open_device(name.c_str());
        emit deviceOptionsGot(m_currentDevice.get_option_infos());
    } catch (const std::exception& e) {
        emit errorHappened(QString("unable to open device \"%1\": %2").arg(name.c_str(), e.what()));
    }
}
*/

DeviceListModel::DeviceListModel(ScanWorker& worker, QObject* parent)
    : QAbstractListModel(parent) {
    connect(this, &DeviceListModel::getDeviceInfos, &worker, &ScanWorker::getDeviceInfos);
    connect(&worker, &ScanWorker::gotDeviceInfos, this, &DeviceListModel::gotDeviceInfos);
}
/*
void DeviceListModel::update() {
    beginResetModel();
    m_infos.clear();
    emit getDeviceInfos();
    endResetModel();
}
*/
void DeviceListModel::gotDeviceInfos(vg_sane::device_infos_t val) {
    beginResetModel();
    m_infos.clear();
    for (const auto& v : val) {
        m_infos.append({v->name, v->type, v->model, v->vendor});
    }
    endResetModel();
}

QVariant DeviceListModel::data(const QModelIndex &index, int role) const {
    if (index == QModelIndex() || index.parent() != QModelIndex())
        return {};

    switch (role)
    {
    case Qt::DisplayRole:
        return m_infos[index.row()].m_name;
    case DeviceTypeRole:
        return m_infos[index.row()].m_type;
    case DeviceModelRole:
        return m_infos[index.row()].m_model;
    case DeviceVendorRole:
        return m_infos[index.row()].m_vendor;
    default:
        return {};
    }
}
